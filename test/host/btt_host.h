/*
 * btt_host.h - just enough of the console's kernel headers for the host tests
 * (test/test_*.c) to compile the kernel module's own sources on a computer. The
 * functions are declared here and defined, as controllable fakes, in each test.
 */
#ifndef BTT_HOST_H
#define BTT_HOST_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef int SceUID;
typedef unsigned int SceSize;
typedef unsigned int SceUInt;
typedef uint64_t SceUInt64;

#define KERNEL_PID                  0x10005
#define SCE_KERNEL_START_SUCCESS    0
#define SCE_KERNEL_STOP_SUCCESS     0
#define SCE_EVENT_WAITOR            0x01
#define SCE_EVENT_WAITCLEAR_PAT     0x20
#define SCE_O_RDONLY                0x0001
#define SCE_O_WRONLY                0x0002
#define SCE_O_CREAT                 0x0200
#define SCE_O_TRUNC                 0x0400

typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);
typedef int (*SceSysEventHandler)(int resume, int eventid, void *args, void *opt);
typedef int SceSSize;

SceUID ksceIoOpen(const char *path, int flags, int mode);
int    ksceIoRead(SceUID fd, void *buf, SceSize n);
int    ksceIoWrite(SceUID fd, const void *buf, SceSize n);
int    ksceIoClose(SceUID fd);
int    ksceIoMkdir(const char *path, int mode);

uint64_t ksceKernelGetSystemTimeWide(void);
uint32_t ksceKernelGetSystemTimeLow(void);
SceUID ksceKernelCreateEventFlag(const char *name, unsigned attr, unsigned pattern, void *opt);
int    ksceKernelDeleteEventFlag(SceUID ev);
int    ksceKernelSetEventFlag(SceUID ev, unsigned bits);
int    ksceKernelWaitEventFlag(SceUID ev, unsigned bits, unsigned mode, unsigned *res, SceUInt *timeout);
SceUID ksceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int prio,
                              SceSize stack, unsigned attr, int cpu, void *opt);
int    ksceKernelStartThread(SceUID th, SceSize args, void *argp);
int    ksceKernelWaitThreadEnd(SceUID th, int *res, SceUInt *timeout);
int    ksceKernelDeleteThread(SceUID th);
int    ksceKernelChangeThreadPriority(SceUID thid, int priority);
int    ksceKernelRegisterSysEventHandler(const char *name, SceSysEventHandler handler, void *args);
/* the system's checked copies between the caller's memory and the kernel's (macros in the SDK) */
int    ksceKernelMemcpyKernelToUser(void *dst, const void *src, size_t len);
int    ksceKernelMemcpyUserToKernel(void *dst, const void *src, size_t len);
SceSSize ksceKernelStrncpyUserToKernel(char *dst, const char *src, SceSize len);

#endif
