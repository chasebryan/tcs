BUILD_DIR ?= build
HOST_CC ?= cc
QEMU ?= qemu-system-aarch64
PYTHON ?= python3
TOOLS_DIR ?= .tools
ZIG ?= $(shell $(PYTHON) tools/fetch_tools.py --directory "$(TOOLS_DIR)" --path zig)
MICROKIT_SDK ?= $(shell $(PYTHON) tools/fetch_tools.py --directory "$(TOOLS_DIR)" --path microkit)

BOARD := qemu_virt_aarch64
CONFIG := debug
ifneq ($(CONFIG),debug)
$(error CONFIG must remain debug; use terminal-release-image for the separate release profile)
endif
SDK_BOARD = $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
RELEASE_SDK_BOARD = $(MICROKIT_SDK)/board/$(BOARD)/release
RELEASE_DIR := $(BUILD_DIR)/release
SERVERS := console client storage policy audit
IMAGES := $(addprefix $(BUILD_DIR)/,$(addsuffix .elf,$(SERVERS)))
TERMINAL_IMAGES := $(addprefix $(BUILD_DIR)/,$(addsuffix .elf,terminal serial client storage policy audit))
RELEASE_IMAGES := $(addprefix $(RELEASE_DIR)/,$(addsuffix .elf,terminal serial client storage policy audit))
HEADERS := $(wildcard include/tcs/*.h)
HOST_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -Iinclude
TARGET_COMMON_FLAGS = -target aarch64-freestanding -mcpu=cortex_a53 -mstrict-align \
    -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -O2 -g \
    -Wall -Wextra -Werror -Iinclude
TARGET_FLAGS = $(TARGET_COMMON_FLAGS) -DTCS_DEBUG_PROFILE=1 -I"$(SDK_BOARD)/include"
RELEASE_FLAGS = $(TARGET_COMMON_FLAGS) -DTCS_RELEASE_PROFILE=1 -I"$(RELEASE_SDK_BOARD)/include"
export ZIG_GLOBAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-cache)
export ZIG_LOCAL_CACHE_DIR := $(abspath $(BUILD_DIR)/zig-local-cache)

.PHONY: all test bootstrap image smoke smoke-saved verify-artifacts check-tools check-system terminal-image terminal-smoke terminal-run terminal-smoke-saved
.PHONY: terminal-release-image terminal-release-smoke terminal-release-smoke-saved terminal-release-run
.SECONDARY:
all: test

bootstrap:
	$(PYTHON) tools/fetch_tools.py --directory "$(TOOLS_DIR)"

$(BUILD_DIR):
	mkdir -p "$@"

$(RELEASE_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/policy_test: lib/policy.c tests/policy_test.c include/tcs/policy.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/policy.c tests/policy_test.c -o "$@"

$(BUILD_DIR)/terminal_test: lib/terminal.c tests/terminal_test.c include/tcs/terminal.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/terminal.c tests/terminal_test.c -o "$@"

$(BUILD_DIR)/serial_test: lib/serial.c lib/terminal.c tests/serial_test.c include/tcs/serial.h include/tcs/terminal.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/serial.c lib/terminal.c tests/serial_test.c -o "$@"

$(BUILD_DIR)/serial_server_test: tests/serial_server_test.c tests/support/microkit.h servers/serial.c lib/serial.c $(HEADERS) | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -Itests/support -fsanitize=address,undefined lib/serial.c tests/serial_server_test.c -o "$@"

$(BUILD_DIR)/status_ipc_test: tests/status_ipc_test.c tests/support/microkit.h servers/policy.c servers/storage.c servers/client.c lib/policy.c $(HEADERS) | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -Itests/support -fsanitize=address,undefined lib/policy.c tests/status_ipc_test.c -o "$@"

$(BUILD_DIR)/terminal_server_test: tests/terminal_server_test.c tests/support/microkit.h servers/terminal.c lib/terminal.c $(HEADERS) | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -Itests/support -fsanitize=address,undefined lib/terminal.c tests/terminal_server_test.c -o "$@"

test: $(BUILD_DIR)/policy_test $(BUILD_DIR)/terminal_test $(BUILD_DIR)/serial_test $(BUILD_DIR)/serial_server_test $(BUILD_DIR)/status_ipc_test $(BUILD_DIR)/terminal_server_test check-system
	"$(BUILD_DIR)/policy_test"
	"$(BUILD_DIR)/terminal_test"
	"$(BUILD_DIR)/serial_test"
	"$(BUILD_DIR)/serial_server_test"
	"$(BUILD_DIR)/status_ipc_test"
	"$(BUILD_DIR)/terminal_server_test"
	HOST_CC="$(HOST_CC)" $(PYTHON) -m unittest discover -s tests -p '*_test.py'

check-system:
	$(PYTHON) tools/check_system.py system/tcs.system
	$(PYTHON) tools/check_system.py system/terminal.system --profile terminal

check-tools:
	@test -f "$(MICROKIT_SDK)/VERSION" || { echo 'Run make bootstrap first, or set MICROKIT_SDK to the extracted 2.3.0 SDK'; exit 1; }
	@test "$$(tr -d '\r\n' < "$(MICROKIT_SDK)/VERSION")" = '2.3.0' || { echo 'TCS Seed requires Microkit 2.3.0'; exit 1; }
	@test "$$("$(ZIG)" version)" = '0.14.1' || { echo 'TCS Seed requires Zig 0.14.1'; exit 1; }

$(BUILD_DIR)/%.o: servers/%.c $(HEADERS) Makefile | $(BUILD_DIR) check-tools
	"$(ZIG)" cc $(TARGET_FLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/policy_core.o: lib/policy.c include/tcs/policy.h Makefile | $(BUILD_DIR) check-tools
	"$(ZIG)" cc $(TARGET_FLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/terminal_core.o: lib/terminal.c include/tcs/terminal.h Makefile | $(BUILD_DIR) check-tools
	"$(ZIG)" cc $(TARGET_FLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/serial_core.o: lib/serial.c include/tcs/serial.h Makefile | $(BUILD_DIR) check-tools
	"$(ZIG)" cc $(TARGET_FLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/terminal.elf: $(BUILD_DIR)/terminal.o $(BUILD_DIR)/terminal_core.o
	"$(ZIG)" cc $(TARGET_FLAGS) $^ -L"$(SDK_BOARD)/lib" -Wl,-T,"$(SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(BUILD_DIR)/serial.elf: $(BUILD_DIR)/serial.o $(BUILD_DIR)/serial_core.o
	"$(ZIG)" cc $(TARGET_FLAGS) $^ -L"$(SDK_BOARD)/lib" -Wl,-T,"$(SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(BUILD_DIR)/policy.elf: $(BUILD_DIR)/policy.o $(BUILD_DIR)/policy_core.o
	"$(ZIG)" cc $(TARGET_FLAGS) $^ -L"$(SDK_BOARD)/lib" -Wl,-T,"$(SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(addprefix $(BUILD_DIR)/,console.elf client.elf storage.elf audit.elf): $(BUILD_DIR)/%.elf: $(BUILD_DIR)/%.o
	"$(ZIG)" cc $(TARGET_FLAGS) $< -L"$(SDK_BOARD)/lib" -Wl,-T,"$(SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(BUILD_DIR)/loader.img: $(IMAGES) system/tcs.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/tcs.system --search-path "$(BUILD_DIR)" \
	    --board $(BOARD) --config $(CONFIG) -o "$@" -r "$(BUILD_DIR)/report.txt"

image: $(BUILD_DIR)/loader.img

$(BUILD_DIR)/terminal.img: $(TERMINAL_IMAGES) system/terminal.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/terminal.system --search-path "$(BUILD_DIR)" \
	    --board $(BOARD) --config $(CONFIG) -o "$@" -r "$(BUILD_DIR)/terminal-report.txt"

terminal-image: $(BUILD_DIR)/terminal.img

# No release object, library, or image is shared with the debug profiles.
$(RELEASE_DIR)/%.o: servers/%.c $(HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(RELEASE_DIR)/%_core.o: lib/%.c $(HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(RELEASE_DIR)/terminal.elf: $(RELEASE_DIR)/terminal.o $(RELEASE_DIR)/terminal_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/serial.elf: $(RELEASE_DIR)/serial.o $(RELEASE_DIR)/serial_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/policy.elf: $(RELEASE_DIR)/policy.o $(RELEASE_DIR)/policy_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(addprefix $(RELEASE_DIR)/,client.elf storage.elf audit.elf): $(RELEASE_DIR)/%.elf: $(RELEASE_DIR)/%.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $< -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/terminal.img: $(RELEASE_IMAGES) system/terminal.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/terminal.system --search-path "$(RELEASE_DIR)" \
	    --board $(BOARD) --config release -o "$@" -r "$(RELEASE_DIR)/terminal-report.txt"

terminal-release-image: $(RELEASE_DIR)/terminal.img

terminal-release-smoke: terminal-release-image
	$(PYTHON) tools/terminal_boot_test.py --profile release --qemu "$(QEMU)" --image "$(RELEASE_DIR)/terminal.img" --log "$(RELEASE_DIR)/terminal-boot.log"

terminal-release-smoke-saved: verify-artifacts
	$(PYTHON) tools/terminal_boot_test.py --profile release --qemu "$(QEMU)" --image artifacts/terminal-release.img --log "$(BUILD_DIR)/saved-terminal-release-boot.log"

terminal-release-run: terminal-release-image
	"$(QEMU)" -machine virt,virtualization=on -cpu cortex-a53 -m 2G -smp 1 \
	    -display none -serial mon:stdio -nic none -accel tcg \
	    -device loader,file=$(RELEASE_DIR)/terminal.img,addr=0x70000000,cpu-num=0

terminal-smoke: terminal-image
	$(PYTHON) tools/terminal_boot_test.py --qemu "$(QEMU)" --image "$(BUILD_DIR)/terminal.img" --log "$(BUILD_DIR)/terminal-boot.log"

terminal-smoke-saved: verify-artifacts
	$(PYTHON) tools/terminal_boot_test.py --qemu "$(QEMU)" --image artifacts/terminal.img --log "$(BUILD_DIR)/saved-terminal-boot.log"

terminal-run: terminal-image
	"$(QEMU)" -machine virt,virtualization=on -cpu cortex-a53 -m 2G -smp 1 \
	    -display none -serial mon:stdio -nic none -accel tcg \
	    -device loader,file=$(BUILD_DIR)/terminal.img,addr=0x70000000,cpu-num=0

verify-artifacts:
	$(PYTHON) tools/verify_artifacts.py

smoke-saved: verify-artifacts
	$(PYTHON) tools/boot_test.py --qemu "$(QEMU)" --image artifacts/loader.img --log "$(BUILD_DIR)/saved-image-boot.log"

smoke: image
	$(PYTHON) tools/boot_test.py --qemu "$(QEMU)" --image "$(BUILD_DIR)/loader.img" --log "$(BUILD_DIR)/boot.log"
