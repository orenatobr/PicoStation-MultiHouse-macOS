/* mh_disk_mac.c: privileged raw device open for the macOS disk layer (no Apple frameworks). */
#include "mh_disk_mac.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int mh_authopen(const char *path, int oflags, int timeout_ms) {
    if (!path || path[0] != '/' || oflags < 0 || oflags > 3) {
        errno = EINVAL;
        return -1;
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, sv[0]);
    posix_spawn_file_actions_addclose(&fa, sv[1]);
    char flags[8];
    snprintf(flags, sizeof flags, "%d", oflags);
    char *argv[] = {(char *)MH_AUTHOPEN_PATH, (char *)"-stdoutpipe", (char *)"-o", flags,
                    (char *)path, NULL};
    pid_t pid;
    int rc = posix_spawn(&pid, MH_AUTHOPEN_PATH, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(sv[1]);
    if (rc != 0) {
        close(sv[0]);
        errno = rc;
        return -1;
    }

    int fd = -1;
    int64_t deadline = now_ms() + timeout_ms;
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left < 0) left = 0;
        struct pollfd p = {.fd = sv[0], .events = POLLIN};
        int pr = poll(&p, 1, (int)left);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) break; /* timeout or error */
        char data[64];
        union {
            struct cmsghdr h;
            char buf[CMSG_SPACE(sizeof(int))];
        } u;
        struct iovec iov = {.iov_base = data, .iov_len = sizeof data};
        struct msghdr msg;
        memset(&msg, 0, sizeof msg);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = u.buf;
        msg.msg_controllen = sizeof u.buf;
        ssize_t n = recvmsg(sv[0], &msg, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break; /* EOF: authopen exited without sending */
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS &&
                c->cmsg_len >= CMSG_LEN(sizeof(int))) {
                int got;
                memcpy(&got, CMSG_DATA(c), sizeof got);
                if (fd < 0) fd = got;
                else close(got);
            }
        }
        if (fd >= 0) break;
    }
    close(sv[0]);
    if (fd < 0) kill(pid, SIGTERM);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (fd >= 0 && !(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
        close(fd);
        fd = -1;
    }
    if (fd < 0) errno = EACCES;
    return fd;
}

int mh_disk_open_raw(const char *bsd_name, bool write, int *used_auth) {
    if (used_auth) *used_auth = 0;
    char path[32];
    if (!mh_disk_device_path(bsd_name, true, path, sizeof path)) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, write ? O_RDWR : O_RDONLY);
    if (fd < 0 && write && (errno == EACCES || errno == EPERM)) {
        fd = mh_authopen(path, O_RDWR, 120000);
        if (fd >= 0 && used_auth) *used_auth = 1;
    }
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode)) {
        close(fd);
        errno = ENOTBLK;
        return -1;
    }
    fcntl(fd, F_NOCACHE, 1);
    return fd;
}

bool mh_disk_fd_geometry(int fd, uint64_t *size, uint32_t *block_size) {
    uint32_t bs = 0;
    uint64_t cnt = 0;
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &bs) != 0 || ioctl(fd, DKIOCGETBLOCKCOUNT, &cnt) != 0 || !bs)
        return false;
    if (size) *size = cnt * bs;
    if (block_size) *block_size = bs;
    return true;
}
