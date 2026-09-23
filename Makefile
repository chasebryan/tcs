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
OPERATOR_DIR := $(BUILD_DIR)/operator
FIXTURE_DIR := $(BUILD_DIR)/interactive-fixture
SERVERS := console client storage policy audit
IMAGES := $(addprefix $(BUILD_DIR)/,$(addsuffix .elf,$(SERVERS)))
TERMINAL_IMAGES := $(addprefix $(BUILD_DIR)/,$(addsuffix .elf,terminal serial client storage policy audit))
RELEASE_IMAGES := $(addprefix $(RELEASE_DIR)/,$(addsuffix .elf,terminal serial client storage policy audit))
ISOLATION_DIR := $(BUILD_DIR)/isolation
LIFECYCLE_DIR := $(BUILD_DIR)/lifecycle-test
CONTAINMENT_DIR := $(BUILD_DIR)/containment-test
ISOLATION_PROBES := $(addprefix $(ISOLATION_DIR)/probe,$(addsuffix .elf,1 2 3 4 5 6))
CRYPTO_DIR := third_party/monocypher
CRYPTO_SOURCES := $(CRYPTO_DIR)/monocypher.c $(CRYPTO_DIR)/monocypher-ed25519.c
CRYPTO_HEADERS := $(CRYPTO_DIR)/monocypher.h $(CRYPTO_DIR)/monocypher-ed25519.h
HOST_CRYPTO_DIR := $(BUILD_DIR)/host-sanitized
HOST_CRYPTO_OBJECTS := $(addprefix $(HOST_CRYPTO_DIR)/,monocypher.o monocypher-ed25519.o)
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
.PHONY: isolation-image isolation-smoke isolation-smoke-saved
.PHONY: admin-cross-check
.PHONY: boot-test-image boot-test-smoke boot-test-smoke-saved
.PHONY: admin-ipc-test
.PHONY: admin-test-image admin-test-smoke admin-test-smoke-saved
.PHONY: operator-tools operator-test
.PHONY: interactive-image interactive-fixture-image
.PHONY: interactive-smoke interactive-smoke-saved
.PHONY: lifecycle-test lifecycle-cross-check
.PHONY: lifecycle-image lifecycle-smoke lifecycle-smoke-saved
.PHONY: reduction-test reduction-cross-check
.PHONY: deadline-test deadline-cross-check
.PHONY: containment-image containment-smoke containment-smoke-saved
.SECONDARY:
all: test

bootstrap:
	$(PYTHON) tools/fetch_tools.py --directory "$(TOOLS_DIR)"

$(BUILD_DIR):
	mkdir -p "$@"

$(RELEASE_DIR):
	mkdir -p "$@"

$(ISOLATION_DIR):
	mkdir -p "$@"

$(LIFECYCLE_DIR):
	mkdir -p "$@"

$(CONTAINMENT_DIR):
	mkdir -p "$@"

$(OPERATOR_DIR) $(FIXTURE_DIR):
	mkdir -p "$@"

# Only native sanitizer tests share these mode-independent upstream objects.
# Operator tooling and freestanding guests retain their separate compilation.
$(HOST_CRYPTO_DIR):
	mkdir -p "$@"

$(HOST_CRYPTO_OBJECTS): $(HOST_CRYPTO_DIR)/%.o: $(CRYPTO_DIR)/%.c $(CRYPTO_HEADERS) Makefile | $(HOST_CRYPTO_DIR)
	$(HOST_CC) $(HOST_FLAGS) -I$(CRYPTO_DIR) -fsanitize=address,undefined -c "$<" -o "$@"

$(BUILD_DIR)/policy_test: lib/policy.c tests/policy_test.c include/tcs/policy.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/policy.c tests/policy_test.c -o "$@"

$(BUILD_DIR)/lifecycle_test $(BUILD_DIR)/lifecycle_probe: $(BUILD_DIR)/%: tests/%.c lib/lifecycle.c include/tcs/lifecycle.h Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/lifecycle.c "$<" -o "$@"

$(BUILD_DIR)/lifecycle_runtime_test: tests/lifecycle_runtime_test.c tests/lifecycle/supervisor.c tests/lifecycle/broker.c tests/lifecycle/controller.c tests/lifecycle/runtime.h tests/lifecycle/support/microkit.h lib/lifecycle.c lib/terminal.c $(HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -DTCS_LIFECYCLE_TEST_PROFILE=1 -Itests/lifecycle/support -fsanitize=address,undefined lib/lifecycle.c lib/terminal.c tests/lifecycle_runtime_test.c -o "$@"

$(BUILD_DIR)/containment_runtime_test: tests/containment_runtime_test.c $(wildcard tests/containment/*.c) tests/containment/runtime.h tests/lifecycle/support/microkit.h lib/lifecycle.c lib/reduction.c lib/terminal.c $(HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -DTCS_CONTAINMENT_TEST_PROFILE=1 -Itests/lifecycle/support -fsanitize=address,undefined lib/lifecycle.c lib/reduction.c lib/terminal.c tests/containment_runtime_test.c -o "$@"

lifecycle-test: $(BUILD_DIR)/lifecycle_test $(BUILD_DIR)/lifecycle_probe
	"$(BUILD_DIR)/lifecycle_test"
	TCS_TEST_BUILD_DIR="$(abspath $(BUILD_DIR))" $(PYTHON) -m unittest discover -s tests -p 'lifecycle_model_test.py'

$(BUILD_DIR)/reduction_test $(BUILD_DIR)/reduction_probe: $(BUILD_DIR)/%: tests/%.c lib/reduction.c lib/lifecycle.c include/tcs/reduction.h include/tcs/lifecycle.h Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -pthread -fsanitize=address,undefined lib/lifecycle.c lib/reduction.c "$<" -o "$@"

reduction-test: $(BUILD_DIR)/reduction_test $(BUILD_DIR)/reduction_probe
	"$(BUILD_DIR)/reduction_test"
	TCS_TEST_BUILD_DIR="$(abspath $(BUILD_DIR))" $(PYTHON) -m unittest discover -s tests -p 'reduction_model_test.py'

$(BUILD_DIR)/deadline_test $(BUILD_DIR)/deadline_probe: $(BUILD_DIR)/%: tests/%.c lib/deadline.c lib/reduction.c lib/lifecycle.c include/tcs/deadline.h include/tcs/reduction.h include/tcs/lifecycle.h Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/lifecycle.c lib/reduction.c lib/deadline.c "$<" -o "$@"

deadline-test: $(BUILD_DIR)/deadline_test $(BUILD_DIR)/deadline_probe
	"$(BUILD_DIR)/deadline_test"
	TCS_TEST_BUILD_DIR="$(abspath $(BUILD_DIR))" $(PYTHON) -m unittest discover -s tests -p 'deadline_model_test.py'

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

$(BUILD_DIR)/isolation_test: tests/isolation_test.c tests/isolation/cases.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined tests/isolation_test.c -o "$@"

$(BUILD_DIR)/isolation_observer_test: tests/isolation_observer_test.c tests/isolation/observer.c tests/isolation/cases.h tests/support/microkit.h servers/terminal.c lib/terminal.c $(HEADERS) | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -Itests/support -fsanitize=address,undefined lib/terminal.c tests/isolation_observer_test.c -o "$@"

$(BUILD_DIR)/admin_test: tests/admin_test.c lib/admin.c lib/admin_policy.c lib/policy.c include/tcs/admin.h include/tcs/policy.h $(HOST_CRYPTO_OBJECTS) $(CRYPTO_HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -I$(CRYPTO_DIR) -fsanitize=address,undefined $(HOST_CRYPTO_OBJECTS) lib/admin.c lib/admin_policy.c lib/policy.c tests/admin_test.c -o "$@"

$(BUILD_DIR)/boot_test: tests/boot_test.c lib/boot.c include/tcs/boot.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/boot.c tests/boot_test.c -o "$@"

$(BUILD_DIR)/admin_ipc_test: tests/admin_ipc_test.c servers/admin.c servers/admin_policy.c lib/admin.c lib/admin_policy.c lib/admin_receipt.c lib/policy.c lib/boot.c $(HEADERS) tests/support/microkit.h $(HOST_CRYPTO_OBJECTS) $(CRYPTO_HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -Itests/support -I$(CRYPTO_DIR) -fsanitize=address,undefined $(HOST_CRYPTO_OBJECTS) lib/admin.c lib/admin_policy.c lib/admin_receipt.c lib/policy.c lib/boot.c tests/admin_ipc_test.c -o "$@"

admin-ipc-test: $(BUILD_DIR)/admin_ipc_test
	"$(BUILD_DIR)/admin_ipc_test"

$(BUILD_DIR)/launch_admin_ipc_test: tests/admin_ipc_test.c servers/launch_admin.c servers/admin_policy.c lib/admin.c lib/admin_policy.c lib/admin_receipt.c lib/policy.c lib/launch.c $(HEADERS) tests/support/microkit.h $(HOST_CRYPTO_OBJECTS) $(CRYPTO_HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -DTCS_LAUNCH_IPC_TEST -Itests/support -I$(CRYPTO_DIR) -fsanitize=address,undefined $(HOST_CRYPTO_OBJECTS) lib/admin.c lib/admin_policy.c lib/admin_receipt.c lib/policy.c lib/launch.c tests/admin_ipc_test.c -o "$@"

$(BUILD_DIR)/boot_fixture: tests/boot_fixture.c lib/admin.c lib/boot.c $(HEADERS) $(HOST_CRYPTO_OBJECTS) $(CRYPTO_HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -I$(CRYPTO_DIR) -fsanitize=address,undefined $(HOST_CRYPTO_OBJECTS) lib/admin.c lib/boot.c tests/boot_fixture.c -o "$@"

$(BUILD_DIR)/admin_fixture: tests/boot_fixture.c tests/admin/scenario.h lib/admin.c lib/boot.c $(HEADERS) $(HOST_CRYPTO_OBJECTS) $(CRYPTO_HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -DTCS_ADMIN_SCENARIO -I$(CRYPTO_DIR) -fsanitize=address,undefined $(HOST_CRYPTO_OBJECTS) lib/admin.c lib/boot.c tests/boot_fixture.c -o "$@"

$(BUILD_DIR)/tcs-operator: tools/operator.c lib/launch.c lib/admin.c $(HEADERS) $(CRYPTO_SOURCES) $(CRYPTO_HEADERS) | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -I$(CRYPTO_DIR) $(CRYPTO_SOURCES) lib/admin.c lib/launch.c tools/operator.c -o "$@"

$(BUILD_DIR)/operator_fixture: tools/operator.c tests/operator_entropy.c lib/launch.c lib/admin.c $(HEADERS) $(HOST_CRYPTO_OBJECTS) $(CRYPTO_HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -DTCS_OPERATOR_TEST_ONLY -I$(CRYPTO_DIR) -fsanitize=address,undefined $(HOST_CRYPTO_OBJECTS) lib/admin.c lib/launch.c tools/operator.c tests/operator_entropy.c -o "$@"

$(BUILD_DIR)/operator_verify: tests/operator_verify.c lib/launch.c lib/admin.c $(HEADERS) $(HOST_CRYPTO_OBJECTS) $(CRYPTO_HEADERS) Makefile | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -I$(CRYPTO_DIR) -fsanitize=address,undefined $(HOST_CRYPTO_OBJECTS) lib/admin.c lib/launch.c tests/operator_verify.c -o "$@"

$(BUILD_DIR)/launch_test: tests/launch_test.c lib/launch.c include/tcs/launch.h | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/launch.c tests/launch_test.c -o "$@"

$(BUILD_DIR)/launch_probe: tests/launch_probe.c | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) tests/launch_probe.c -o "$@"

$(BUILD_DIR)/signed_input_test: tests/signed_input_test.c lib/signed_input.c $(HEADERS) | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined lib/signed_input.c tests/signed_input_test.c -o "$@"

$(BUILD_DIR)/signed_terminal_test: tests/signed_terminal_test.c servers/terminal.c tests/support/microkit.h lib/terminal.c lib/signed_input.c lib/admin_receipt.c $(HEADERS) | $(BUILD_DIR)
	$(HOST_CC) $(HOST_FLAGS) -Itests/support -fsanitize=address,undefined lib/terminal.c lib/signed_input.c lib/admin_receipt.c tests/signed_terminal_test.c -o "$@"

operator-tools: $(BUILD_DIR)/tcs-operator

operator-test: operator-tools $(BUILD_DIR)/operator_fixture $(BUILD_DIR)/operator_verify $(BUILD_DIR)/launch_test $(BUILD_DIR)/launch_probe
	"$(BUILD_DIR)/launch_test"
	TCS_TEST_BUILD_DIR="$(abspath $(BUILD_DIR))" $(PYTHON) -m unittest discover -s tests -p 'operator_test.py'

test: $(BUILD_DIR)/launch_probe $(BUILD_DIR)/signed_input_test $(BUILD_DIR)/signed_terminal_test $(BUILD_DIR)/launch_admin_ipc_test
test: $(BUILD_DIR)/lifecycle_test $(BUILD_DIR)/lifecycle_probe $(BUILD_DIR)/lifecycle_runtime_test
test: $(BUILD_DIR)/reduction_test $(BUILD_DIR)/reduction_probe
test: $(BUILD_DIR)/containment_runtime_test
test: $(BUILD_DIR)/deadline_test $(BUILD_DIR)/deadline_probe

test: $(BUILD_DIR)/policy_test $(BUILD_DIR)/terminal_test $(BUILD_DIR)/serial_test $(BUILD_DIR)/serial_server_test $(BUILD_DIR)/status_ipc_test $(BUILD_DIR)/terminal_server_test $(BUILD_DIR)/isolation_test $(BUILD_DIR)/isolation_observer_test $(BUILD_DIR)/admin_test $(BUILD_DIR)/boot_test $(BUILD_DIR)/admin_ipc_test $(BUILD_DIR)/tcs-operator $(BUILD_DIR)/operator_fixture $(BUILD_DIR)/operator_verify $(BUILD_DIR)/launch_test check-system
	"$(BUILD_DIR)/policy_test"
	"$(BUILD_DIR)/lifecycle_test"
	"$(BUILD_DIR)/lifecycle_runtime_test"
	"$(BUILD_DIR)/reduction_test"
	"$(BUILD_DIR)/containment_runtime_test"
	"$(BUILD_DIR)/deadline_test"
	"$(BUILD_DIR)/terminal_test"
	"$(BUILD_DIR)/serial_test"
	"$(BUILD_DIR)/serial_server_test"
	"$(BUILD_DIR)/status_ipc_test"
	"$(BUILD_DIR)/terminal_server_test"
	"$(BUILD_DIR)/isolation_test"
	"$(BUILD_DIR)/isolation_observer_test"
	"$(BUILD_DIR)/admin_test"
	"$(BUILD_DIR)/boot_test"
	"$(BUILD_DIR)/admin_ipc_test"
	"$(BUILD_DIR)/launch_admin_ipc_test"
	"$(BUILD_DIR)/launch_test"
	"$(BUILD_DIR)/signed_input_test"
	"$(BUILD_DIR)/signed_terminal_test"
	TCS_TEST_BUILD_DIR="$(abspath $(BUILD_DIR))" HOST_CC="$(HOST_CC)" $(PYTHON) -m unittest discover -s tests -p '*_test.py'

check-system:
	$(PYTHON) tools/check_system.py system/tcs.system
	$(PYTHON) tools/check_system.py system/terminal.system --profile terminal
	$(PYTHON) tools/check_system.py system/isolation.system --profile isolation
	$(PYTHON) tools/check_system.py system/boot-test.system --profile boot-test
	$(PYTHON) tools/check_system.py system/admin-test.system --profile admin-test
	$(PYTHON) tools/check_system.py system/interactive.system --profile interactive
	$(PYTHON) tools/check_system.py system/lifecycle-test.system --profile lifecycle-test
	$(PYTHON) tools/check_system.py system/containment-test.system --profile containment-test

check-tools:
	@test -f "$(MICROKIT_SDK)/VERSION" || { echo 'Run make bootstrap first, or set MICROKIT_SDK to the extracted 2.3.0 SDK'; exit 1; }
	@test "$$(tr -d '\r\n' < "$(MICROKIT_SDK)/VERSION")" = '2.3.0' || { echo 'TCS Seed requires Microkit 2.3.0'; exit 1; }
	@test "$$("$(ZIG)" version)" = '0.14.1' || { echo 'TCS Seed requires Zig 0.14.1'; exit 1; }

# Build the real verification core for the target, without granting a live RPC.
$(RELEASE_DIR)/admin.o: lib/admin.c $(HEADERS) $(CRYPTO_HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -I$(CRYPTO_DIR) -c "$<" -o "$@"

$(RELEASE_DIR)/admin_policy.o: lib/admin_policy.c $(HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(RELEASE_DIR)/monocypher.o $(RELEASE_DIR)/monocypher-ed25519.o: $(RELEASE_DIR)/%.o: $(CRYPTO_DIR)/%.c $(CRYPTO_HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -I$(CRYPTO_DIR) -c "$<" -o "$@"

admin-cross-check: $(RELEASE_DIR)/admin.o $(RELEASE_DIR)/admin_policy.o $(RELEASE_DIR)/monocypher.o $(RELEASE_DIR)/monocypher-ed25519.o $(RELEASE_DIR)/launch_core.o

# Compile the model for AArch64, but do not link it into any guest image.
$(RELEASE_DIR)/lifecycle_model.o: lib/lifecycle.c include/tcs/lifecycle.h Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

lifecycle-cross-check: $(RELEASE_DIR)/lifecycle_model.o

# Reduction latch: only the separate containment supervisor links this object.
$(RELEASE_DIR)/reduction_model.o: lib/reduction.c include/tcs/reduction.h include/tcs/lifecycle.h Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

reduction-cross-check: $(RELEASE_DIR)/reduction_model.o

# Native-only timing contract: no guest links this object.
$(RELEASE_DIR)/deadline_model.o: lib/deadline.c include/tcs/deadline.h include/tcs/reduction.h include/tcs/lifecycle.h Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

deadline-cross-check: $(RELEASE_DIR)/deadline_model.o

$(addprefix $(CONTAINMENT_DIR)/,observer.o supervisor.o broker.o caller.o worker.o): $(CONTAINMENT_DIR)/%.o: tests/containment/%.c tests/containment/runtime.h $(HEADERS) Makefile | $(CONTAINMENT_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -DTCS_CONTAINMENT_TEST_PROFILE=1 -c "$<" -o "$@"

$(CONTAINMENT_DIR)/observer.elf: $(CONTAINMENT_DIR)/observer.o $(RELEASE_DIR)/terminal_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(CONTAINMENT_DIR)/supervisor.elf: $(CONTAINMENT_DIR)/supervisor.o $(RELEASE_DIR)/lifecycle_model.o $(RELEASE_DIR)/reduction_model.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(addprefix $(CONTAINMENT_DIR)/,broker.elf caller.elf): $(CONTAINMENT_DIR)/%.elf: $(CONTAINMENT_DIR)/%.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(addprefix $(CONTAINMENT_DIR)/,worker_a.elf worker_b.elf): $(CONTAINMENT_DIR)/worker.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(CONTAINMENT_DIR)/containment-test.img: $(addprefix $(CONTAINMENT_DIR)/,observer.elf supervisor.elf broker.elf caller.elf worker_a.elf worker_b.elf) $(RELEASE_DIR)/serial.elf system/containment-test.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/containment-test.system --search-path "$(CONTAINMENT_DIR)" "$(RELEASE_DIR)" --board $(BOARD) --config release -o "$@" -r "$(CONTAINMENT_DIR)/report.txt"

containment-image: $(CONTAINMENT_DIR)/containment-test.img

containment-smoke: containment-image
	$(PYTHON) tools/containment_boot_test.py --qemu "$(QEMU)" --image "$(CONTAINMENT_DIR)/containment-test.img" --log "$(CONTAINMENT_DIR)/boot.log"

containment-smoke-saved: verify-artifacts
	$(PYTHON) tools/containment_boot_test.py --qemu "$(QEMU)" --image artifacts/containment-test.img --log "$(BUILD_DIR)/saved-containment-boot.log"

$(addprefix $(LIFECYCLE_DIR)/,controller.o supervisor.o broker.o): $(LIFECYCLE_DIR)/%.o: tests/lifecycle/%.c tests/lifecycle/runtime.h $(HEADERS) Makefile | $(LIFECYCLE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -DTCS_LIFECYCLE_TEST_PROFILE=1 -c "$<" -o "$@"

$(LIFECYCLE_DIR)/worker_a.o: tests/lifecycle/worker.c tests/lifecycle/runtime.h $(HEADERS) Makefile | $(LIFECYCLE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -DTCS_LIFECYCLE_TEST_PROFILE=1 -DLT_WORKER=1 -c "$<" -o "$@"

$(LIFECYCLE_DIR)/worker_b.o: tests/lifecycle/worker.c tests/lifecycle/runtime.h $(HEADERS) Makefile | $(LIFECYCLE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -DTCS_LIFECYCLE_TEST_PROFILE=1 -DLT_WORKER=2 -c "$<" -o "$@"

$(LIFECYCLE_DIR)/controller.elf: $(LIFECYCLE_DIR)/controller.o $(RELEASE_DIR)/terminal_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(LIFECYCLE_DIR)/supervisor.elf: $(LIFECYCLE_DIR)/supervisor.o $(RELEASE_DIR)/lifecycle_model.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(addprefix $(LIFECYCLE_DIR)/,worker_a.elf worker_b.elf broker.elf): $(LIFECYCLE_DIR)/%.elf: $(LIFECYCLE_DIR)/%.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(LIFECYCLE_DIR)/lifecycle-test.img: $(addprefix $(LIFECYCLE_DIR)/,controller.elf supervisor.elf broker.elf worker_a.elf worker_b.elf) $(RELEASE_DIR)/serial.elf system/lifecycle-test.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/lifecycle-test.system --search-path "$(LIFECYCLE_DIR)" "$(RELEASE_DIR)" --board $(BOARD) --config release -o "$@" -r "$(LIFECYCLE_DIR)/report.txt"

lifecycle-image: $(LIFECYCLE_DIR)/lifecycle-test.img

lifecycle-smoke: lifecycle-image
	$(PYTHON) tools/lifecycle_boot_test.py --qemu "$(QEMU)" --image "$(LIFECYCLE_DIR)/lifecycle-test.img" --log "$(LIFECYCLE_DIR)/boot.log"

lifecycle-smoke-saved: verify-artifacts
	$(PYTHON) tools/lifecycle_boot_test.py --qemu "$(QEMU)" --image artifacts/lifecycle-test.img --log "$(BUILD_DIR)/saved-lifecycle-boot.log"

$(RELEASE_DIR)/boot_probe.o: tests/boot/probe.c $(HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(RELEASE_DIR)/boot_probe.elf: $(RELEASE_DIR)/boot_probe.o $(RELEASE_DIR)/boot_core.o $(RELEASE_DIR)/admin.o $(RELEASE_DIR)/monocypher.o $(RELEASE_DIR)/monocypher-ed25519.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/boot-test.img: $(RELEASE_DIR)/boot_probe.elf $(RELEASE_DIR)/serial.elf system/boot-test.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/boot-test.system --search-path "$(RELEASE_DIR)" \
	    --board $(BOARD) --config release -o "$@" -r "$(RELEASE_DIR)/boot-test-report.txt"

boot-test-image: $(RELEASE_DIR)/boot-test.img

boot-test-smoke: boot-test-image $(BUILD_DIR)/boot_fixture
	$(PYTHON) tools/boot_context_test.py --qemu "$(QEMU)" --image "$(RELEASE_DIR)/boot-test.img" --fixture "$(BUILD_DIR)/boot_fixture" --log "$(BUILD_DIR)/boot-context.log"

boot-test-smoke-saved: verify-artifacts $(BUILD_DIR)/boot_fixture
	$(PYTHON) tools/boot_context_test.py --qemu "$(QEMU)" --image artifacts/boot-test.img --fixture "$(BUILD_DIR)/boot_fixture" --log "$(BUILD_DIR)/saved-boot-context.log"

$(RELEASE_DIR)/admin_server.o: servers/admin.c $(HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(RELEASE_DIR)/admin_policy_server.o: servers/admin_policy.c $(HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(RELEASE_DIR)/admin_%.o: tests/admin/%.c tests/admin/scenario.h servers/terminal.c $(HEADERS) Makefile | $(RELEASE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(RELEASE_DIR)/admin.elf: $(RELEASE_DIR)/admin_server.o $(RELEASE_DIR)/admin.o $(RELEASE_DIR)/admin_receipt_core.o $(RELEASE_DIR)/boot_core.o $(RELEASE_DIR)/monocypher.o $(RELEASE_DIR)/monocypher-ed25519.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/admin_policy.elf: $(RELEASE_DIR)/admin_policy_server.o $(RELEASE_DIR)/admin_policy.o $(RELEASE_DIR)/admin_receipt_core.o $(RELEASE_DIR)/policy_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/admin_client.elf: $(RELEASE_DIR)/admin_client.o $(RELEASE_DIR)/admin_receipt_core.o $(RELEASE_DIR)/terminal_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/admin_boot.elf: $(RELEASE_DIR)/admin_boot.o $(RELEASE_DIR)/boot_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(RELEASE_DIR)/admin-test.img: $(RELEASE_DIR)/admin.elf $(RELEASE_DIR)/admin_policy.elf $(RELEASE_DIR)/admin_client.elf $(RELEASE_DIR)/admin_boot.elf $(RELEASE_IMAGES) system/admin-test.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/admin-test.system --search-path "$(RELEASE_DIR)" \
	    --board $(BOARD) --config release -o "$@" -r "$(RELEASE_DIR)/admin-test-report.txt"

admin-test-image: $(RELEASE_DIR)/admin-test.img

admin-test-smoke: admin-test-image $(BUILD_DIR)/admin_fixture
	$(PYTHON) tools/admin_boot_test.py --qemu "$(QEMU)" --image "$(RELEASE_DIR)/admin-test.img" --fixture "$(BUILD_DIR)/admin_fixture" --log "$(BUILD_DIR)/admin-ipc-boot.log"

admin-test-smoke-saved: verify-artifacts $(BUILD_DIR)/admin_fixture
	$(PYTHON) tools/admin_boot_test.py --qemu "$(QEMU)" --image artifacts/admin-test.img --fixture "$(BUILD_DIR)/admin_fixture" --log "$(BUILD_DIR)/saved-admin-ipc-boot.log"

$(OPERATOR_DIR)/%.o: servers/%.c $(HEADERS) Makefile | $(OPERATOR_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -DTCS_SIGNED_INPUT=1 -DTCS_LAUNCH_MODE=0 -c "$<" -o "$@"

$(FIXTURE_DIR)/%.o: servers/%.c $(HEADERS) Makefile | $(FIXTURE_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -DTCS_SIGNED_INPUT=1 -DTCS_LAUNCH_MODE=1 -c "$<" -o "$@"

$(OPERATOR_DIR)/terminal.elf $(FIXTURE_DIR)/terminal.elf: %/terminal.elf: %/terminal.o $(RELEASE_DIR)/terminal_core.o $(RELEASE_DIR)/signed_input_core.o $(RELEASE_DIR)/admin_receipt_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(OPERATOR_DIR)/launch_admin.elf $(FIXTURE_DIR)/launch_admin.elf: %/launch_admin.elf: %/launch_admin.o $(RELEASE_DIR)/admin.o $(RELEASE_DIR)/admin_receipt_core.o $(RELEASE_DIR)/launch_core.o $(RELEASE_DIR)/monocypher.o $(RELEASE_DIR)/monocypher-ed25519.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(OPERATOR_DIR)/launch_boot.elf $(FIXTURE_DIR)/launch_boot.elf: %/launch_boot.elf: %/launch_boot.o $(RELEASE_DIR)/boot_core.o $(RELEASE_DIR)/launch_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(OPERATOR_DIR)/interactive.img $(FIXTURE_DIR)/interactive.img: %/interactive.img: %/terminal.elf %/launch_admin.elf %/launch_boot.elf $(RELEASE_DIR)/admin_policy.elf $(RELEASE_IMAGES) system/interactive.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/interactive.system --search-path "$(@D)" "$(RELEASE_DIR)" --board $(BOARD) --config release -o "$@" -r "$(@D)/report.txt"

interactive-image: $(OPERATOR_DIR)/interactive.img
interactive-fixture-image: $(FIXTURE_DIR)/interactive.img

interactive-smoke: interactive-image interactive-fixture-image $(BUILD_DIR)/operator_fixture
	$(PYTHON) tools/interactive_boot_test.py --qemu "$(QEMU)" --fixture "$(BUILD_DIR)/operator_fixture" --image "$(FIXTURE_DIR)/interactive.img" --operator-image "$(OPERATOR_DIR)/interactive.img" --log "$(BUILD_DIR)/interactive-boot.log"

interactive-smoke-saved: verify-artifacts $(BUILD_DIR)/operator_fixture
	$(PYTHON) tools/interactive_boot_test.py --qemu "$(QEMU)" --fixture "$(BUILD_DIR)/operator_fixture" --image artifacts/interactive-fixture.img --operator-image artifacts/interactive.img --log "$(BUILD_DIR)/saved-interactive-boot.log"

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

# Test-only fault observer/probes; normal images never depend on these ELFs.
$(ISOLATION_DIR)/isolation_%.o: tests/isolation/%.c tests/isolation/cases.h servers/terminal.c servers/policy.c $(HEADERS) Makefile | $(ISOLATION_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -c "$<" -o "$@"

$(ISOLATION_DIR)/probe%.o: tests/isolation/probe.c tests/isolation/cases.h $(HEADERS) Makefile | $(ISOLATION_DIR) check-tools
	"$(ZIG)" cc $(RELEASE_FLAGS) -DISO_PROBE=$* -c "$<" -o "$@"

$(ISOLATION_PROBES): $(ISOLATION_DIR)/%.elf: $(ISOLATION_DIR)/%.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $< -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(ISOLATION_DIR)/isolation_observer.elf: $(ISOLATION_DIR)/isolation_observer.o $(RELEASE_DIR)/terminal_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(ISOLATION_DIR)/isolation_policy.elf: $(ISOLATION_DIR)/isolation_policy.o $(RELEASE_DIR)/policy_core.o
	"$(ZIG)" cc $(RELEASE_FLAGS) $^ -L"$(RELEASE_SDK_BOARD)/lib" -Wl,-T,"$(RELEASE_SDK_BOARD)/lib/microkit.ld" -Wl,--build-id=none -lmicrokit -o "$@"

$(ISOLATION_DIR)/isolation.img: $(ISOLATION_PROBES) $(ISOLATION_DIR)/isolation_observer.elf $(ISOLATION_DIR)/isolation_policy.elf $(RELEASE_IMAGES) system/isolation.system | check-system
	"$(MICROKIT_SDK)/bin/microkit" system/isolation.system --search-path "$(ISOLATION_DIR)" "$(RELEASE_DIR)" \
	    --board $(BOARD) --config release -o "$@" -r "$(ISOLATION_DIR)/report.txt"

isolation-image: $(ISOLATION_DIR)/isolation.img

isolation-smoke: isolation-image
	$(PYTHON) tools/terminal_boot_test.py --profile release --isolation --qemu "$(QEMU)" --image "$(ISOLATION_DIR)/isolation.img" --log "$(ISOLATION_DIR)/boot.log"

isolation-smoke-saved: verify-artifacts
	$(PYTHON) tools/terminal_boot_test.py --profile release --isolation --qemu "$(QEMU)" --image artifacts/isolation.img --log "$(BUILD_DIR)/saved-isolation-boot.log"

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
