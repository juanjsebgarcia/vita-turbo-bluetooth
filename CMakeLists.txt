# CMake build for Bluetooth Turbo, the PS Vita Bluetooth audio plugin (firmware 3.60-3.74, taiHEN/HENkaku).
#
# Two artifacts:
#   bluetoothturbo_kernel.skprx   - kernel module: settings, the Bluetooth audio changes and the
#                                   user-callable SceBluetoothTurbo syscalls.
#   bluetoothturbo_settings.suprx - Settings app module (NPXS10015): the Bluetooth Turbo
#                                   page, Settings -> Devices, below Bluetooth Devices.
#   bluetoothturbo.suprx          - Home screen module (*main, SceShell): the low battery
#                                   notifications of the headset.
#
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
#   cmake --build build
# or just `make` (pinned VitaSDK Docker image when $VITASDK isn't set).

cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE PATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "Set VITASDK or pass -DCMAKE_TOOLCHAIN_FILE")
  endif()
endif()

project(bluetoothturbo C ASM)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 99)

# --------------------------------------------------------------------------
# The AAC encoder (third_party/aacenc), built twice into the kernel module
# --------------------------------------------------------------------------
# Defined before the product's own warning flags below: third-party code is built as it is, none of it is changed.
# Two builds of the same sources, each merged into one object file with every symbol given its own prefix (tools/prefix_symbols.sh),
# so both live in one module and the engine (kernel/btaac.c) picks one at run time:
#   aac_c  plain C, -mgeneral-regs-only: not one floating-point or vector instruction (tools/check_novec.sh checks it);
#   aac_n  ARM mode (the encoder's inline assembly does not build as Thumb) with its integer NEON assembly. It runs only in the
#         engine's own thread, and only after the start-up self-test has found it gives the same bytes as the C build.
set(BTAAC_ENC ${CMAKE_SOURCE_DIR}/third_party/aacenc)
file(GLOB BTAAC_ENC_SRC ${BTAAC_ENC}/aacenc/src/*.c)
list(APPEND BTAAC_ENC_SRC ${BTAAC_ENC}/aacenc/basic_op/basicop2.c ${BTAAC_ENC}/aacenc/basic_op/oper_32b.c
     ${CMAKE_SOURCE_DIR}/kernel/btaac_enclimit.c)   # ours: holds the encoder's bit reservoir to a size, built with it (it reads the encoder's own record)
set(BTAAC_ENC_ASM ${BTAAC_ENC}/aacenc/src/asm/ARMV5E/AutoCorrelation_v5.s
                 ${BTAAC_ENC}/aacenc/src/asm/ARMV5E/CalcWindowEnergy_v5.s
                 ${BTAAC_ENC}/aacenc/src/asm/ARMV5E/band_nrg_v5.s
                 ${BTAAC_ENC}/aacenc/src/asm/ARMV7/PrePostMDCT_v7.s
                 ${BTAAC_ENC}/aacenc/src/asm/ARMV7/R4R8First_v7.s
                 ${BTAAC_ENC}/aacenc/src/asm/ARMV7/Radix4FFT_v7.s)
# the shim's assert.h first: the encoder includes that header by name
set(BTAAC_ENC_INC ${CMAKE_SOURCE_DIR}/kernel/shim ${BTAAC_ENC}/aacenc/inc ${BTAAC_ENC}/aacenc/basic_op ${BTAAC_ENC}/common/include)

add_library(aac_c OBJECT ${BTAAC_ENC_SRC})
target_include_directories(aac_c BEFORE PRIVATE ${BTAAC_ENC_INC})
target_compile_definitions(aac_c PRIVATE __unused=)
target_compile_options(aac_c PRIVATE -O2 -mgeneral-regs-only)

add_library(aac_n OBJECT ${BTAAC_ENC_SRC} ${BTAAC_ENC_ASM})
target_include_directories(aac_n BEFORE PRIVATE ${BTAAC_ENC_INC})
target_compile_definitions(aac_n PRIVATE __unused= ARMV5E ARMV7Neon ARM_INASM ARMV5_INASM ARMV6_INASM)
target_compile_options(aac_n PRIVATE -O2 -marm -mfpu=neon)

foreach(v c n)
  set(BTAAC_OUT_${v} ${CMAKE_BINARY_DIR}/aac${v}_prefixed.o)
  add_custom_command(OUTPUT ${BTAAC_OUT_${v}}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/prefix_symbols.sh aac${v}_ ${BTAAC_OUT_${v}} ${VITASDK}/bin/arm-vita-eabi-ld ${CMAKE_NM}
            ${CMAKE_OBJCOPY} $<TARGET_OBJECTS:aac_${v}>
    DEPENDS aac_${v} ${CMAKE_SOURCE_DIR}/tools/prefix_symbols.sh
    COMMAND_EXPAND_LISTS
    COMMENT "merging the ${v} build of the AAC encoder and prefixing its symbols")
  set_source_files_properties(${BTAAC_OUT_${v}} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endforeach()
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/aac_c_novec.stamp
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP} sh ${CMAKE_SOURCE_DIR}/tools/check_novec.sh ${BTAAC_OUT_c}
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/aac_c_novec.stamp
  DEPENDS ${BTAAC_OUT_c} ${CMAKE_SOURCE_DIR}/tools/check_novec.sh
  COMMENT "checking the plain C build of the AAC encoder holds no vector instruction")

# --------------------------------------------------------------------------
# The aptX encoders (third_party/aptxenc, Apache-2.0), built into the aptX add-on
# --------------------------------------------------------------------------
# Two objects' worth, classic and HD, as they are (one line of each AptxParameters.h is changed, marked there: third_party/aptxenc/README.md): plain integer code,
# -mgeneral-regs-only, no memset/memcpy calls, no allocation (the caller supplies the state). Defined before the product's own warning flags.
# ARM mode (-marm): the encoder runs inline in the audio sender's thread, and ARM mode needs about 9 % fewer instructions than Thumb for this code (static count of the objects:
# classic 2176 -> 1976, HD 2140 -> 1953; the text grows by about 1.4 KB each). -O3 was tried and is worse (more instructions and bytes), and -mtune=cortex-a9 is the compiler's default here.
# The encoded bytes must not change: the add-on's start-up self-test compares the encoders' output with a hash and refuses to run on a mismatch (the host test, built with -O0, -O2 and -O3 under the undefined-behaviour checker, gives the same hashes).
foreach(v classic hd)
  file(GLOB BTT_APTX_SRC_${v} ${CMAKE_SOURCE_DIR}/third_party/aptxenc/${v}/src/*.c)
  add_library(aptx_lib_${v} OBJECT ${BTT_APTX_SRC_${v}})
  target_include_directories(aptx_lib_${v} BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/third_party/aptxenc/${v}/src ${CMAKE_SOURCE_DIR}/third_party/aptxenc/${v}/include)
  target_compile_options(aptx_lib_${v} PRIVATE -O2 -marm -mgeneral-regs-only -fno-tree-vectorize -fno-tree-loop-distribute-patterns)
endforeach()

# --------------------------------------------------------------------------
# The LDAC encoder (third_party/libldac, Apache-2.0), built into the LDAC add-on
# --------------------------------------------------------------------------
# Sony's open-source LDAC encoder (see THIRD_PARTY_NOTICES.md), as it is, in its integer (fixed point) build, with our own compile lines (its build files are not used). No vector instruction
# (-fno-tree-vectorize; the build checks the object: -mgeneral-regs-only makes this compiler stop on the library's float input path), its allocation calls renamed to the
# add-on's static block (kernel/btldac_mem.c). Defined before the product's own warning flags.
add_library(ldac_lib OBJECT
  ${CMAKE_SOURCE_DIR}/third_party/libldac/src/ldacBT.c
  ${CMAKE_SOURCE_DIR}/third_party/libldac/src/ldaclib.c
  ${CMAKE_SOURCE_DIR}/third_party/libldac/abr/src/ldacBT_abr.c)
target_include_directories(ldac_lib BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/third_party/libldac/inc ${CMAKE_SOURCE_DIR}/third_party/libldac/src ${CMAKE_SOURCE_DIR}/third_party/libldac/abr/inc)
target_compile_definitions(ldac_lib PRIVATE _32BIT_FIXED_POINT malloc=btldac_malloc calloc=btldac_calloc free=btldac_free)
target_compile_options(ldac_lib PRIVATE -O2 -fno-tree-vectorize -fno-tree-loop-distribute-patterns -w -include ${CMAKE_SOURCE_DIR}/kernel/btldac_mem.h)

add_compile_options(-Wall -Wextra -Wno-unused-parameter -O2)

# --------------------------------------------------------------------------
# Kernel module: bluetoothturbo_kernel.skprx
# --------------------------------------------------------------------------
add_executable(bluetoothturbo_kernel
  kernel/main.c
  kernel/klog.c
  kernel/fwlog.c
  kernel/shell.c
  kernel/btfix.c
  kernel/bteq.c
  kernel/bteq_plan.c
  kernel/bteq_run.c
  kernel/bteq_hook.c
  kernel/btvol.c
  kernel/btvol_map.c
  kernel/btvol_hook.c
  kernel/btqual.c
  kernel/btqual_hook.c
  kernel/btdisc.c
  kernel/btname.c
  kernel/btcall.c
  kernel/btreconn.c
  kernel/btreconn_hook.c
  kernel/btbatt.c
  kernel/bthfp.c
  kernel/bthfp_hook.c
  kernel/bthfp_mod.c
  kernel/hpgains.c
  kernel/btflush.c
  kernel/btflush_hook.c
  kernel/btevlog.c
  kernel/btevlog_hook.c
  kernel/bthq.c
  kernel/btaac_link.c
  kernel/btaac_link_leaf.c
  kernel/btaptx_link.c
  kernel/btaptx_link_leaf.c
  kernel/btldac_link.c
  kernel/btldac_link_leaf.c
  kernel/btaac_neg.c
  kernel/btaac_hook.c
  kernel/btaac_stream.c
  kernel/btlink.c
  kernel/btfall.c
  kernel/btcodecs.c
  kernel/btapply.c
)

target_link_libraries(bluetoothturbo_kernel
  taihenForKernel_stub
  taihenModuleUtils_stub        # module_get_export_func / module_get_offset
  SceSysclibForDriver_stub      # memcpy/memset/snprintf/strnlen
  SceThreadmgrForDriver_stub
  SceIofilemgrForDriver_stub    # settings file and log
  SceSysmemForDriver_stub       # the checked copies between the Settings page's memory and the kernel's (the name)
  SceKernelSuspendForDriver_stub  # the standby handler (Reconnect After Standby)
)
# (the headset battery code, kernel/bthfp_mod.c, uses only these: taiHEN for the hook and the injects, the module offsets, the thread
# priority and the clock)
# The module's data is put one 64 KiB boundary above its text (0x81020000), as the AAC module's and every module over 64 KB of text have it: the text of this module ends within
# a few hundred bytes of 0x81010000, where the linker would otherwise put the data, and the converter (vita-elf-create) fails on a module that close to the boundary.
target_link_options(bluetoothturbo_kernel PRIVATE -nostartfiles -Wl,-Tdata=0x81020000)

# The sound filter's hook runs inside the audio sender's thread, the two volume hooks inside the
# sound policy thread, the encoder hook and the flush and guard hooks inside the Bluetooth
# code's own threads (and the reconnect hook, with its standby handler, in the sound policy thread and the power code's context): general registers only, and no library routine (loops the compiler may not
# turn into memset/memcpy). The check reads the object files after the link
# (tools/check_bteq_leaf.sh).
set(BTT_LEAF_SOURCES kernel/btevlog_hook.c kernel/bteq_run.c kernel/bteq_hook.c kernel/btvol_map.c kernel/btvol_hook.c kernel/btqual_hook.c
    kernel/btreconn_hook.c kernel/bthfp.c kernel/bthfp_hook.c kernel/btaac_link_leaf.c kernel/btaac_leaf.c kernel/btaac_hook.c kernel/btaptx_link_leaf.c kernel/btaptx_leaf.c kernel/btldac_link_leaf.c kernel/btldac_leaf.c)
set(BTT_LEAF_FLAGS "-mgeneral-regs-only -fno-tree-vectorize -fno-tree-loop-distribute-patterns")
set_source_files_properties(${BTT_LEAF_SOURCES} PROPERTIES COMPILE_FLAGS "${BTT_LEAF_FLAGS}")
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/bteq_run.c.obj
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/bteq_hook.c.obj
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btvol_map.c.obj
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btvol_hook.c.obj
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btqual_hook.c.obj
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btreconn_hook.c.obj
  COMMENT "checking the Bluetooth sound, volume, connection quality and reconnect hook objects")
set_source_files_properties(kernel/btflush_hook.c PROPERTIES COMPILE_FLAGS "${BTT_LEAF_FLAGS}")
# the headset battery answers: the parser calls nothing at all; the hook calls the parser and the microsecond clock only (the
# console's reply routine and the original are indirect calls: no relocation names them)
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=bthfp_answer" BTEQ_ALLOWED_CALLS=none
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/bthfp.c.obj
  COMMENT "checking the headset answers object")
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=bthfp_hook" "BTEQ_ALLOWED_CALLS=ksceKernelGetSystemTimeLow bthfp_answer"
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/bthfp_hook.c.obj
  COMMENT "checking the headset line hook object")
# the flush and guard hooks call nothing at all but the original routines, the command sender (indirect calls: no relocation names a function) and the link log's routine
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btflush_start_hook btflush_stop_hook btflush_evt_hook" BTEQ_ALLOWED_CALLS=btevlog_note_hci
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btflush_hook.c.obj
  COMMENT "checking the flush and guard hook object")
# the link log's routine inside the event handler: it stores in a ring and reads the microsecond clock, nothing else
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btevlog_note_hci" "BTEQ_ALLOWED_CALLS=ksceKernelGetSystemTimeLow"
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btevlog_hook.c.obj
  COMMENT "checking the link log hook object")
# the AAC module's two leaves in this module: what the audio sender's hooks will call, one load of the table's pointer and one indirect call
# (no relocation names a function: the check sees no call at all)
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btt_aac_push_pcm btt_aac_pop_au btt_aac_au_trim btt_aac_au_depth" BTEQ_ALLOWED_CALLS=none
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btaac_link_leaf.c.obj
  COMMENT "checking the AAC module's leaves in the main module")
# the aptX add-on's one leaf in this module: one load of the table's pointer and one indirect call
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btt_aptx_encode btt_aptx_encode_hd" BTEQ_ALLOWED_CALLS=none
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btaptx_link_leaf.c.obj
  COMMENT "checking the aptX module's leaf in the main module")
# the LDAC add-on's ring routines in this module: one load of the table's pointer and one indirect call each
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btt_ldac_push_pcm btt_ldac_pop_pkt btt_ldac_pkt_trim btt_ldac_pkt_depth" BTEQ_ALLOWED_CALLS=none
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btldac_link_leaf.c.obj
  COMMENT "checking the LDAC module's leaves in the main module")
# the AAC stream hooks (the signalling observer and the media packet sender): they call the engine's access unit leaf (an indirect call) and nothing
# else; the packet builder is plain integer code (a division would be a library call: the check would find it)
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btaac_disp_hook btaac_send_hook" "BTEQ_ALLOWED_CALLS=btt_aac_pop_au btt_aac_au_trim btt_aac_au_depth btt_aptx_encode btt_aptx_encode_hd btt_ldac_pop_pkt btt_ldac_pkt_trim btt_ldac_pkt_depth ksceKernelGetSystemTimeLow"
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btaac_hook.c.obj
  COMMENT "checking the AAC stream hook object")

vita_create_self(bluetoothturbo_kernel.skprx bluetoothturbo_kernel
  CONFIG ${CMAKE_SOURCE_DIR}/bluetoothturbo_kernel.yml
  UNSAFE)
vita_create_stubs(bluetoothturbo-stubs bluetoothturbo_kernel ${CMAKE_SOURCE_DIR}/bluetoothturbo_kernel.yml
  KERNEL)

# --------------------------------------------------------------------------
# AAC module: bluetoothturbo_aac.skprx (optional: installing it is the opt-in)
# --------------------------------------------------------------------------
# The engine and the encoder, in a module of their own so the main one stays as small as it was without AAC. One export, btaacGetApi, which
# the main module looks up (kernel/btaac_api.h). Listed under *KERNEL after the main module; neither needs the other to load.
set(BTT_AAC_YML ${CMAKE_SOURCE_DIR}/bluetoothturbo_aac.yml)
add_executable(bluetoothturbo_aac
  kernel/btaac_main.c
  kernel/btaac_log.c
  kernel/btaac.c
  kernel/btaac_leaf.c
  ${BTAAC_OUT_c} ${BTAAC_OUT_n} ${CMAKE_BINARY_DIR}/aac_c_novec.stamp
)
target_link_libraries(bluetoothturbo_aac
  SceSysclibForDriver_stub      # memcpy/memset/snprintf and the divide helpers
  SceThreadmgrForDriver_stub
  SceIofilemgrForDriver_stub    # the crash record and the log
  SceSysmemForDriver_stub       # the encoder's memory block
)
target_link_options(bluetoothturbo_aac PRIVATE -nostartfiles)
# the engine's rings (what the audio sender's thread will call through the main module) call nothing at all; the rest of the engine's code
# runs in its own thread: general registers only (the NEON build's instructions are in the encoder's own object, not in ours)
set(BTT_AAC_OBJ ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_aac.dir/kernel)
set_source_files_properties(kernel/btaac_leaf.c PROPERTIES COMPILE_FLAGS "${BTT_LEAF_FLAGS}")
set_source_files_properties(kernel/btaac.c kernel/btaac_main.c kernel/btaac_log.c PROPERTIES COMPILE_FLAGS "-mgeneral-regs-only")
add_custom_command(TARGET bluetoothturbo_aac POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btaac_push_pcm btaac_pop_au btaac_au_trim btaac_au_depth" BTEQ_ALLOWED_CALLS=ksceKernelGetSystemTimeLow
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh ${BTT_AAC_OBJ}/btaac_leaf.c.obj
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          sh ${CMAKE_SOURCE_DIR}/tools/check_novec.sh
          ${BTT_AAC_OBJ}/btaac.c.obj ${BTT_AAC_OBJ}/btaac_main.c.obj ${BTT_AAC_OBJ}/btaac_log.c.obj
  COMMENT "checking the AAC engine's ring object (a leaf) and its thread's code (no vector instruction)")
vita_create_self(bluetoothturbo_aac.skprx bluetoothturbo_aac
  CONFIG ${BTT_AAC_YML}
  UNSAFE)
# the stubs are generated only to read the export's number from them: tools/check_aac_nid.py compares it with kernel/btaac_api.h
vita_create_stubs(bluetoothturbo_aac-stubs bluetoothturbo_aac ${BTT_AAC_YML} KERNEL)

# --------------------------------------------------------------------------
# aptX module: bluetoothturbo_aptx.skprx (optional: installing it is the opt-in)
# --------------------------------------------------------------------------
# The aptX encoders (Apache-2.0, third_party/aptxenc) in a module of their own, like the AAC one: the main module loads nothing it does not find, and installing this
# file is the opt-in. No thread: the encoder runs inline in the audio sender's thread (kernel/btaptx_leaf.c). One export, btaptxGetApi (kernel/btaptx_api.h).
set(BTT_APTX_YML ${CMAKE_SOURCE_DIR}/bluetoothturbo_aptx.yml)
add_executable(bluetoothturbo_aptx
  kernel/btaptx_main.c
  kernel/btaptx_log.c
  kernel/btaptx.c
  kernel/btaptx_leaf.c
  $<TARGET_OBJECTS:aptx_lib_classic>
  $<TARGET_OBJECTS:aptx_lib_hd>
)
target_link_libraries(bluetoothturbo_aptx
  SceSysclibForDriver_stub      # memset/snprintf
  SceThreadmgrForDriver_stub    # the mutex of the log, the microsecond clock
  SceIofilemgrForDriver_stub    # the crash record and the log
)
target_link_options(bluetoothturbo_aptx PRIVATE -nostartfiles)
set(BTT_APTX_OBJ ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_aptx.dir/kernel)
set_source_files_properties(kernel/btaptx_leaf.c PROPERTIES COMPILE_FLAGS "${BTT_LEAF_FLAGS}")
set_source_files_properties(kernel/btaptx.c kernel/btaptx_main.c kernel/btaptx_log.c PROPERTIES COMPILE_FLAGS "-mgeneral-regs-only")
# the encoders run in the audio sender's thread: the leaf calls only the encoders' two routines, those call only each other (the library's own routines, listed
# here), and no object holds a system call, a vector or floating-point instruction, or a call to anything else (not the library's start-up either)
set(BTT_APTX_CALLS_classic "AsmQmfConvI AsmQmfConvO processSubband processSubbandHL processSubbandLL quantiseDifferenceHH quantiseDifferenceHL quantiseDifferenceLH quantiseDifferenceLL")
set(BTT_APTX_CALLS_hd "AsmQmfConvI_HD AsmQmfConvO_HD processSubband_HD processSubband_HDHL processSubband_HDLL quantiseDifference_HDHH quantiseDifference_HDHL quantiseDifference_HDLH quantiseDifference_HDLL")
add_custom_command(TARGET bluetoothturbo_aptx POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btaptx_encode btaptx_encode_hd" "BTEQ_ALLOWED_CALLS=aptxbtenc_encodestereo aptxhdbtenc_encodestereo ksceKernelGetSystemTimeLow"
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh ${BTT_APTX_OBJ}/btaptx_leaf.c.obj
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=aptxbtenc_encodestereo" "BTEQ_ALLOWED_CALLS=${BTT_APTX_CALLS_classic}"
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh $<TARGET_OBJECTS:aptx_lib_classic>
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=aptxhdbtenc_encodestereo" "BTEQ_ALLOWED_CALLS=${BTT_APTX_CALLS_hd}"
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh $<TARGET_OBJECTS:aptx_lib_hd>
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          sh ${CMAKE_SOURCE_DIR}/tools/check_novec.sh ${BTT_APTX_OBJ}/btaptx.c.obj ${BTT_APTX_OBJ}/btaptx_main.c.obj ${BTT_APTX_OBJ}/btaptx_log.c.obj
  COMMAND_EXPAND_LISTS
  COMMENT "checking the aptX encoders' objects (a leaf: no system call, no vector instruction, only the library's own calls)")
vita_create_self(bluetoothturbo_aptx.skprx bluetoothturbo_aptx
  CONFIG ${BTT_APTX_YML}
  UNSAFE)
vita_create_stubs(bluetoothturbo_aptx-stubs bluetoothturbo_aptx ${BTT_APTX_YML} KERNEL)

# --------------------------------------------------------------------------
# LDAC module: bluetoothturbo_ldac.skprx (optional: installing it is the opt-in)
# --------------------------------------------------------------------------
# Sony's encoder (Apache-2.0, third_party/libldac) and an engine thread in a module of their own, like the AAC one. One export, btldacGetApi (kernel/btldac_api.h).
set(BTT_LDAC_YML ${CMAKE_SOURCE_DIR}/bluetoothturbo_ldac.yml)
add_executable(bluetoothturbo_ldac
  kernel/btldac_main.c
  kernel/btldac_log.c
  kernel/btldac.c
  kernel/btldac_enc.c
  kernel/btldac_mem.c
  kernel/btldac_leaf.c
  $<TARGET_OBJECTS:ldac_lib>
)
target_include_directories(bluetoothturbo_ldac PRIVATE ${CMAKE_SOURCE_DIR}/third_party/libldac/inc ${CMAKE_SOURCE_DIR}/third_party/libldac/abr/inc)
target_link_libraries(bluetoothturbo_ldac
  SceSysclibForDriver_stub      # memcpy/memset/snprintf and the divide helpers
  SceThreadmgrForDriver_stub
  SceIofilemgrForDriver_stub    # the crash record and the log
)
target_link_options(bluetoothturbo_ldac PRIVATE -nostartfiles)
set(BTT_LDAC_OBJ ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_ldac.dir/kernel)
set_source_files_properties(kernel/btldac_leaf.c PROPERTIES COMPILE_FLAGS "${BTT_LEAF_FLAGS}")
set_source_files_properties(kernel/btldac.c kernel/btldac_main.c kernel/btldac_log.c kernel/btldac_enc.c kernel/btldac_mem.c PROPERTIES COMPILE_FLAGS "-mgeneral-regs-only")
# the rings (what the audio sender's thread calls through the main module) call nothing at all; the encoder runs in the engine's thread: no vector or floating-point
# instruction in any object of the module (the library's too: it is the fixed point build)
add_custom_command(TARGET bluetoothturbo_ldac POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btldac_push_pcm btldac_pop_pkt btldac_pkt_trim btldac_pkt_depth" BTEQ_ALLOWED_CALLS=none
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh ${BTT_LDAC_OBJ}/btldac_leaf.c.obj
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          sh ${CMAKE_SOURCE_DIR}/tools/check_novec.sh
          ${BTT_LDAC_OBJ}/btldac.c.obj ${BTT_LDAC_OBJ}/btldac_main.c.obj ${BTT_LDAC_OBJ}/btldac_log.c.obj ${BTT_LDAC_OBJ}/btldac_enc.c.obj ${BTT_LDAC_OBJ}/btldac_mem.c.obj $<TARGET_OBJECTS:ldac_lib>
  COMMAND_EXPAND_LISTS
  COMMENT "checking the LDAC module (the ring object is a leaf; no vector instruction anywhere)")
vita_create_self(bluetoothturbo_ldac.skprx bluetoothturbo_ldac
  CONFIG ${BTT_LDAC_YML}
  UNSAFE)
vita_create_stubs(bluetoothturbo_ldac-stubs bluetoothturbo_ldac ${BTT_LDAC_YML} KERNEL)

# --------------------------------------------------------------------------
# Settings module: bluetoothturbo_settings.suprx (*NPXS10015)
# --------------------------------------------------------------------------
# System libraries are weak imports: a strong import of a library that isn't
# loaded in the process makes taiHEN drop the whole module silently (see
# Wi-Fi Turbo's CMakeLists). taihen and the product's own syscall library stay strong.
add_executable(bluetoothturbo_settings
  settings_app/main.c
  settings_app/bluetoothturbo_settings.c
  settings_app/bluetoothturbo_i18n.c
)
add_dependencies(bluetoothturbo_settings bluetoothturbo-stubs)

target_link_libraries(bluetoothturbo_settings
  taihen_stub
  ${CMAKE_BINARY_DIR}/libbluetoothturbo_kernel_stub.a
  SceLibKernel_stub_weak
  SceSysmem_stub_weak           # the page buffer (sized to the page received)
  SceRegistryMgr_stub_weak      # system language
  SceAppUtil_stub_weak          # system language fallback
)
target_link_options(bluetoothturbo_settings PRIVATE -nostartfiles -Wl,-Tdata=0x81100000)
set_source_files_properties(settings_app/bluetoothturbo_settings.c settings_app/bluetoothturbo_i18n.c
  PROPERTIES COMPILE_OPTIONS -Os)

vita_create_self(bluetoothturbo_settings.suprx bluetoothturbo_settings
  CONFIG ${CMAKE_SOURCE_DIR}/bluetoothturbo_settings.yml
  UNSAFE)

# --------------------------------------------------------------------------
# Home screen module: bluetoothturbo.suprx (*main)
# --------------------------------------------------------------------------
# As small as it can be: SceShell is short of memory. Every library is a weak import (a strong import of a library that is not
# loaded makes taiHEN drop the whole module silently), the product's own syscall library too: with the kernel module missing the
# module loads and does nothing. No taiHEN, no log.
add_executable(bluetoothturbo
  user/main.c
  user/lowbatt.c
  user/notice.c
)
add_dependencies(bluetoothturbo bluetoothturbo-stubs)

target_link_libraries(bluetoothturbo
  ${CMAKE_BINARY_DIR}/libbluetoothturbo_kernel_stub_weak.a
  SceLibKernel_stub_weak
  SceKernelThreadMgr_stub_weak
  SceIofilemgr_stub_weak        # the notice's guard byte
  SceSysmodule_stub_weak        # the notification library, loaded at the first notice
  SceNotificationUtil_stub_weak # its send call
  SceRegistryMgr_stub_weak      # system language
)
target_link_options(bluetoothturbo PRIVATE -nostartfiles -Wl,-Tdata=0x81100000)
set_source_files_properties(user/main.c user/lowbatt.c user/notice.c PROPERTIES COMPILE_OPTIONS -Os)

vita_create_self(bluetoothturbo.suprx bluetoothturbo
  CONFIG ${CMAKE_SOURCE_DIR}/bluetoothturbo.yml
  UNSAFE)
