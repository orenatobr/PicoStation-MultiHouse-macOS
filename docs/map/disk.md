# disk module (macOS disk layer)

Headers: `core/include/mh/blockdev.h` (interface used by the portable SD code).
Sources: `core/src/blockdev/blockdev.c` (rules, text, file stand-in, handle dispatch),
`platform/mac/mh_disk_mac.{h,m,c}` (DiskArbitration, IOKit, authopen).
Tests: `core/tests/test_blockdev.c` (`make test-blockdev`, portable) and
`platform/mac/tests/test_disk_mac.m` (`tools/test-disk.sh [--images]`).
Addresses are virtual addresses in `mh64.exe`.

## Original function to port function

| Original (VA) | Port | Notes |
|---|---|---|
| sd_enumerate 14002dfd0 | `mh_disk_enumerate` (.m) + `mh_disk_eligible` (core) | Scanned `\\.\PhysicalDrive0..63` (a 0x1e8 byte record each), returned at most 0x20. Here: every whole `IOMedia`, sorted by number, at most `MH_DISK_MAX` = 32. Drive letters (GetLogicalDrives, IOCTL_STORAGE_GET_DEVICE_NUMBER 0x2d1080, GetDriveType == DRIVE_REMOVABLE, GetVolumeInformation label) became `mounted` (count of mounted volumes) and `label` (volume name of the first one). |
| system_disks 140032250 | `mh_disk_system_numbers` | Windows: physical drives behind the Windows directory (IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 0x560000, max 0x40), failure means nothing is offered. Here: the volumes mounted at `/` and `/System/Volumes/Data`, followed up the IORegistry parent chain; every whole `IOMedia` on the way is a system disk (APFS container `disk3` and its physical store `disk0`). Failure to resolve `/` returns 0 and `mh_disk_enumerate` offers nothing, error text "Nao foi possivel identificar o disco do sistema com seguranca." (new text, the original had none). |
| query_info 14002b160 | `mh_disk_query` (+ static `query_one`, `probe_v2`, `read_header_slot`) | Model = `clean_ascii(vendor, 0x30)` + " " + `clean_ascii(product, 0x40)`, serial through `clean_ascii(0x40)`, size, then the PSXRAW probe: block 0 must start with "PSXRAW" and byte 6 in 1..3 (slot A), else the block at 0xA00 with byte 6 == 2 (slot B). `has_v2`=+0x14, `v2_state`=byte 6 (shown as "PSXRAW V<n>"), `v2_flags`=byte 7. Each slot is read with retries sleeping 5,15,25,...,75 ms (8 tries); a successful read that does not match stops the retries and goes to the next slot. Deviation: a permission error (EACCES, EPERM, ENXIO, ENOENT) is not retried. The probe only runs for disks that pass `mh_disk_eligible` with `has_v2` assumed, so the internal SSD or the 2 TB disk are never read. |
| v2_probe_recoverable_physical 140038770, recover_v2_header_from_body 14002bf00 | `MhV2RecoverProbe` hook in `MhDiskOpts` | Both need V2 body formats and the embedded menu size (0x930 sectors), which belong to the V2 module. The hook receives a read-only `MhIo` and the disk size; when it returns true the disk is reported `has_v2=1`, `v2_state=2`, `v2_recovered=1` exactly like the original (state byte forced to 2, header zeroed). |
| identity_matches 14001cc20, sd_refresh_identity 14002ec60 | `mh_disk_identity_matches` | Both are the same code. Original: system_disks, reject if the number is a system disk, query_info again, require size in 1..130e9 and equal to the stored size, reject if a system disk again. Port: same, plus number, BSD name, block size, model, serial and the IORegistry entry id of the IOMedia (`media_id`, changes when the card is removed and reinserted) must be equal, and the live record must be eligible. The credit rule: our own unmount lowers `mounted` and an invalidated header lowers `has_v2`, so the live record gets the known record's `mounted` and `has_v2` before the eligibility test. |
| lock_volumes 14001dd10 | static `unmount_whole` (.m), called by `mh_blockdev_open_write` | Windows opened each volume of the disk, FSCTL_LOCK_VOLUME (0x90018) retried at 40,75,110,...,285 ms (`DVar4` 0x28 step 0x23 until 0x140), FSCTL_DISMOUNT_VOLUME (0x90020), kept the handles (locks) until the write ended. macOS: `DADiskUnmount(disk, kDADiskUnmountOptionWhole)` without force, same retry schedule while the answer is busy (`kDAReturnBusy`, `kDAReturnExclusiveAccess` or `unix_err(EBUSY)` = 0xc010, which is what a mounted volume with a process cwd inside returns), then verification with `getmntinfo` that no volume of the disk is left. Texts kept byte for byte: busy "Um volume do SD esta em uso e nao pode ser bloqueado. Feche o Explorador e outros programas que usam o cartao." (it still says Explorador, rule 4; the UI may rewrite it), any other failure "Nao foi possivel desmontar um volume do SD com seguranca.", more than 16 volumes "O SD possui volumes demais para serem bloqueados com seguranca." (original limit 0x40). Not ported: "Nao foi possivel enumerar os volumes ...", "O volume montado do SD nao pode ser identificado ..." and "A enumeracao dos volumes foi interrompida ...": they described FindFirstVolume failures; getmntinfo cannot fail like that, and a volume that vanished since the scan is simply already unmounted. |
| (volume lock held during the write) | `dev_recheck` | The Windows lock stopped anyone from mounting while writing. macOS has no equivalent held lock for a raw node, so `mh_blockdev_recheck` (run at open and callable before the first write) fails with "Um volume do SD foi montado durante a operacao; a gravacao foi cancelada por seguranca." if any volume of the disk is mounted again. Not done: a DiskArbitration mount-approval dissenter. |
| CreateFileW(PhysicalDriveN, GENERIC_READ\|WRITE) in sd_write_image etc. | `mh_disk_open_raw`, `mh_authopen` (.c) | Opens `/dev/rdiskN`. Write: `open(O_RDWR)` first (works for disk images owned by the user), on EACCES/EPERM falls back to `/usr/libexec/authopen -stdoutpipe -o 2 <path>`, receiving the descriptor over the stdout socket with SCM_RIGHTS (authorization dialog; waits up to 120 s). Read: `open(O_RDONLY)` only (group operator), never a dialog. F_NOCACHE set; the fd must be a character device; geometry from DKIOCGETBLOCKSIZE/COUNT is compared with the identity record. Failure text for write: "Execute como administrador e feche programas usando o SD." (original), for read "Nao foi possivel abrir o SD para leitura.". |
| refresh_disk_properties 14002cd20 | `mh_blockdev_refresh_properties` | Original: sleep 120 ms, IOCTL_DISK_UPDATE_PROPERTIES (0x70140), sleep 250 ms. macOS re-reads the partition map and remounts when the last writer closes the raw node, so the port is: sleep 120 ms, flush, sleep 250 ms; `mh_blockdev_close` then closes. |
| refresh_sd.part.0 14002d600 (list text only) | `mh_disk_label_text` | `"PhysicalDrive<N> | <model or Dispositivo USB/SD> | <size> [| <drive letter>:] [| PSXRAW V<state>]"`. Port: `"disk<N> | ... | <size> [| <volume label>] [| PSXRAW V<state>]"`, size through `mh_wbuf_size`. Strings: `MH_DISK_TXT_NONE` "Nenhum dispositivo USB/SD seguro foi detectado.", `MH_DISK_TXT_FOUND` "Dispositivo detectado. Cartoes V2 existentes sao carregados automaticamente.". The auto-load rule (`has_v2` and `v2_state == 2`) and keeping the selection by disk number are the UI's job. |

## Eligibility (`mh_disk_eligible`)

Original (sd_enumerate): `!is_system && 1 <= size <= 130000000000 && (has_v2 || bus in {USB(7), SD(12), MMC(13)} || (drive letter && removable))`. Fixed disks were excluded by the bus rule plus the size cap, so the 2 TB external NTFS HDD (USB bus, 2 000 365 289 472 bytes) is excluded by the 130 GB cap. Port, all of:

1. whole disk with a valid `diskN` name, not `is_system`, not synthesized (APFS container: it has another whole IOMedia as ancestor), not optical;
2. `1 <= size <= 130e9` (decimal), `block_size == 512`, size multiple of 512 (the rest of the code is LBA512);
3. disk images only with `MH_DISK_ALLOW_IMAGES` (or env `MH_DISK_TEST_ALLOW_IMAGES=1`, see `mh_disk_flags_from_env`); in that mode an image needs no bus match;
4. not (internal and not removable media);
5. bus is USB, SD or MMC (from the DiskArbitration protocol "USB", "Secure Digital", "MMC"), or removable media with a mounted volume, or (`has_v2` and not internal).

Deviation: rules 1 (synthesized, optical), the block size, and the internal-fixed exclusion are additions; `has_v2` alone no longer qualifies an internal disk.

## Privilege flow and safety sequence (`mh_blockdev_open_write`)

1. `known` (what the user picked) must be a valid whole-disk name whose number matches, and eligible.
2. Live query of that number; `mh_disk_identity_matches` (system disks recomputed, eligibility, number/size/block/model/serial/media_id).
3. Unmount every volume (not forced; busy volumes are refused with the original text).
4. Open `/dev/rdiskN` (direct, else authopen dialog), F_NOCACHE.
5. `mh_blockdev_recheck`: identity again, device geometry equal to the record, no volume mounted.
6. The caller gets `dev->io` (an `MhIo`): use `mh_io_write_block_verified`, `mh_io_sync_blocks_verified`, etc. Call `mh_blockdev_recheck` again right before the first write if time passed.
7. `mh_blockdev_refresh_properties`, `mh_blockdev_close` (flush, close).

A device path is built only by `mh_disk_device_path` after `mh_disk_valid_bsd_name` (`^disk[0-9]+$`, at most 6 digits, no leading zero). Partitions (`disk4s1`), `rdisk4`, paths and traversal are rejected.

## Notes and findings

* The original's identity check compared only number and size. The port adds model, serial and the IOMedia registry id.
* `kDADiskDescriptionDeviceInternalKey` is absent for some virtual devices: absent counts as internal (safe side).
* A card left unmounted after a failed write is not remounted by the port; it is remounted by diskarbitrationd after the last writer closes.
* Not testable here: the authopen dialog for a real SD card write (the descriptor passing was tested against a regular file), real SD protocol strings ("Secure Digital"), `v2_probe_recoverable_physical`.
* Tests: `tools/test-disk.sh` (read only: the boot disk and the 2 TB disk are never offered or openable) and `tools/test-disk.sh --images` (hdiutil 64 MB images, attached with `-nomount` and one FAT image: enumerate, refusals, write, verified read back, header slots A and B state, busy volume refusal, unmount, detach).
