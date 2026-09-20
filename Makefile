BUILD_DIR ?= build
HOST_CC ?= cc
QEMU ?= qemu-system-aarch64
PYTHON ?= python3
TOOLS_DIR ?= .tools
ZIG ?= $(shell $(PYTHON) tools/fetch_tools.py --directory "$(TOOLS_DIR)" --path zig)
MICROKIT_SDK ?= $(shell $(PYTHON) tools/fetch_tools.py --directory "$(TOOLS_DIR)" --path microkit)

BOARD := qemu_virt_aarch64
CONFIG := debug
SDK_BOARD = $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
SERVERS := console client storage policy audit
IMAGES := $(addprefix $(BUILD_DIR)/,$(addsuffix .elf,$(SERVERS)))
HEADERS := $(wildcard include/tcs/*.h)
HOST_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -Iinclude
TARGET_FLAGS = -target aarch64-freestanding -mcpu=cortex_a53 -mstrict-align \
    -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 -g \
    -Wall -Wextra -Werror -Iinclude -I"$(SDK_BOARD)/include"
export ZIG_GLOBAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-cache)
export ZIG_LOCAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-local-cache)

.PHONY: all test bootstrap image smoke smoke-saved verify-artifacts check-tools check-system
.SECONDARY:
all: test

bootstrap:
	$(PYTHON) tools/fetch_tools.py --directory "$(TOOLS_DIR)"

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/policy_test: lib/policy.c tests/policy_test.c include/tcs/policy.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/policy.c tests/policy_test.c -o "$@"

$(BUILD_DIR)/terminal_test: lib/terminal.c tests/terminal_test.c include/tcs/terminal.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/terminal.c tests/terminal_test.c -o "$@"

test: $(BUILD_DIR)/policy_test $(BUILD_DIR)/terminal_test check-system
	"$(BUILD_DIR)/policy_test"
	"$(BUILD_DIR)/terminal_test"
	$(PYTHON) -m unittest discover -s tests -p '*_test.py'

check-system:
	$(PYTHON) tools/check_system.py system/tcs.system

check-tools:
	@test -f "$(MICROKIT_SDK)/VERSION" || { echo 'Run make bootstrap first, or set MICROKIT_SDK to the extracted 2.3.0 SDK'; exit 1; }
	@test "$$(tr -d '\r\n' < "$(MICROKIT_SDK)/VERSION")" = '2.3.0' || { echo 'TCS Seed requires Microkit 2.3.0'; exit 1; }
	@test "$$("$(ZIG)" version)" = '0.14.1' || { echo 'TCS Seed requires Zig 0.14.1'; exit 1; }

$(BUILD_DIR)/%.o: servers/%.c $(HEADERS) Makefile | $(BUILD_DIR) check-tools
	"$(ZIG)" cc $(TARGET_FLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/policy_core.o: lib/policy.c include/tcs/policy.h Makefile | $(BUILD_DIR) check-tools
	"$(ZIG)" cc $(TARGET_FLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/policy.elf: $(BUILD_DIR)/policy.o $(BUILD_DIR)/policy_core.o
	"$(ZIG)" cc $(TARGET_FLAGS) $^ -L"$(SDK_BOARD)/lib" -Wl,-T,"$(SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(BUILD_DIR)/%.elf: $(BUILD_DIR)/%.o
	"$(ZIG)" cc $(TARGET_FLAGS) $< -L"$(SDK_BOARD)/lib" -Wl,-T,"$(SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(BUILD_DIR)/loader.img: $(IMAGES) system/tcs.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/tcs.system --search-path "$(BUILD_DIR)" \
	    --board $(BOARD) --config $(CONFIG) -o "$@" -r "$(BUILD_DIR)/report.txt"

image: $(BUILD_DIR)/loader.img

verify-artifacts:
	$(PYTHON) tools/verify_artifacts.py

smoke-saved: verify-artifacts
	$(PYTHON) tools/boot_test.py --qemu "$(QEMU)" --image artifacts/loader.img --log "$(BUILD_DIR)/saved-image-boot.log"

smoke: image
	$(PYTHON) tools/boot_test.py --qemu "$(QEMU)" --image "$(BUILD_DIR)/loader.img" --log "$(BUILD_DIR)/boot.log"
