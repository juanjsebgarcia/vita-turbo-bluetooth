# vita-turbo-bluetooth - build and deploy helpers.
#
#   make                      build the six modules (uses your VitaSDK if $VITASDK
#                             is set, otherwise the pinned VitaSDK Docker image)
#   make install VITA_IP=...  upload the six modules to ur0:tai/ over FTP (the AAC, aptX and LDAC modules only load if config.txt lists them)
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
#                             standby (test/test_btreconn.c), the settings that apply at once (test/test_btapply.c: the debounce,
#                             the no-effect cases, the failures), the AAC engine (test/test_btaac_pace.c: the media hook's pacing, pre-roll and trim, the engine thread's priority choice, the rings; test/test_btaac.c: its rings, the bit rate, the crash record, the
#                             self-test's verdict, and the real encoder through the engine), AAC streaming (test/test_btaac_stream.c: the choice of
#                             endpoint from a real headset's bytes, the configuration, the LATM/RTP packets against an independent reader and
#                             ffmpeg, the hooks, the fallback, the session's lifecycle, the link information; the byte checker's own examples), the headset battery call (test/test_btbatt.c), the low battery notifications'
#                             rules and notification block (test/test_lowbatt.c), the link rows on the page of one Bluetooth device (test/test_btpagerows.c: the splice, which device, the values in every language, the label-value wrapper on a fake structure), the aptX add-on (test/test_btaptx.c, and its stream in test_btaac_stream.c), the codec help page and the list-style hints (test/test_btcodechelp.c), the codec fallback notification's rules and texts (test/test_btfall.c, and the stream code's reports in test_btaac_stream.c), the translations, the Settings page XML ids,
#                             and the check that the module touches nothing but the Bluetooth code
#                             and the audio sender's Bluetooth hand-offs
#   make clean
#
# install/deploy/log talk to vitacompanion (FTP on :1337, commands on :1338)
# running on the Vita. Output: build/bluetoothturbo_kernel.skprx, build/bluetoothturbo_settings.suprx, build/bluetoothturbo.suprx,
# build/bluetoothturbo_aac.skprx, build/bluetoothturbo_aptx.skprx, build/bluetoothturbo_ldac.skprx

BUILD   ?= build
VITA_IP ?=
# Extra CMake options for a build (see CMakeLists.txt).
CMAKE_EXTRA ?=
# Pinned so every build uses the same toolchain (VitaSDK 2026.08, GCC 15.2).
VITASDK_IMAGE ?= vitasdk/vitasdk@sha256:8227da1b596d981f4513cd9b36bda1e43857f6dae5bcf4312c1cd40167f7d9bb

MODULES := $(BUILD)/bluetoothturbo_kernel.skprx $(BUILD)/bluetoothturbo_settings.suprx $(BUILD)/bluetoothturbo.suprx $(BUILD)/bluetoothturbo_aac.skprx $(BUILD)/bluetoothturbo_aptx.skprx $(BUILD)/bluetoothturbo_ldac.skprx
FTP      = ftp://$(VITA_IP):1337
OBJDIR   = $(BUILD)/CMakeFiles/bluetoothturbo_kernel.dir/kernel
HOOK_OBJ = $(OBJDIR)/bteq_run.c.obj $(OBJDIR)/bteq_hook.c.obj $(OBJDIR)/btvol_map.c.obj $(OBJDIR)/btvol_hook.c.obj \
           $(OBJDIR)/btqual_hook.c.obj $(OBJDIR)/btreconn_hook.c.obj
# The flush and guard hooks call nothing at all (the original routines and the command sender are indirect calls).
FLUSH_OBJ      = $(OBJDIR)/btflush_hook.c.obj
# The link log's routine inside the event handler: a ring store and the microsecond clock.
EVLOG_OBJ      = $(OBJDIR)/btevlog_hook.c.obj
FLUSH_REQUIRED = btflush_start_hook btflush_stop_hook btflush_evt_hook
# The headset battery answers: the parser calls nothing; the hook calls the parser and the microsecond clock.
HFP_ANS_OBJ  = $(OBJDIR)/bthfp.c.obj
HFP_HOOK_OBJ = $(OBJDIR)/bthfp_hook.c.obj
# The AAC engine's rings (in the AAC module) and the two leaves that reach them from the main module: what the audio sender's thread will
# call. They call nothing at all (the pointer call of the leaves is indirect).
AAC_OBJ       = $(BUILD)/CMakeFiles/bluetoothturbo_aac.dir/kernel/btaac_leaf.c.obj
AAC_REQUIRED  = btaac_push_pcm btaac_pop_au btaac_au_trim btaac_au_depth
LINK_OBJ      = $(OBJDIR)/btaac_link_leaf.c.obj
LINK_REQUIRED = btt_aac_push_pcm btt_aac_pop_au btt_aac_au_trim btt_aac_au_depth
# The aptX add-on: its one leaf in the main module (an indirect call: no call at all), the encoder's leaf in the add-on, and the vendored library's own objects.
APTX_LINK_OBJ = $(OBJDIR)/btaptx_link_leaf.c.obj
APTX_OBJ      = $(BUILD)/CMakeFiles/bluetoothturbo_aptx.dir/kernel/btaptx_leaf.c.obj
APTX_LIB_CLASSIC = $(addprefix $(BUILD)/CMakeFiles/aptx_lib_classic.dir/third_party/aptxenc/classic/src/,aptXbtenc.c.obj ProcessSubband.c.obj QmfConv.c.obj QuantiseDifference.c.obj)
APTX_LIB_HD      = $(addprefix $(BUILD)/CMakeFiles/aptx_lib_hd.dir/third_party/aptxenc/hd/src/,aptXHDbtenc.c.obj ProcessSubband.c.obj QmfConv.c.obj QuantiseDifference.c.obj)
APTX_CALLS_CLASSIC = AsmQmfConvI AsmQmfConvO processSubband processSubbandHL processSubbandLL quantiseDifferenceHH quantiseDifferenceHL quantiseDifferenceLH quantiseDifferenceLL
APTX_CALLS_HD      = AsmQmfConvI_HD AsmQmfConvO_HD processSubband_HD processSubband_HDHL processSubband_HDLL quantiseDifference_HDHH quantiseDifference_HDHL quantiseDifference_HDLH quantiseDifference_HDLL
# The LDAC add-on: the ring routines in the add-on, their leaves in the main module, and the library's own objects (no vector instruction: the build checks them).
# The LDAC library for the host tests: the same sources and flags as the add-on's, the add-on's allocator behind its allocation calls.
LDAC_DIR       = third_party/libldac
LDAC_HOSTFLAGS = -O2 -w -D_32BIT_FIXED_POINT -Dmalloc=btldac_malloc -Dcalloc=btldac_calloc -Dfree=btldac_free -include kernel/btldac_mem.h -I$(LDAC_DIR)/inc -I$(LDAC_DIR)/src -I$(LDAC_DIR)/abr/inc
LDAC_OBJ       = $(BUILD)/CMakeFiles/bluetoothturbo_ldac.dir/kernel/btldac_leaf.c.obj
LDAC_REQUIRED  = btldac_push_pcm btldac_pop_pkt btldac_pkt_trim btldac_pkt_depth
LDAC_LINK_OBJ  = $(OBJDIR)/btldac_link_leaf.c.obj
LDAC_LINK_REQUIRED = btt_ldac_push_pcm btt_ldac_pop_pkt btt_ldac_pkt_trim btt_ldac_pkt_depth
# The AAC stream hooks: the signalling observer and the media packet sender. They call the engine's access unit leaf and nothing else.
STREAM_OBJ      = $(OBJDIR)/btaac_hook.c.obj
STREAM_REQUIRED = btaac_disp_hook btaac_send_hook
HOSTCC   = cc -O2 -Wall -Wextra -Wno-unused-parameter -Itest/host
# The vendored encoder for the host tests (one build; the NEON build is the console's): as it is, no warnings asked for.
AAC_DIR      = third_party/aacenc
AAC_SRC     = $(wildcard $(AAC_DIR)/aacenc/src/*.c) $(AAC_DIR)/aacenc/basic_op/basicop2.c $(AAC_DIR)/aacenc/basic_op/oper_32b.c kernel/btaac_enclimit.c
AAC_HOSTCC  = cc -O2 -w -D__unused= -Ikernel/shim -I$(AAC_DIR)/aacenc/inc -I$(AAC_DIR)/aacenc/basic_op -I$(AAC_DIR)/common/include

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
	@printf '%s\n' '*KERNEL' 'ur0:tai/bluetoothturbo_kernel.skprx' '# optional, after the main module: AAC (opt in by adding this line)' 'ur0:tai/bluetoothturbo_aac.skprx' '# optional, likewise: aptX (an add-on, Apache-2.0 encoders)' 'ur0:tai/bluetoothturbo_aptx.skprx' '# optional, likewise: LDAC (an add-on, the open-source LDAC encoder, Apache-2.0)' 'ur0:tai/bluetoothturbo_ldac.skprx' '' '*main' '# optional: only the low battery and codec notifications' 'ur0:tai/bluetoothturbo.suprx' '' '*NPXS10015' 'ur0:tai/bluetoothturbo_settings.suprx'

# The hook bodies and the functions they call run in the audio sender's thread (the sound filter), the
# sound policy thread (the volume mapping and the reconnect hook) and the Bluetooth code's own thread (the encoder hook): their
# object files must hold no floating-point/vector instruction, no system call, and call nothing but each
# other and the microsecond clock. (The build runs the same check after the link, and for the flush and
# guard hooks too.)
leaf: build
ifdef VITASDK
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump sh tools/check_bteq_leaf.sh $(HOOK_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(FLUSH_REQUIRED)" BTEQ_ALLOWED_CALLS=btevlog_note_hci sh tools/check_bteq_leaf.sh $(FLUSH_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=btevlog_note_hci BTEQ_ALLOWED_CALLS=ksceKernelGetSystemTimeLow sh tools/check_bteq_leaf.sh $(EVLOG_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_answer BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(HFP_ANS_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_hook BTEQ_ALLOWED_CALLS="ksceKernelGetSystemTimeLow bthfp_answer" sh tools/check_bteq_leaf.sh $(HFP_HOOK_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(AAC_REQUIRED)" BTEQ_ALLOWED_CALLS=ksceKernelGetSystemTimeLow sh tools/check_bteq_leaf.sh $(AAC_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(LINK_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(LINK_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(STREAM_REQUIRED)" BTEQ_ALLOWED_CALLS="btt_aac_pop_au btt_aac_au_trim btt_aac_au_depth btt_aptx_encode btt_aptx_encode_hd btt_ldac_pop_pkt btt_ldac_pkt_trim btt_ldac_pkt_depth ksceKernelGetSystemTimeLow" sh tools/check_bteq_leaf.sh $(STREAM_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="btt_aptx_encode btt_aptx_encode_hd" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(APTX_LINK_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(LDAC_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(LDAC_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(LDAC_LINK_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(LDAC_LINK_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="btaptx_encode btaptx_encode_hd" BTEQ_ALLOWED_CALLS="aptxbtenc_encodestereo aptxhdbtenc_encodestereo ksceKernelGetSystemTimeLow" sh tools/check_bteq_leaf.sh $(APTX_OBJ)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=aptxbtenc_encodestereo BTEQ_ALLOWED_CALLS="$(APTX_CALLS_CLASSIC)" sh tools/check_bteq_leaf.sh $(APTX_LIB_CLASSIC)
	OBJDUMP=$(VITASDK)/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=aptxhdbtenc_encodestereo BTEQ_ALLOWED_CALLS="$(APTX_CALLS_HD)" sh tools/check_bteq_leaf.sh $(APTX_LIB_HD)
else
	docker run --rm -v "$(CURDIR)":/src -w /src $(VITASDK_IMAGE) sh -c '\
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump sh tools/check_bteq_leaf.sh $(HOOK_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(FLUSH_REQUIRED)" BTEQ_ALLOWED_CALLS=btevlog_note_hci sh tools/check_bteq_leaf.sh $(FLUSH_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=btevlog_note_hci BTEQ_ALLOWED_CALLS=ksceKernelGetSystemTimeLow sh tools/check_bteq_leaf.sh $(EVLOG_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_answer BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(HFP_ANS_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=bthfp_hook BTEQ_ALLOWED_CALLS="ksceKernelGetSystemTimeLow bthfp_answer" sh tools/check_bteq_leaf.sh $(HFP_HOOK_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(AAC_REQUIRED)" BTEQ_ALLOWED_CALLS=ksceKernelGetSystemTimeLow sh tools/check_bteq_leaf.sh $(AAC_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(LINK_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(LINK_OBJ) && \
	  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(STREAM_REQUIRED)" BTEQ_ALLOWED_CALLS="btt_aac_pop_au btt_aac_au_trim btt_aac_au_depth btt_aptx_encode btt_aptx_encode_hd btt_ldac_pop_pkt btt_ldac_pkt_trim btt_ldac_pkt_depth ksceKernelGetSystemTimeLow" sh tools/check_bteq_leaf.sh $(STREAM_OBJ) && \
		  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="btt_aptx_encode btt_aptx_encode_hd" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(APTX_LINK_OBJ) && \
		  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(LDAC_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(LDAC_OBJ) && \
		  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="$(LDAC_LINK_REQUIRED)" BTEQ_ALLOWED_CALLS=none sh tools/check_bteq_leaf.sh $(LDAC_LINK_OBJ) && \
		  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED="btaptx_encode btaptx_encode_hd" BTEQ_ALLOWED_CALLS="aptxbtenc_encodestereo aptxhdbtenc_encodestereo ksceKernelGetSystemTimeLow" sh tools/check_bteq_leaf.sh $(APTX_OBJ) && \
		  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=aptxbtenc_encodestereo BTEQ_ALLOWED_CALLS="$(APTX_CALLS_CLASSIC)" sh tools/check_bteq_leaf.sh $(APTX_LIB_CLASSIC) && \
		  OBJDUMP=$$VITASDK/bin/arm-vita-eabi-objdump BTEQ_REQUIRED=aptxhdbtenc_encodestereo BTEQ_ALLOWED_CALLS="$(APTX_CALLS_HD)" sh tools/check_bteq_leaf.sh $(APTX_LIB_HD)'
endif

test: leaf
	@mkdir -p $(BUILD)
	$(HOSTCC) test/test_worker.c -o $(BUILD)/test_worker
	$(BUILD)/test_worker
	BTT_TEST_APPLY_HOLD=1 $(BUILD)/test_worker
	BTT_TEST_SELFTEST_STUCK=1 $(BUILD)/test_worker
	$(HOSTCC) test/test_btfix.c -o $(BUILD)/test_btfix
	$(BUILD)/test_btfix
	$(HOSTCC) test/test_bteq.c -o $(BUILD)/test_bteq -lm -lpthread
	$(BUILD)/test_bteq
	$(HOSTCC) test/test_btvol.c -o $(BUILD)/test_btvol
	$(BUILD)/test_btvol
	$(HOSTCC) test/test_btflush.c -o $(BUILD)/test_btflush
	$(BUILD)/test_btflush
	$(HOSTCC) test/test_btevlog.c -o $(BUILD)/test_btevlog
	$(BUILD)/test_btevlog
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
	$(HOSTCC) test/test_btapply.c -o $(BUILD)/test_btapply
	$(BUILD)/test_btapply
	$(HOSTCC) test/test_btbatt.c -o $(BUILD)/test_btbatt
	$(BUILD)/test_btbatt
	$(HOSTCC) test/test_lowbatt.c -o $(BUILD)/test_lowbatt
	$(BUILD)/test_lowbatt
	$(HOSTCC) test/test_btcodecs.c -o $(BUILD)/test_btcodecs
	$(BUILD)/test_btcodecs
	$(HOSTCC) test/test_btpagerows.c -o $(BUILD)/test_btpagerows
	$(BUILD)/test_btpagerows
	$(HOSTCC) test/test_btcodechelp.c -o $(BUILD)/test_btcodechelp
	$(BUILD)/test_btcodechelp
	$(HOSTCC) test/test_btfall.c -o $(BUILD)/test_btfall
	$(BUILD)/test_btfall
	$(HOSTCC) -c test/test_btaac.c -o $(BUILD)/test_btaac.o
	$(AAC_HOSTCC) $(BUILD)/test_btaac.o $(AAC_SRC) -o $(BUILD)/test_btaac -lpthread
	$(BUILD)/test_btaac
	$(HOSTCC) test/test_btaac_link.c -o $(BUILD)/test_btaac_link
	$(BUILD)/test_btaac_link
	$(HOSTCC) test/test_btaac_pace.c -o $(BUILD)/test_btaac_pace
	$(BUILD)/test_btaac_pace
	$(HOSTCC) -Wno-deprecated-non-prototype -c test/test_btaac_stream.c -o $(BUILD)/test_btaac_stream.o
	$(AAC_HOSTCC) $(BUILD)/test_btaac_stream.o $(AAC_SRC) -o $(BUILD)/test_btaac_stream -lpthread
	$(BUILD)/test_btaac_stream
	$(AAC_HOSTCC) -Wall -Wno-unused-function test/test_btaac_enclimit.c $(AAC_SRC) -o $(BUILD)/test_btaac_enclimit -lm
	$(BUILD)/test_btaac_enclimit
	mkdir -p $(BUILD)/aptxenc_host/classic $(BUILD)/aptxenc_host/hd
	for v in classic hd; do for f in third_party/aptxenc/$$v/src/*.c; do cc -O2 -w -Ithird_party/aptxenc/$$v/src -Ithird_party/aptxenc/$$v/include -c $$f -o $(BUILD)/aptxenc_host/$$v/$$(basename $$f .c).o || exit 1; done; done
	$(HOSTCC) test/test_btaptx.c $(BUILD)/aptxenc_host/*/*.o -o $(BUILD)/test_btaptx -lm
	$(BUILD)/test_btaptx
	$(HOSTCC) -DBAD_HASH_BUILD -DBTAPTX_ST_HASH=0x1ull test/test_btaptx.c $(BUILD)/aptxenc_host/*/*.o -o $(BUILD)/test_btaptx_bad -lm
	$(BUILD)/test_btaptx_bad
	$(HOSTCC) -DBAD_HD_HASH_BUILD -DBTAPTX_ST_HASH_HD=0x1ull test/test_btaptx.c $(BUILD)/aptxenc_host/*/*.o -o $(BUILD)/test_btaptx_bad_hd -lm
	$(BUILD)/test_btaptx_bad_hd
	mkdir -p $(BUILD)/ldac_host
	for f in $(LDAC_DIR)/src/ldacBT $(LDAC_DIR)/src/ldaclib $(LDAC_DIR)/abr/src/ldacBT_abr; do cc $(LDAC_HOSTFLAGS) -c $$f.c -o $(BUILD)/ldac_host/$$(basename $$f).o || exit 1; done
	$(HOSTCC) -I$(LDAC_DIR)/inc -I$(LDAC_DIR)/abr/inc -Ikernel test/test_btldac.c $(BUILD)/ldac_host/*.o -o $(BUILD)/test_btldac -lpthread -lm
	$(BUILD)/test_btldac
	$(HOSTCC) -DBAD_HASH_BUILD -DBTLDAC_ST_HASH_330=0x1ull -I$(LDAC_DIR)/inc -I$(LDAC_DIR)/abr/inc -Ikernel test/test_btldac.c $(BUILD)/ldac_host/*.o -o $(BUILD)/test_btldac_bad -lpthread -lm
	$(BUILD)/test_btldac_bad
	python3 tools/check_aac_nid.py $(BUILD)/bluetoothturbo_aac-stubs.yml kernel/btaac_api.h
	python3 tools/check_aac_nid.py $(BUILD)/bluetoothturbo_aptx-stubs.yml kernel/btaptx_api.h btaptxGetApi BTAPTX_NID_GET_API
	python3 tools/check_aac_nid.py $(BUILD)/bluetoothturbo_ldac-stubs.yml kernel/btldac_api.h btldacGetApi BTLDAC_NID_GET_API
	python3 tools/gen_bteq_table.py --check
	python3 tools/gen_btvol_table.py --check
	python3 tools/gen_i18n.py --check settings_app user kernel
	python3 tools/check_xml_ids.py
	python3 tools/check_coexistence.py

clean:
	rm -rf $(BUILD) logs
