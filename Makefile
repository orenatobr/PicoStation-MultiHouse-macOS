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

VERSION ?= 0.1.0
APPNAME := PicoStation Multi House V3
APPDIR  := $(BUILD)/$(APPNAME).app
APP_M   := $(wildcard app/*.m) $(wildcard platform/mac/*.m)
APP_C   := $(wildcard core/src/*/*.c) $(wildcard platform/mac/*.c)
APP_FW  := -framework Cocoa -framework DiskArbitration -framework IOKit -framework ImageIO -framework CoreGraphics -framework CoreFoundation -framework UniformTypeIdentifiers
test:
	@set -e; for f in $$(ls core/tests/test_*.c 2>/dev/null); do \
	  m=$$(basename $$f .c); m=$${m#test_}; m=$${m%%_*}; echo $$m; done | sort -u | while read m; do $(MAKE) --no-print-directory test-$$m || exit 1; done

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
	@! grep -rIl --exclude-dir=.git --exclude-dir=build --exclude=LICENSE --exclude=.graphify-autobuild.log --exclude-dir=graphify-out $$(printf '\xe2\x80\x94') . && echo "no em dash"

# Universal-free, arm64-only app bundle, ad-hoc signed.
app:
	@rm -rf "$(APPDIR)"
	@mkdir -p "$(APPDIR)/Contents/MacOS" "$(APPDIR)/Contents/Resources/embedded" "$(BUILD)/obj"
	clang -arch arm64 -O2 -g -std=gnu11 -fobjc-arc -mmacosx-version-min=12.0 -Icore/include -Iapp -Iplatform/mac \
	  $(APP_M) $(APP_C) $(APP_FW) -o "$(APPDIR)/Contents/MacOS/PicoStationMultiHouse"
	@sed 's/@VERSION@/$(VERSION)/' app/Info.plist > "$(APPDIR)/Contents/Info.plist"
	@cp resources/AppIcon.icns "$(APPDIR)/Contents/Resources/AppIcon.icns"
	@cp resources/embedded/* "$(APPDIR)/Contents/Resources/embedded/"
	@xattr -cr "$(APPDIR)" 2>/dev/null || true
	@rm -rf "$(APPDIR)"/Contents/MacOS/*.dSYM
	codesign --force --deep --sign - "$(APPDIR)"
	@echo "built $(APPDIR)"

# Standalone harness for the cheat windows (not part of the product app): make cheat-demo
.PHONY: cheat-demo
cheat-demo:
	@mkdir -p $(BUILD)
	clang -arch arm64 -O1 -g -std=gnu11 -fobjc-arc -mmacosx-version-min=12.0 -Icore/include -Iapp \
	  app/cheat_ui.m app/demo/cheat_ui_demo.m $(wildcard core/src/base/*.c) $(wildcard core/src/cheat/*.c) \
	  -framework Cocoa -framework UniformTypeIdentifiers -o $(BUILD)/cheat-demo
	@echo "built $(BUILD)/cheat-demo (run from the repo root: $(BUILD)/cheat-demo [shot-dir] [serial])"
