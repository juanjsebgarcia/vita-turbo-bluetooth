#pragma once
#include <btt_host.h>

/* Only for the host tests that define BTT_HOST_TAI (test_bteq.c, test_btvol.c, test_btflush.c) or
 * BTT_HOST_INJECT (test_btfix.c): just enough of taiHEN to compile the hooks and their installers, or
 * the byte-checked changes, on a computer. */
#ifdef BTT_HOST_TAI
/* The reference is the original routine itself, so TAI_CONTINUE calls it. The hooks in this product
 * have these shapes, told apart here by the number of arguments and the type of the second or third:
 *   4 arguments, first a void *    the connection quality hook (connection, flags, bytes, length)
 *   4 arguments, third a pointer   the sound hook (address, address, buffer, length)
 *   4 arguments, third a number    the two volume hooks
 *   3 arguments, second a pointer  the event hook (context, event bytes, length)
 *   3 arguments                    the stream start and stop hooks (connection, flag, mask) */
typedef uintptr_t tai_hook_ref_t;
#define BTT_TAI_PICK(_1, _2, _3, _4, name, ...) name
#define TAI_CONTINUE(type, ref, ...) BTT_TAI_PICK(__VA_ARGS__, BTT_TAI4, BTT_TAI3)(type, ref, __VA_ARGS__)
#define BTT_TAI4(type, ref, a, b, c, d) \
	(_Generic((a), \
	          void *: (type (*)(void *, uint32_t, const void *, uint32_t))(ref), \
	          default: _Generic((c), \
	                            const void *: (type (*)(uint32_t, uint32_t, const void *, uint32_t))(ref), \
	                            void *: (type (*)(uint32_t, uint32_t, const void *, uint32_t))(ref), \
	                            default: (type (*)(uint32_t, uint32_t, uint32_t, uint32_t))(ref)))(a, b, c, d))
#define BTT_TAI3(type, ref, a, b, c) \
	(_Generic((b), \
	          const uint8_t *: (type (*)(uint32_t *, const uint8_t *, int))(ref), \
	          uint8_t *: (type (*)(uint32_t *, const uint8_t *, int))(ref), \
	          default: (type (*)(int *, int, uint32_t))(ref))(a, b, c))
SceUID taiHookFunctionImportForKernel(SceUID pid, tai_hook_ref_t *ref, const char *module,
                                      uint32_t lib_nid, uint32_t func_nid, const void *hook);
SceUID taiHookFunctionOffsetForKernel(SceUID pid, tai_hook_ref_t *ref, SceUID modid, int segidx,
                                      uint32_t offset, int thumb, const void *hook);
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref);
int ksceKernelDelayThread(SceUInt delay);
#endif

#ifdef BTT_HOST_INJECT
typedef struct {
	size_t size;
	SceUID modid;
} tai_module_info_t;
int taiGetModuleInfoForKernel(SceUID pid, const char *name, tai_module_info_t *info);
SceUID taiInjectDataForKernel(SceUID pid, SceUID modid, int segidx, uint32_t offset, const void *data, size_t size);
int taiInjectReleaseForKernel(SceUID uid);
#endif
