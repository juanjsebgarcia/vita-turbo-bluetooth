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

project(bluetoothturbo C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 99)
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
  kernel/btreconn.c
  kernel/btreconn_hook.c
  kernel/btbatt.c
  kernel/bthfp.c
  kernel/bthfp_hook.c
  kernel/bthfp_mod.c
  kernel/hpgains.c
  kernel/btflush.c
  kernel/btflush_hook.c
  kernel/bthq.c
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
target_link_options(bluetoothturbo_kernel PRIVATE -nostartfiles)

# The sound filter's hook runs inside the audio sender's thread, the two volume hooks inside the
# sound policy thread, the encoder hook and the flush and guard hooks inside the Bluetooth
# code's own threads (and the reconnect hook, with its standby handler, in the sound policy thread and the power code's context): general registers only, and no library routine (loops the compiler may not
# turn into memset/memcpy). The check reads the object files after the link
# (tools/check_bteq_leaf.sh).
set(BTT_LEAF_SOURCES kernel/bteq_run.c kernel/bteq_hook.c kernel/btvol_map.c kernel/btvol_hook.c kernel/btqual_hook.c
    kernel/btreconn_hook.c kernel/bthfp.c kernel/bthfp_hook.c)
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
# the flush and guard hooks call nothing at all but the original routines and the command sender, which
# are indirect calls: no relocation names a function
add_custom_command(TARGET bluetoothturbo_kernel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP}
          "BTEQ_REQUIRED=btflush_start_hook btflush_stop_hook btflush_evt_hook" BTEQ_ALLOWED_CALLS=none
          sh ${CMAKE_SOURCE_DIR}/tools/check_bteq_leaf.sh
          ${CMAKE_BINARY_DIR}/CMakeFiles/bluetoothturbo_kernel.dir/kernel/btflush_hook.c.obj
  COMMENT "checking the flush and guard hook object")

vita_create_self(bluetoothturbo_kernel.skprx bluetoothturbo_kernel
  CONFIG ${CMAKE_SOURCE_DIR}/bluetoothturbo_kernel.yml
  UNSAFE)
vita_create_stubs(bluetoothturbo-stubs bluetoothturbo_kernel ${CMAKE_SOURCE_DIR}/bluetoothturbo_kernel.yml
  KERNEL)

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
