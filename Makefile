# vita-turbo-bluetooth - build and deploy helpers.
#
#   make                      build the three modules (uses your VitaSDK if $VITASDK
#                             is set, otherwise the pinned VitaSDK Docker image)
#   make install VITA_IP=...  upload the three modules to ur0:tai/ over FTP
#   make deploy  VITA_IP=...  install, then reboot the Vita so they load
#   make log     VITA_IP=...  fetch the plugin log into ./logs/
#   make config               print the lines to add to ur0:tai/config.txt
#   make test                 builds, then: the check on the sound and volume hooks' object files, the
#                             host tests of the kernel module's start-up/worker/stop logic
#                             (test/test_worker.c), of the byte-checked changes (test/test_btfix.c),
#                             of the sound filter with its channels and balance (test/test_bteq.c,
#                             and its coefficient table), of the volume mapping (test/test_btvol.c,
#                             and its tables) and of the flush timeout
#                             (test/test_btflush.c: the timeout and the completed-packets guard), of the
#                             connection quality hook (test/test_btqual.c) and of High and Highest (test/test_bthq.c), of what happens when headphones disconnect
#                             (test/test_btdisc.c), the Bluetooth name (test/test_btname.c) and the reconnecting after
#                             standby (test/test_btreconn.c), the headset battery call (test/test_btbatt.c), the low battery notifications'
#                             rules and notification block (test/test_lowbatt.c), the translations, the Settings page XML ids,
#                             and the check that the module touches nothing but the Bluetooth code
#                             and the audio sender's Bluetooth hand-offs
#   make clean
#
# install/deploy/log talk to vitacompanion (FTP on :1337, commands on :1338)
# running on the Vita. Output: build/bluetoothturbo_kernel.skprx, build/bluetoothturbo_settings.suprx, build/bluetoothturbo.suprx

BUILD   ?= build
VITA_IP ?=
# Extra CMake options for a build (see CMakeLists.txt).
CMAKE_EXTRA ?=
# Pinned so every build uses the same toolchain (VitaSDK 2026.08, GCC 15.2).
VITASDK_IMAGE ?= vitasdk/vitasdk@sha256:8227da1b596d981f4513cd9b36bda1e43857f6dae5bcf4312c1cd40167f7d9bb

MODULES := $(BUILD)/bluetoothturbo_kernel.skprx $(BUILD)/bluetoothturbo_settings.suprx $(BUILD)/bluetoothturbo.suprx
FTP      = ftp://$(VITA_IP):1337
OBJDIR   = $(BUILD)/CMakeFiles/bluetoothturbo_kernel.dir/kernel
HOOK_OBJ = $(OBJDIR)/bteq_run.c.obj $(OBJDIR)/bteq_hook.c.obj $(OBJDIR)/btvol_map.c.obj $(OBJDIR)/btvol_hook.c.obj \
           $(OBJDIR)/btqual_hook.c.obj $(OBJDIR)/btreconn_hook.c.obj
# The flush and guard hooks call nothing at all (the original routines and the command sender are indirect calls).
FLUSH_OBJ      = $(OBJDIR)/btflush_hook.c.obj
FLUSH_REQUIRED = btflush_start_hook btflush_stop_hook btflush_evt_hook
# The headset battery answers: the parser calls nothing; the hook calls the parser and the microsecond clock.
HFP_ANS_OBJ  = $(OBJDIR)/bthfp.c.obj
HFP_HOOK_OBJ = $(OBJDIR)/bthfp_hook.c.obj
HOSTCC   = cc -O2 -Wall -Wextra -Wno-unused-parameter -Itest/host

.PHONY: all build install deploy log config test leaf clean check-ip

all: build

build:
ifdef VITASDK
	cmake -S . -B $(BUILD) $(CMAKE_EXTRA) -DCMAKE_TOOLCHAIN_FILE=$(VITASDK)/share/vita.toolchain.cmake
	cmake --build $(BUILD) -j
else
	docker run --rm -v "$(CURDIR)":/src -w /src $(VITASDK_IMAGE) sh -c '\
	  cmake -S . -B $(BUILD) $(CMAKE_EXTRA) -DCMAKE_TOOLCHAIN_FILE=$$VITASDK/share/vita.toolchain.cmake && \
	  cmake --build $(BUILD) -j$$(nproc) && \
	  chown -R $(shell id -u):$(shell id -g) $(BUILD)'
endif

check-ip:
	@test -n "$(VITA_IP)" || { echo "Set VITA_IP, e.g. make $(MAKECMDGOALS) VITA_IP=192.168.1.50"; exit 1; }

install: build check-ip
	@for f in $(MODULES); do \
	  echo "upload $$f"; curl -sS -T $$f "$(FTP)/ur0:/tai/$$(basename $$f)" || exit 1; \
	done
	@echo "Installed. If this is the first install, add the plugin to config.txt (make config)."

deploy: install
	@printf 'reboot\n' | nc -w 3 $(VITA_IP) 1338 >/dev/null || true
	@echo "Rebooting the Vita - the plugin loads at boot."

log: check-ip
	@mkdir -p logs
	curl -sS "$(FTP)/ur0:/data/bluetoothturbo/bluetoothturbo_kernel.log" -o logs/bluetoothturbo_kernel.log
	@echo "Saved logs/bluetoothturbo_kernel.log"

config:
	@printf '%s\n' '*KERNEL' 'ur0:tai/bluetoothturbo_kernel.skprx' '' '*main' '# optional: only the low battery notifications' 'ur0:tai/bluetoothturbo.suprx' '' '*NPXS10015' 'ur0:tai/bluetoothturbo_settings.suprx'

# The hook bodies and the functions they call run in the audio sender's thread (the sound filter), the
# sound policy thread (the volume mapping and the reconnect hook) and the Bluetooth code's own thread (the encoder hook): their
# object files must hold no floating-point/vector instruction, no system call, and call nothing but each
# other and the microsecond clock. (The build runs the same check after the link, and for the flush and
# guard hooks too.)
leaf: build
ifdef VITASDK
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump sh tools/check_bteq_leaf.sh $(HOOK_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(FLUSH_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(FLUSH_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_answer BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(HFP_ANS_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_hook BTEQ_ALLOWED_CALLS="ksceKernelGetSystemTimeLow bthfp_answer" sh tools/check_bteq_leaf.sh $(HFP_HOOK_OBJ)
else
	docker run --rm -v "$(CURDIR)":/src -w /src $(VITASDK_IMAGE) sh -c '\
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump sh tools/check_bteq_leaf.sh $(HOOK_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(FLUSH_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(FLUSH_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_answer BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(HFP_ANS_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_hook BTEQ_ALLOWED_CALLS="ksceKernelGetSystemTimeLow bthfp_answer" sh tools/check_bteq_leaf.sh $(HFP_HOOK_OBJ)'
endif

test: leaf
	@mkdir -p $(BUILD)
	$(HOSTCC) test/test_worker.c -o $(BUILD)/test_worker
	$(BUILD)/test_worker
	$(HOSTCC) test/test_btfix.c -o $(BUILD)/test_btfix
	$(BUILD)/test_btfix
	$(HOSTCC) test/test_bteq.c -o $(BUILD)/test_bteq -lm -lpthread
	$(BUILD)/test_bteq
	$(HOSTCC) test/test_btvol.c -o $(BUILD)/test_btvol
	$(BUILD)/test_btvol
	$(HOSTCC) test/test_btflush.c -o $(BUILD)/test_btflush
	$(BUILD)/test_btflush
	$(HOSTCC) test/test_btqual.c -o $(BUILD)/test_btqual
	$(BUILD)/test_btqual
	$(HOSTCC) test/test_bthq.c -o $(BUILD)/test_bthq
	$(BUILD)/test_bthq
	$(HOSTCC) test/test_btdisc.c -o $(BUILD)/test_btdisc
	$(BUILD)/test_btdisc
	$(HOSTCC) test/test_btname.c -o $(BUILD)/test_btname
	$(BUILD)/test_btname
	$(HOSTCC) test/test_btreconn.c -o $(BUILD)/test_btreconn
	$(BUILD)/test_btreconn
	$(HOSTCC) test/test_btbatt.c -o $(BUILD)/test_btbatt
	$(BUILD)/test_btbatt
	$(HOSTCC) test/test_lowbatt.c -o $(BUILD)/test_lowbatt
	$(BUILD)/test_lowbatt
	python3 tools/gen_bteq_table.py --check
	python3 tools/gen_btvol_table.py --check
	python3 tools/gen_i18n.py --check settings_app user
	python3 tools/check_xml_ids.py
	python3 tools/check_coexistence.py

clean:
	rm -rf $(BUILD) logs
