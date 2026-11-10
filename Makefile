# PicoStation Multi House V3 for macOS (Apple Silicon)
# Core tests: `make test` runs every core/tests/test_*.c;
# `make test-<mod>` builds core/src/<mod>/*.c plus the module dirs listed (one per line)
# in core/tests/<mod>.deps (default dependency: base) and runs core/tests/test_<mod>*.c.
CC       ?= clang
CSTD     := -std=c11 -D_DARWIN_C_SOURCE
WARN     := -Wall -Wextra -Wno-unused-parameter
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS_T := $(CSTD) $(WARN) -g -O1 $(SAN) -Icore/include
BUILD    ?= $(HOME)/.cache/picostation-mh-build

.PHONY: test clean app
test:
	@set -e; for f in $$(ls core/tests/test_*.c 2>/dev/null); do \
	  m=$$(basename $$f .c); m=$${m#test_}; $(MAKE) --no-print-directory test-$$m; done

test-%:
	@mkdir -p $(BUILD)/tests
	@m=$*; \
	deps="base"; [ -f core/tests/$$m.deps ] && deps="$$(cat core/tests/$$m.deps)"; \
	srcs=""; for d in $$deps $$m; do srcs="$$srcs $$(ls core/src/$$d/*.c 2>/dev/null)"; done; \
	srcs=$$(echo $$srcs | tr ' ' '\n' | awk '!s[$$0]++' | tr '\n' ' '); \
	for t in core/tests/test_$$m*.c; do \
	  out=$(BUILD)/tests/$$(basename $$t .c); \
	  $(CC) $(CFLAGS_T) $$t $$srcs -o $$out $$(cat core/tests/$$m.ldflags 2>/dev/null) && echo "== $$out" && $$out; \
	done

clean:
	rm -rf "$(BUILD)"

.PHONY: lint-dash
lint-dash:
	@! grep -rIl --exclude-dir=.git --exclude-dir=build --exclude=LICENSE $$(printf '\xe2\x80\x94') . && echo "no em dash"
