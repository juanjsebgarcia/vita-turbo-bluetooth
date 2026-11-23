/*
 * test_btaac_stream.c - host test of AAC streaming (kernel/btaac_sep.h, btaac_latm.h, btaac_gate.h, btaac_hook.c, btaac_hook.h, btaac_stream.c,
 * btlink.c, btlink_core.h), compiled as they are. The real engine and the vendored encoder run behind the hooks (a POSIX thread for the engine's
 * thread, as in test/test_btaac.c); the Bluetooth code is a recorder: its connection is memory in the layout the hooks read, its routines are fakes
 * that note what they were given.
 *
 *   make test      (cc -Itest/host test/test_btaac_stream.c + the vendored encoder, then run)
 *
 * What is proved:
 *   - the choice of endpoint from the capability bytes the reference headset sent (a recording: a vendor codec, AAC, SBC, a
 *     vendor codec) and from an SBC-only sink; every refusal (another sampling rate, mono, an unusable object type, a truncated or malformed element,
 *     an endpoint in use, a source, another media type); the first free one wins; the configuration bytes, byte for byte, with the sink's highest
 *     bit rate holding ours down;
 *   - the packet: the AudioMuxElement against an independent reader of the standard (every field of the StreamMuxConfig, the length coding at 254, 255,
 *     256 and 510 bytes, the zero padding), the RTP header, the cutting of an element to the link's packet limit (every length and limit: pieces in
 *     order, none over the limit, the marker on the last only, reassembly exact), and, with ffmpeg on the machine, a stream of real access units
 *     decoded as the standard says (LOAS framing of the same elements, whole and reassembled from pieces);
 *   - the hooks: the signalling hook passes every argument and the answer through, reads DISCOVER, the capability replies and the answer to SET_CONFIGURATION
 *     (fragments, unreadable pages, other connections and a switched-off module ignored), the rewrite of the configuration after the original ran
 *     (only for the initiator's record in state 4 with the record's own buffer), the fallback on a reject (the SBC configuration sent again, byte for byte the
 *     call the code itself makes, the device not offered AAC again), the media hook (the engine's units as RTP and LATM, in order, sequence and timestamp,
 *     fragmented to the link's limit, nothing sent when there is no unit, never the code's SBC packet on a stream that is AAC, everything else passed through);
 *   - the stream's worker: the hooks go in only when AAC is wanted and the bytes are recognised (and are not retried until off and on), the session starts when
 *     the stream starts and ends when it stops for 3 s, when the link goes, when the console goes down and at module stop, the engine's failure ends the sound and
 *     bans the device, nothing is installed with the AAC module absent;
 *   - the link information: every case of what bttGetLinkInfo says and the size rules of the call.
 * Each scenario runs in its own process: the engine and the hooks keep their state in statics.
 */
#define BTT_HOST_TEST 1
#define BTT_HOST_INJECT 1
#include <btt_host.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef TAI_ANY_LIBRARY
#define TAI_ANY_LIBRARY 0xFFFFFFFFu
#endif

/* the hooks' own taiHEN: a reference is the original routine, and TAI_CONTINUE calls it */
typedef uintptr_t tai_hook_ref_t;
#define TAI_CONTINUE(type, ref, ...) ((type (*)())(ref))(__VA_ARGS__)
#include <taihen.h>
SceUID taiHookFunctionOffsetForKernel(SceUID pid, tai_hook_ref_t *ref, SceUID modid, int segidx, uint32_t offset, int thumb, const void *hook);
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref);
int ksceKernelUnregisterSysEventHandler(SceUID uid);
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);
int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

#include "../kernel/btaac_leaf.c"
#include "../kernel/btaac.c"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

#include "host/btaac_fakes.h"

#include "../kernel/btaac_sep.h"
#include "../kernel/btaac_latm.h"
#include "../kernel/btaac_gate.h"

/* The hashed windows' bytes are the firmware's, which the repository does not carry (the table's hashes match the twelve builds): a
 * test gives the gate text of its own and makes the table's hashes those of that text, so what is tested is the gate's arithmetic and what the hooks do after it. */
static void hash_table_for(const uint8_t *text)
{
	for (unsigned w = 0; w < BTAAC_NHWIN; w++)
		k_hw_hash[w] = btaac_hwin_live(text, w);
}
#include "../kernel/btlink_core.h"
#include "../kernel/btsignal_core.h"

/* the main module's side of the engine (the leaves and the codec test) and the hooks */
#include "../kernel/btaac_link.c"
#include "../kernel/btaac_link_leaf.c"
#include "../kernel/btaptx_link.c"
#include "../kernel/btaptx_link_leaf.c"
#include "../kernel/btldac_link.c"
#include "../kernel/btldac_link_leaf.c"
#include "../kernel/btaac_neg.c"
int btaac_host_mapped(uintptr_t va);
#include "../kernel/btaac_hook.c"

static int fake_disp_orig(void *conn, void *chan, const uint8_t *msg, int len) { (void)conn; (void)chan; (void)msg; (void)len; return 0; }
static int fake_send_orig(void *conn, const void *buf, uint32_t len, uint32_t flag) { (void)conn; (void)buf; (void)len; (void)flag; return 0; }

/* the worker's side and the link information: the Bluetooth code's text and data are memory here, taiHEN and the module manager recorders */
static uint8_t g_text[0x20000] __attribute__((aligned(16)));
static uint8_t g_seg1[0x47400] __attribute__((aligned(16)));    /* the Bluetooth code's data segment is 0x473D0 bytes */
static int g_seg1_ok = 1, g_modinfo_ok = 1, g_hook_fail_at = -1, g_qual_hook_ok = 1, g_qual_hook_calls;
static struct { uint32_t off; const void *hook; int thumb; SceUID uid; uint8_t saved[4]; } g_inst[8];
static int g_ninst, g_nrel, g_sev_registered, g_sev_unregistered, g_hook_calls;
static SceSysEventHandler g_sev_handler;
static int g_bteq_in = 1;
static uint32_t g_codecs_kept;   /* what the codec lists' file says for the link's device */
int btt_bteq_hook_in(void) { return g_bteq_in; }
static uintptr_t btt_bt_seg1(void) { return g_seg1_ok ? (uintptr_t)g_seg1 : 0; }
uint32_t btt_codecs_lookup(uint32_t lo, uint32_t hi) { return g_codecs_kept; }
int btt_btqual_hq_hook(void) { g_qual_hook_calls++; return g_qual_hook_ok ? 0 : -1; }
int taiGetModuleInfoForKernel(SceUID pid, const char *name, tai_module_info_t *info)
{
	CHECK(pid == KERNEL_PID && !strcmp(name, "SceBt"), "the Bluetooth code is looked up by its name");
	if (!g_modinfo_ok)
		return -1;
	info->modid = 0x777;
	return 0;
}
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr)
{
	CHECK(modid == 0x777 && segidx == 0 && offset == 0, "the text segment");
	*addr = (uintptr_t)g_text;
	return 0;
}
SceUID taiHookFunctionOffsetForKernel(SceUID pid, tai_hook_ref_t *ref, SceUID modid, int segidx, uint32_t offset, int thumb, const void *hook)
{
	CHECK(pid == KERNEL_PID && modid == 0x777 && segidx == 0 && g_ninst < 8, "hook arguments");
	g_hook_calls++;
	if (g_hook_fail_at >= 0 && g_hook_calls > g_hook_fail_at)
		return -1;
	CHECK(*ref == 0, "the reference is zeroed before the hook goes in");
	g_inst[g_ninst].off = offset; g_inst[g_ninst].hook = hook; g_inst[g_ninst].thumb = thumb; g_inst[g_ninst].uid = 0x900 + g_ninst;
	/* taiHEN patches the first bytes of the routine with its jump (ldr.w pc,[pc]): what a second look at the same bytes finds, on a console */
	memcpy(g_inst[g_ninst].saved, g_text + offset, 4);
	memcpy(g_text + offset, "\xDF\xF8\x00\xF0", 4);
	*ref = offset == BTAAC_OFF_DISP ? (tai_hook_ref_t)fake_disp_orig : (tai_hook_ref_t)fake_send_orig;
	return g_inst[g_ninst++].uid;
}
int taiHookReleaseForKernel(SceUID uid, tai_hook_ref_t ref)
{
	g_nrel++;
	for (int i = 0; i < g_ninst; i++)
		if (g_inst[i].uid == uid)
			memcpy(g_text + g_inst[i].off, g_inst[i].saved, 4);   /* the routine is stock again */
	return 0;
}
SceUID ksceKernelRegisterSysEventHandler(const char *name, SceSysEventHandler handler, void *args)
{
	g_sev_registered++;
	g_sev_handler = handler;
	return 0xA00;
}
int ksceKernelUnregisterSysEventHandler(SceUID uid) { g_sev_unregistered++; return 0; }
int ksceKernelMemcpyUserToKernel(void *dst, const void *src, size_t len) { memcpy(dst, src, len); return 0; }
int ksceKernelMemcpyKernelToUser(void *dst, const void *src, size_t len) { memcpy(dst, src, len); return 0; }

/* ---- the fake Bluetooth code ------------------------------------------------------------------------------------ */

/* the AAC module's table, as the main module holds it once it has looked the module up */
static const BtAacApi k_api = { .size = sizeof(BtAacApi), .version = BTAAC_API_VERSION, .start = btaac_start, .stop = btaac_stop, .sync = btaac_sync,
                                .get_info = btaac_get_info, .push_pcm = btaac_push_pcm, .pop_au = btaac_pop_au, .au_trim = btaac_au_trim, .au_depth = btaac_au_depth };

/* the engine's table is set by hand; the Bluetooth code's start call (what the retry looks up, SceBtForDriver 0xD48CA62D, NID 0x8D47CABD) is the fake below, unless a test hides it */
static int fake_start_audio(uint32_t lo, uint32_t hi, uint32_t flags);
static int g_sa_found = 1;
static int g_ld_find;                     /* 0: the LDAC module is not there; 1: its table; 2: a table of another version */
static const BtLdacApi *fake_ld_get_api(void);
static int g_apt_find;                    /* 0: the aptX module is not there; 1: its table; 2: a table of another version */
static const BtAptxApi *fake_apt_get_api(void);
int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func)
{
	if (!strcmp(modname, "bluetoothturbo_aptx") && funcnid == BTAPTX_NID_GET_API && g_apt_find) {
		*func = (uintptr_t)fake_apt_get_api;
		return 0;
	}
	if (!strcmp(modname, "bluetoothturbo_ldac") && funcnid == BTLDAC_NID_GET_API && g_ld_find) {
		*func = (uintptr_t)fake_ld_get_api;
		return 0;
	}
	if (g_sa_found && !strcmp(modname, "SceBt") && libnid == 0xD48CA62Du && funcnid == 0x8D47CABDu) {
		*func = (uintptr_t)fake_start_audio;
		return 0;
	}
	return -1;
}

#define NSLOT        BTAAC_SLOTS
static uint8_t g_slots[NSLOT * BTAAC_SLOT_SIZE] __attribute__((aligned(16)));
static uint8_t g_chan[0x1000] __attribute__((aligned(0x1000)));
static uintptr_t g_unmapped_lo, g_unmapped_hi;      /* addresses the translation operation would refuse */
int btaac_host_mapped(uintptr_t va) { return !(g_unmapped_hi && va >= (g_unmapped_lo & ~(uintptr_t)0xFFF) && va < g_unmapped_hi); }

#include "../kernel/btaac_stream.h"
#include "../kernel/btaac_stream.c"

/* the Bluetooth code's information call, as btcall.c gives it to btlink.c: the readings are what the test sets, a failure is a negative return */
static struct { int found, calls, rc; unsigned lq; int rssi; uint32_t lo, hi; unsigned zero_first; } g_sigcall = { .found = 1 };
int btcall_find_signal(void) { return g_sigcall.found ? 0 : -1; }
int btcall_signal(uint32_t lo, uint32_t hi, unsigned *lq, int *rssi)
{
	g_sigcall.calls++;
	g_sigcall.lo = lo;
	g_sigcall.hi = hi;
	if (g_sigcall.rc < 0)
		return g_sigcall.rc;
	if (g_sigcall.zero_first > 0) {   /* the first call after a connection returns zeros */
		g_sigcall.zero_first--;
		*lq = 0;
		*rssi = 0;
		return 0;
	}
	*lq = g_sigcall.lq;
	*rssi = g_sigcall.rssi;
	return 0;
}
#include "../kernel/btlink.c"

static uint8_t *slot(int i) { return g_slots + (size_t)i * BTAAC_SLOT_SIZE; }
static void put32(uint8_t *conn, uint32_t off, uint32_t v) { memcpy(conn + off, &v, 4); }
static uint32_t get32(const uint8_t *conn, uint32_t off) { uint32_t v; memcpy(&v, conn + off, 4); return v; }

/* ksceBtStartAudio as the Bluetooth code does it (the code behind it, under its lock): nothing at all when the stream is started or its "starting" bit is
 * set; else the bit is set and, with the stream record at 8, the record goes to 9 with the retry byte cleared (the START is sent). What it returns is `g_sa_ret`. */
static struct { int calls; uint32_t lo, hi, flags; int prio; } g_sa;
static int g_sa_ret, g_sa_fail;                 /* g_sa_fail: the call refuses (an error of the code's own) and sends nothing */
static uint8_t *g_sa_conn;
static int fake_start_audio(uint32_t lo, uint32_t hi, uint32_t flags)
{
	uint8_t *c = g_sa_conn;

	g_sa.calls++; g_sa.lo = lo; g_sa.hi = hi; g_sa.flags = flags; g_sa.prio = g_cur_prio;
	if (g_sa_fail)
		return (int)0x802F0D01;
	if ((get32(c, BTAAC_C_WANT) & 8) || (get32(c, BTAAC_C_PROFILES) & 8))
		return g_sa_ret;
	put32(c, BTAAC_C_WANT, get32(c, BTAAC_C_WANT) | 8);
	if (get32(c, BTAAC_C_REC_B) == BTAAC_REC_READY) {
		put32(c, BTAAC_C_REC_B, BTAAC_REC_STARTING);
		c[BTAAC_C_REC_B + BTAAC_R_RETRY] = 0;
	}
	return g_sa_ret;
}

/* what the originals were given */
static struct { int calls; void *conn, *chan; const uint8_t *msg; int len; } g_disp;
static int g_disp_ret = 0x55;
static int fake_disp(void *conn, void *chan, const uint8_t *msg, int len)
{
	g_disp.calls++; g_disp.conn = conn; g_disp.chan = chan; g_disp.msg = msg; g_disp.len = len;
	return g_disp_ret;
}
typedef struct { uint8_t data[2048]; uint32_t len, flag; } Pkt;
static Pkt g_pkts[4096];
static int g_npkts, g_send_calls, g_send_ret, g_send_fail_at = -1;
static struct { void *conn; const void *buf; uint32_t len, flag; } g_send_last;
static int fake_send(void *conn, const void *buf, uint32_t len, uint32_t flag)
{
	g_send_calls++;
	g_send_last.conn = conn; g_send_last.buf = buf; g_send_last.len = len; g_send_last.flag = flag;
	if (g_send_fail_at >= 0 && g_send_calls > g_send_fail_at)
		return (int)0x802F0D01;
	CHECK(len <= sizeof(g_pkts[0].data) && g_npkts < 4096, "packet too long or too many");
	memcpy(g_pkts[g_npkts].data, buf, len);
	g_pkts[g_npkts].flag = flag;
	g_pkts[g_npkts++].len = len;
	return g_send_ret;
}
/* the code's frame sender (resend of SET_CONFIGURATION) */
static struct { int calls; void *conn; uint32_t cid; const void *fmt; uint32_t label, sig, acp, intr; uint8_t caps[BTAAC_CAPS_BUF]; uint32_t len; int caps_is_rec; } g_l2;
static int fake_l2(void *conn, uint32_t cid, const void *fmt, uint32_t label, uint32_t sig, uint32_t acp, uint32_t intr, const void *caps, uint32_t len)
{
	g_l2.calls++; g_l2.conn = conn; g_l2.cid = cid; g_l2.fmt = fmt; g_l2.label = label; g_l2.sig = sig; g_l2.acp = acp; g_l2.intr = intr; g_l2.len = len;
	memcpy(g_l2.caps, caps, len <= BTAAC_CAPS_BUF ? len : BTAAC_CAPS_BUF);
	g_l2.caps_is_rec = caps == (const uint8_t *)conn + BTAAC_C_REC_B + 6;
	return 0;
}

/* a connection of the hooks' world: slot `i` is the link, the table starts at slot 0 */
static void world(int i, int enable)
{
	memset(g_slots, 0, sizeof(g_slots));
	memset(g_chan, 0, sizeof(g_chan));
	memset((void *)&btaac_neg, 0, sizeof(btaac_neg));
	memset((void *)&btaac_cdc, 0, sizeof(btaac_cdc));
	g_npkts = g_send_calls = 0;
	g_send_ret = 1;
	g_send_fail_at = -1;
	memset(&g_disp, 0, sizeof(g_disp));
	memset(&g_l2, 0, sizeof(g_l2));
	g_unmapped_lo = g_unmapped_hi = 0;
	btaac_neg.conn_lo = (uintptr_t)g_slots;
	btaac_neg.conn_hi = btaac_neg.conn_lo + NSLOT * BTAAC_SLOT_SIZE;
	btaac_neg.l2_send = (uintptr_t)fake_l2;
	btaac_neg.l2_fmt = (uintptr_t)0x1234;
	btaac_neg.l2_fmt_acc = (uintptr_t)0x2345;
	btaac_neg.l2_fmt_rej = (uintptr_t)0x3456;
	btaac_neg.enable = (uint32_t)enable;
	btaac_neg.kbps = 256;
	btaac_disp_ref = (tai_hook_ref_t)fake_disp;
	btaac_send_ref = (tai_hook_ref_t)fake_send;
	btaac_link_api = &k_api;
	(void)i;
}

/* ---- the reference headset (a recording) ---------------------------------- */

static const uint8_t k_discover[8] = { 0x1C, 0x08, 0x18, 0x08, 0x14, 0x08, 0x24, 0x08 };        /* endpoints 7, 6, 5, 9: sinks, audio, free */
static const uint8_t k_caps_vendor7[17] = { 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x4F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x32, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_caps_aac6[16] = { 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0xC0, 0x01, 0x8C, 0x84, 0x09, 0xB6, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_caps_sbc5[14] = { 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x3F, 0xFF, 0x02, 0x35, 0x04, 0x02, 0x02, 0x00 };
static const uint8_t k_caps_vendor9[50] = { 0x01, 0x00, 0x07, 0x2A, 0x00, 0xFF, 0xD7, 0x00, 0x00, 0x00, 0xAD, 0x00, 0x91, 0x0A, 0x53, 0x53, 0xFA, 0x7D, 0x64,
                                           0x7D, 0x00, 0x01, 0x07, 0x00, 0x00, 0x0F, 0x02, 0x03, 0x03, 0x03, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x02, 0x00 };

/* ---- the endpoint choice and the configuration ------------------------------------------------------------------- */

static void t_sep_choice(void)
{
	BtAacSink s;
	uint8_t c[64];

	/* the reference headset: the AAC endpoint is found, the others are not AAC */
	CHECK(!btaac_sep_scan(k_caps_vendor7, sizeof(k_caps_vendor7), &s), "a vendor codec is not AAC");
	CHECK(btaac_sep_scan(k_caps_aac6, sizeof(k_caps_aac6), &s), "the AAC endpoint");
	CHECK(s.object == BTAAC_OBJ_MPEG2_LC && s.vbr == 1 && s.max_bps == 264630, "object %02x, vbr %d, max %u (the headset: MPEG-2 LC, VBR, 264,630)", s.object, s.vbr, (unsigned)s.max_bps);
	CHECK(!btaac_sep_scan(k_caps_sbc5, sizeof(k_caps_sbc5), &s), "SBC is not AAC");
	CHECK(!btaac_sep_scan(k_caps_vendor9, sizeof(k_caps_vendor9), &s), "the second vendor codec is not AAC");
	/* an SBC-only sink: nothing */
	CHECK(!btaac_sep_scan(k_caps_sbc5, sizeof(k_caps_sbc5), &s) && !btaac_sep_scan(NULL, 0, &s), "an SBC-only sink offers no AAC; an empty reply neither");

	/* the endpoint's own two bytes: free audio sink only */
	static const uint8_t ok[2] = { 0x18, 0x08 }, inuse[2] = { 0x1A, 0x08 }, source[2] = { 0x18, 0x00 }, video[2] = { 0x18, 0x18 }, multi[2] = { 0x18, 0x28 };
	CHECK(btaac_sep_free_audio_sink(ok), "free audio sink");
	CHECK(!btaac_sep_free_audio_sink(inuse) && !btaac_sep_free_audio_sink(source) && !btaac_sep_free_audio_sink(video) && !btaac_sep_free_audio_sink(multi),
	      "in use, a source, video and multimedia are all refused");

	/* variations of the AAC element: [obj, f1, f2|ch, vbr|rate hi, mid, lo] */
	struct { uint8_t e[6]; int want; uint8_t obj; const char *why; } cases[] = {
		{ { 0x80, 0x01, 0x8C, 0x00, 0x00, 0x00 }, 1, 0x80, "MPEG-2 LC, rate not stated" },
		{ { 0x40, 0x01, 0x8C, 0x84, 0x09, 0xB6 }, 1, 0x40, "MPEG-4 LC only: it is configured as that" },
		{ { 0xC0, 0x01, 0x8C, 0x84, 0x09, 0xB6 }, 1, 0x80, "both: MPEG-2 (the common one)" },
		{ { 0x20, 0x01, 0x8C, 0x84, 0x09, 0xB6 }, 0, 0, "LTP only" },
		{ { 0x10, 0x01, 0x8C, 0x84, 0x09, 0xB6 }, 0, 0, "scalable only" },
		{ { 0x00, 0x01, 0x8C, 0x84, 0x09, 0xB6 }, 0, 0, "no object type" },
		{ { 0xC0, 0xFF, 0x08, 0x84, 0x09, 0xB6 }, 0, 0, "no 48 kHz" },
		{ { 0xC0, 0x01, 0x84, 0x84, 0x09, 0xB6 }, 1, 0x80, "48 kHz, two channels only" },
		{ { 0xC0, 0x01, 0x88, 0x84, 0x09, 0xB6 }, 0, 0, "48 kHz, mono only" },
		{ { 0xC0, 0x01, 0x04, 0x84, 0x09, 0xB6 }, 0, 0, "44.1 kHz band only: no 48 kHz bit" },
		{ { 0xC0, 0x01, 0x4C, 0x84, 0x09, 0xB6 }, 0, 0, "64 kHz, not 48" },
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memcpy(c, "\x01\x00\x07\x08\x00\x02", 6);
		memcpy(c + 6, cases[i].e, 6);
		memset(&s, 0xAA, sizeof(s));
		CHECK(btaac_sep_scan(c, 12, &s) == cases[i].want && (!cases[i].want || s.object == cases[i].obj), "%s", cases[i].why);
	}
	/* a malformed or truncated element, another media type, the element behind other categories */
	memcpy(c, k_caps_aac6, 16);
	CHECK(!btaac_sep_scan(c, 11, &s) && !btaac_sep_scan(c, 3, &s) && !btaac_sep_scan(c, 1, &s), "truncated");
	c[3] = 0x20;
	CHECK(!btaac_sep_scan(c, 16, &s), "an element whose length runs past the reply");
	memcpy(c, k_caps_aac6, 16);
	c[4] = 0x10;
	CHECK(!btaac_sep_scan(c, 16, &s), "another media type");
	memcpy(c, k_caps_aac6, 16);
	c[5] = 0x00;
	CHECK(!btaac_sep_scan(c, 16, &s), "SBC's codec type");
	memcpy(c, "\x04\x02\x02\x00\x08\x00\x01\x00", 8);   /* content protection, delay reporting, transport, then the codec */
	memcpy(c + 8, k_caps_aac6 + 2, 14);
	CHECK(btaac_sep_scan(c, 22, &s), "the element behind other categories");
	/* the sink's highest rate */
	memcpy(c, k_caps_aac6, 16);
	c[9] = 0x80;
	c[10] = 0; c[11] = 0;
	CHECK(btaac_sep_scan(c, 16, &s) && s.max_bps == 0 && s.vbr == 1, "a rate of 0 is 'not stated'");
	c[9] = 0x7F; c[10] = 0xFF; c[11] = 0xFF;
	CHECK(btaac_sep_scan(c, 16, &s) && s.max_bps == 0x7FFFFF && s.vbr == 0, "the 23-bit rate's top");
}

static void t_config_bytes(void)
{
	BtAacSink s = { .seid = 0x18, .object = BTAAC_OBJ_MPEG2_LC, .vbr = 1, .max_bps = 264630 };
	uint8_t c[BTAAC_CONFIG_LEN];
	static const uint8_t want[12] = { 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0x80, 0x00, 0x84, 0x03, 0xE8, 0x00 };

	CHECK(btaac_sep_bps(&s, 256) == 256000, "256 kb/s under the headset's 264,630");
	/* the AAC Bit Rate setting's four values (128, 192, 256, 320) under the reference headset's highest: 320 is held at its 264,630 */
	CHECK(btaac_sep_bps(&s, 128) == 128000 && btaac_sep_bps(&s, 192) == 192000 && btaac_sep_bps(&s, 320) == 264630,
	      "the four AAC Bit Rate values under 264,630: %u %u %u", (unsigned)btaac_sep_bps(&s, 128), (unsigned)btaac_sep_bps(&s, 192), (unsigned)btaac_sep_bps(&s, 320));
	btaac_sep_config(&s, 256000, c);
	CHECK(!memcmp(c, want, 12), "the reference headset: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11]);
	/* a lower highest rate holds it down; none stated does not; the encoder's own range is kept */
	s.max_bps = 192000;
	CHECK(btaac_sep_bps(&s, 256) == 192000, "a lower maximum");
	btaac_sep_config(&s, 192000, c);
	CHECK(c[9] == 0x02 && c[10] == 0xEE && c[11] == 0x00, "192,000 b/s = 0x02EE00");
	s.max_bps = 0;
	CHECK(btaac_sep_bps(&s, 256) == 256000 && btaac_sep_bps(&s, 320) == 320000 && btaac_sep_bps(&s, 400) == 320000, "no maximum: ours, to the encoder's top");
	s.max_bps = 5000;
	CHECK(btaac_sep_bps(&s, 256) == 8000, "a maximum under the encoder's lowest cannot go lower than it");
	s.object = BTAAC_OBJ_MPEG4_LC;
	btaac_sep_config(&s, 256000, c);
	CHECK(c[6] == 0x40 && c[8] == 0x84 && (c[9] & 0x80) == 0, "MPEG-4 LC, 48 kHz stereo, constant (the VBR bit clear)");
	/* what the scan, the bit rate and the configuration give together decode back to what was asked */
	BtAacSink got;
	memcpy(c, want, 12);
	CHECK(btaac_sep_scan(c, 12, &got) && got.object == 0x80 && got.max_bps == 256000 && got.vbr == 0, "the configuration is a capability set of one: it scans back");
	CHECK(BTAAC_CONFIG_LEN <= BTAAC_CAPS_BUF, "it fits the stream record's buffer");
}

static void t_gate(void)
{
	static uint8_t text[0x20000];

	/* the hashed windows' bytes are the firmware's, which a test does not carry (the checker proves their hashes on the twelve builds): the text here is made up, and
	 * the hashed windows' own arithmetic is what is tested below */
	for (uint32_t i = 0; i < 0x20000; i++)
		text[i] = (uint8_t)(i * 31u + 7u);
	/* the bytes of the 3.60 build, with the loader's immediates anything */
	memcpy(text + BTAAC_OFF_DISP, k_disp_head, sizeof(k_disp_head));
	memcpy(text + BTAAC_OFF_DISP_W1, k_disp_w1, sizeof(k_disp_w1));
	memcpy(text + BTAAC_OFF_DISP_W2, k_disp_w2, sizeof(k_disp_w2));
	memcpy(text + BTAAC_OFF_DISP_W3, k_disp_w3, sizeof(k_disp_w3));
	memcpy(text + BTAAC_OFF_DISP_W4, k_disp_w4, sizeof(k_disp_w4));
	memcpy(text + BTAAC_OFF_SEND, k_send_a, sizeof(k_send_a));
	memcpy(text + BTAAC_OFF_SEND_B, k_send_b, sizeof(k_send_b));
	memcpy(text + BTAAC_OFF_L2, k_l2_head, sizeof(k_l2_head));
	memcpy(text + BTAAC_OFF_L2_W, k_l2_w, sizeof(k_l2_w));
	memcpy(text + BTAAC_OFF_FMT, k_fmt, sizeof(k_fmt));
	CHECK(btaac_gate(text) == (int)BTAAC_NWIN + 1, "the made-up hashed windows are not the table's: the first hashed window is the one refused (number %d)", btaac_gate(text));
	CHECK(BTAAC_NHWIN == 13 && k_hw_first[BTAAC_NHWIN] == sizeof(k_hw_loader) / sizeof(k_hw_loader[0]), "the table hangs together (%u loader words)", (unsigned)k_hw_first[BTAAC_NHWIN]);
	for (unsigned w = 0; w < BTAAC_NHWIN; w++)
		CHECK(k_hw_off[w] + k_hw_len[w] <= 0x20000 && k_hw_first[w] <= k_hw_first[w + 1], "window %u inside the text", w);
	/* the hash function against the vector the checker's own test holds (the same bytes, FNV-1a 32, address loads masked) */
	{
		uint8_t d[40];
		static const uint16_t w1[] = { 4, 20 }, w3[] = { 0, 36 };

		for (unsigned i = 0; i < 40; i++)
			d[i] = (uint8_t)(i * 7u + 3u);
		CHECK(btaac_masked_hash(d, 40, w1, 2) == 0x2B1A3995u && btaac_masked_hash(d, 40, 0, 0) == 0x0F627655u && btaac_masked_hash(d, 40, w3, 2) == 0x2F9EFF99u, "the hash is the checker's: %08x",
		      (unsigned)btaac_masked_hash(d, 40, w1, 2));
		for (unsigned i = 0; i < 4; i++) {            /* a relocated address load hashes the same, a changed code byte next to it does not */
			uint8_t e[40];
			static const uint8_t keep[4] = { 0xF0, 0xFB, 0x00, 0x0F };

			memcpy(e, d, 40);
			e[4 + i] = (uint8_t)((e[4 + i] & keep[i]) | (uint8_t)(~keep[i] & 0x5A));
			CHECK(btaac_masked_hash(e, 40, w1, 2) == btaac_masked_hash(d, 40, w1, 2), "byte %u of an address load: the loader's bits are free", i);
			e[4 + i] ^= keep[i] ? (uint8_t)(i == 3 ? 0x01u : i == 1 ? 0x80u : 0x10u) : 0;
			if (keep[i])
				CHECK(btaac_masked_hash(e, 40, w1, 2) != btaac_masked_hash(d, 40, w1, 2), "byte %u of an address load: the opcode and register are not free", i);
		}
		d[12] ^= 1;
		CHECK(btaac_masked_hash(d, 40, w1, 2) != 0x2B1A3995u, "a changed byte is a different hash");
	}
	for (unsigned i = 0; i < BTAAC_NHWIN; i++)
		CHECK(!btaac_hwin_ok(text, i), "window %u of made-up bytes is not the table's", i);
	/* a movw/movt pair's immediate: imm4 (byte 0 low nibble), i (byte 1 bit 2), imm8 (byte 2), imm3 (byte 3 bits 4-6) */
	static const uint32_t pairs[] = { BTAAC_OFF_DISP + 4, BTAAC_OFF_DISP + 8, BTAAC_OFF_L2 + 6, BTAAC_OFF_L2 + 10, BTAAC_OFF_DISP_W4 + 96, BTAAC_OFF_DISP_W4 + 114 };
	/* the byte windows alone: put the stock bytes back over the made-up filler and check them one at a time, as the numbered gate does */
	for (unsigned i = 0; i < BTAAC_NWIN; i++)
		CHECK(btaac_win_diff(text, i) < 0, "byte window %u is stock", i);
	for (unsigned i = 0; i < 6; i++) {
		text[pairs[i]] ^= 0x0F; text[pairs[i] + 1] ^= 0x04; text[pairs[i] + 2] ^= 0xFF; text[pairs[i] + 3] ^= 0x70;
	}
	for (unsigned i = 0; i < BTAAC_NWIN; i++)
		CHECK(btaac_win_diff(text, i) < 0, "the loader's address loads may hold any immediates (window %u)", i);
	/* every byte window, one byte changed (outside the masked ones), is refused with its own number */
	struct { uint32_t off; int win; } w[] = { { BTAAC_OFF_DISP, 1 }, { BTAAC_OFF_DISP_W1, 2 }, { BTAAC_OFF_DISP_W2, 3 }, { BTAAC_OFF_DISP_W3, 4 },
	                                          { BTAAC_OFF_DISP_W4, 5 }, { BTAAC_OFF_SEND, 6 }, { BTAAC_OFF_SEND_B, 7 }, { BTAAC_OFF_L2, 8 },
	                                          { BTAAC_OFF_L2_W, 9 }, { BTAAC_OFF_FMT, 10 } };
	for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
		text[w[i].off] ^= 1;
		CHECK(btaac_gate(text) == w[i].win, "window %d: gate says %d", w[i].win, btaac_gate(text));
		text[w[i].off] ^= 1;
	}
	/* the SET_CONFIGURATION format's address load in window 5 is the loader's (the console refused the window unmasked): a relocated copy passes (above), a changed
	 * instruction beside it does not, and the first difference is reported at its own index */
	text[BTAAC_OFF_DISP_W4 + 95] ^= 0x01;
	CHECK(btaac_gate(text) == 5 && btaac_win_diff(text, 4) == 95 && btaac_win_diff(text, 3) == -1, "a byte beside the masked pair: window 5, at +95");
	text[BTAAC_OFF_DISP_W4 + 95] ^= 0x01;
	text[BTAAC_OFF_DISP_W4 + 97] ^= 0x01;               /* an opcode bit of the pair's first half (the loader's are the immediate's) */
	CHECK(btaac_gate(text) == 5 && btaac_win_diff(text, 4) == 97, "an opcode bit of the masked pair itself is refused");
	text[BTAAC_OFF_DISP_W4 + 97] ^= 0x01;
	/* the end of a window too */
	text[BTAAC_OFF_DISP_W4 + sizeof(k_disp_w4) - 1] ^= 0x80;
	CHECK(btaac_gate(text) == 5, "the last byte of a window");
	text[BTAAC_OFF_DISP_W4 + sizeof(k_disp_w4) - 1] ^= 0x80;
	/* a hashed window, made to match what is in `text`: satisfied; a changed byte or a changed opcode of its masked loads: refused with a number after the byte windows;
	 * the loader's own bits of an address load: free. (The table is const: the check is on btaac_masked_hash with the table's own offsets and loader words.) */
	for (unsigned h = 0; h < BTAAC_NHWIN; h++) {
		const uint16_t *words = k_hw_loader + k_hw_first[h];
		uint32_t nw = (uint32_t)(k_hw_first[h + 1] - k_hw_first[h]), want = btaac_masked_hash(text + k_hw_off[h], k_hw_len[h], words, nw);

		for (uint32_t k = 0; k < nw; k++) {        /* the loader writes these bytes' immediates: any value */
			uint8_t *p = text + k_hw_off[h] + words[k];

			p[0] ^= 0x0F; p[1] ^= 0x04; p[2] ^= 0xFF; p[3] ^= 0x70;
		}
		CHECK(btaac_masked_hash(text + k_hw_off[h], k_hw_len[h], words, nw) == want, "hashed window %u: the address loads' immediates are free", h);
		for (uint32_t k = 0; k < nw; k++) {
			uint8_t *p = text + k_hw_off[h] + words[k];

			p[0] ^= 0x0F; p[1] ^= 0x04; p[2] ^= 0xFF; p[3] ^= 0x70;
		}
		if (!nw || words[0] != 0) {                  /* (a window that starts with an address load has its first byte's low nibble free) */
			text[k_hw_off[h]] ^= 1;
			CHECK(btaac_masked_hash(text + k_hw_off[h], k_hw_len[h], words, nw) != want, "hashed window %u: the first byte changed", h);
			text[k_hw_off[h]] ^= 1;
		}
		text[k_hw_off[h] + k_hw_len[h] - 1] ^= 0x80;
		CHECK(btaac_masked_hash(text + k_hw_off[h], k_hw_len[h], words, nw) != want, "hashed window %u: the last byte changed", h);
		text[k_hw_off[h] + k_hw_len[h] - 1] ^= 0x80;
	}
	/* the whole gate with a table made to match this text: satisfied; then each hashed window refuses, with its own number after the byte windows', on a changed byte */
	CHECK(btaac_gate(text) == (int)BTAAC_NWIN + 1 && btaac_hwin_live(text, 0) != k_hw_hash[0], "the table is not this text's: the first hashed window is refused, number %d", (int)BTAAC_NWIN + 1);
	hash_table_for(text);
	CHECK(btaac_gate(text) == 0, "every window matches");
	for (unsigned h = 0; h < BTAAC_NHWIN; h++) {
		uint32_t at = k_hw_off[h] + k_hw_len[h] - 1u;     /* the last byte (none of these windows ends in an address load, and the byte windows lie elsewhere) */

		text[at] ^= 0x10;
		CHECK(btaac_gate(text) == (int)(BTAAC_NWIN + h + 1), "a changed byte in hashed window %u is refused as number %d, not %d", h, (int)(BTAAC_NWIN + h + 1), btaac_gate(text));
		text[at] ^= 0x10;
	}
	CHECK(btaac_gate(text) == 0, "and every window matches again");
}

/* ---- the packet ---------------------------------------------------------------------------------------------------- */

/* A reader of the AudioMuxElement written from the standard (ISO/IEC 14496-3 4.4.1), independent of the writer: every field of the StreamMuxConfig for the
 * one configuration we send is checked, then the length coding and the payload; returns the access unit's length (and its place), or -1. */
typedef struct { const uint8_t *p; uint32_t pos, nbits; } BitReader;
static uint32_t rbits(BitReader *r, int n)
{
	uint32_t v = 0;

	CHECK(r->pos + (uint32_t)n <= r->nbits, "the reader ran past the element");
	while (n--) {
		v = (v << 1) | ((r->p[r->pos >> 3] >> (7 - (r->pos & 7))) & 1u);
		r->pos++;
	}
	return v;
}
static int latm_read(const uint8_t *el, uint32_t len, uint8_t *au, uint32_t cap)
{
	BitReader r = { el, 0, len * 8 };
	uint32_t n = 0, tmp;

	CHECK(rbits(&r, 1) == 0, "useSameStreamMux: 0, the configuration is in band");
	CHECK(rbits(&r, 1) == 0, "audioMuxVersion 0");
	CHECK(rbits(&r, 1) == 1, "allStreamsSameTimeFraming 1");
	CHECK(rbits(&r, 6) == 0, "numSubFrames 0 (one frame)");
	CHECK(rbits(&r, 4) == 0, "numProgram 0 (one program)");
	CHECK(rbits(&r, 3) == 0, "numLayer 0 (one layer)");
	CHECK(rbits(&r, 5) == 2, "audioObjectType 2: AAC LC");
	CHECK(rbits(&r, 4) == 3, "samplingFrequencyIndex 3: 48 kHz");
	CHECK(rbits(&r, 4) == 2, "channelConfiguration 2: stereo");
	CHECK(rbits(&r, 3) == 0, "GASpecificConfig: 1024 frame, no core coder, no extension");
	CHECK(rbits(&r, 3) == 0, "frameLengthType 0: lengths coded in the payload");
	CHECK(rbits(&r, 8) == 0xFF, "latmBufferFullness 0xFF: variable");
	CHECK(rbits(&r, 1) == 0, "otherDataPresent 0");
	CHECK(rbits(&r, 1) == 0, "crcCheckPresent 0");
	CHECK(r.pos == 45, "the StreamMuxConfig and the flag are 45 bits");
	do {                                   /* PayloadLengthInfo: 255 means 'and more' */
		tmp = rbits(&r, 8);
		n += tmp;
	} while (tmp == 255);
	if (n > cap)
		return -1;
	for (uint32_t i = 0; i < n; i++)
		au[i] = (uint8_t)rbits(&r, 8);
	/* then only zero bits to the byte */
	CHECK(len == (r.pos + 7) / 8, "the element is %u bytes, the standard says %u", (unsigned)len, (unsigned)((r.pos + 7) / 8));
	while (r.pos < r.nbits)
		CHECK(rbits(&r, 1) == 0, "padding bits are zero");
	return (int)n;
}

static void t_latm(void)
{
	static uint8_t au[1600], el[BTAAC_LATM_MAX + 8], back[1600];
	static const uint32_t lens[] = { 1, 2, 7, 100, 254, 255, 256, 509, 510, 511, 683, 840, 1024, 1536 };

	for (unsigned k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
		uint32_t n = lens[k], want_nlen = n / 255 + 1, total;

		for (uint32_t i = 0; i < n; i++)
			au[i] = (uint8_t)(i * 37 + 11 + k);
		memset(el, 0xCC, sizeof(el));
		total = btaac_latm_wrap(el, sizeof(el), au, n);
		CHECK(total == (45 + 8 * (want_nlen + n) + 7) / 8 && total == n + want_nlen + 6, "length %u: element %u bytes", (unsigned)n, (unsigned)total);
		CHECK(el[total] == 0xCC, "nothing written past the element");
		CHECK(latm_read(el, total, back, sizeof(back)) == (int)n && !memcmp(back, au, n), "length %u: the standard's reader gets the unit back", (unsigned)n);
	}
	/* the first bytes, by hand from the bit layout above: 0x20 0x00 (the flags and counts), 0x11 0x90 (the config), then 0x1F 0xE0.. */
	au[0] = 0xA5;
	CHECK(btaac_latm_wrap(el, sizeof(el), au, 1) == 8, "one byte");
	CHECK(!memcmp(el, "\x20\x00\x11\x90\x1F\xE0\x0D\x28", 8), "the first bytes %02x %02x %02x %02x %02x %02x %02x %02x", el[0], el[1], el[2], el[3], el[4], el[5], el[6], el[7]);
	/* refusals */
	CHECK(btaac_latm_wrap(el, 20, au, 100) == 0 && btaac_latm_wrap(el, sizeof(el), au, 0) == 0, "too big for the buffer, and empty");
	CHECK(btaac_latm_wrap(el, 107, au, 100) == 107 && btaac_latm_wrap(el, 106, au, 100) == 0, "exactly the buffer, and one byte short");
	/* the RTP header */
	uint8_t h[12];
	btaac_rtp_header(h, 0x1234, 0xAABBCCDD, 0x01020304, 0);
	CHECK(!memcmp(h, "\x80\x60\x12\x34\xAA\xBB\xCC\xDD\x01\x02\x03\x04", 12), "RTP: version 2, payload type 96, no marker");
	btaac_rtp_header(h, 0xFFFF, 1, 0xFFFFFFFF, 1);
	CHECK(h[0] == 0x80 && h[1] == 0xE0 && h[2] == 0xFF && h[3] == 0xFF && h[7] == 1, "RTP: marker set");
}

static void t_fragment(void)
{
	static uint8_t latm[BTAAC_LATM_MAX], pkt[BTAAC_LATM_MAX + 12], back[BTAAC_LATM_MAX + 12];
	static const uint32_t mtus[] = { 256, 300, 512, 672, 895, 1021, 1691 };

	for (uint32_t i = 0; i < sizeof(latm); i++)
		latm[i] = (uint8_t)(i * 13 + 5);
	for (unsigned m = 0; m < sizeof(mtus) / sizeof(mtus[0]); m++)
		for (uint32_t n = 1; n <= BTAAC_LATM_MAX; n += (n < 40 ? 1 : 37)) {
			uint32_t mtu = mtus[m], pieces = btaac_latm_pieces(n, mtu), got = 0, room = mtu - 12;

			CHECK(pieces == (n + room - 1) / room && pieces >= 1, "%u bytes at %u: %u pieces", (unsigned)n, (unsigned)mtu, (unsigned)pieces);
			for (uint32_t k = 0; k < pieces; k++) {
				uint32_t len = btaac_rtp_piece(pkt, latm, n, mtu, k, (uint16_t)(100 + k), 4096, 0xDEADBEEF);

				CHECK(len <= mtu && len > 12, "piece %u of %u is %u bytes at a limit of %u", (unsigned)k, (unsigned)pieces, (unsigned)len, (unsigned)mtu);
				CHECK((pkt[1] & 0x80) == (k + 1 == pieces ? 0x80 : 0), "the marker on the last piece only");
				CHECK(pkt[2] == 0 && pkt[3] == 100 + k, "the sequence number runs on");
				CHECK(pkt[4] == 0 && pkt[5] == 0 && pkt[6] == 0x10 && pkt[7] == 0, "the same timestamp in every piece");
				CHECK(k + 1 == pieces || len == mtu, "every piece but the last is full");
				memcpy(back + got, pkt + 12, len - 12);
				got += len - 12;
			}
			CHECK(got == n && !memcmp(back, latm, n), "%u bytes at %u: reassembled exactly", (unsigned)n, (unsigned)mtu);
		}
	CHECK(btaac_mtu_sane(0) == 672 && btaac_mtu_sane(255) == 672 && btaac_mtu_sane(256) == 256 && btaac_mtu_sane(672) == 672 && btaac_mtu_sane(1021) == 1021 &&
	      btaac_mtu_sane(4096) == 4096 && btaac_mtu_sane(4097) == 672 && btaac_mtu_sane(0xFFFFFFFFu) == 672, "the limit is believed only inside what is believable");
	/* the timestamp: 1024 a unit, a gap where the encoder dropped one, a new session carries on */
	CHECK(btaac_ts_next(0, 0, 0, 0) == 0, "the first unit keeps the start value");
	CHECK(btaac_ts_next(0, 0, 1, 1) == 1024 && btaac_ts_next(1024, 1, 2, 1) == 2048, "1024 samples a unit");
	CHECK(btaac_ts_next(2048, 2, 5, 1) == 2048 + 3 * 1024, "a unit that was dropped leaves its gap");
	CHECK(btaac_ts_next(9 * 1024, 40, 0, 1) == 10 * 1024, "a new session (the number went back) carries on one unit later");
	CHECK(btaac_ts_next(0xFFFFFC00u, 0, 1, 1) == 0, "the 32 bits wrap");
}

/* ---- ffmpeg ---------------------------------------------------------------------------------------------------------- */

static int have_ffmpeg(void) { return system("command -v ffmpeg >/dev/null 2>&1") == 0; }

/* LOAS (ISO/IEC 14496-3 Annex 1.7): a sync word 0x2B7, a 13-bit length, then the AudioMuxElement: the framing ffmpeg reads LATM in. */
static uint32_t loas_wrap(uint8_t *out, const uint8_t *el, uint32_t len)
{
	out[0] = 0x56;
	out[1] = (uint8_t)(0xE0 | ((len >> 8) & 0x1F));
	out[2] = (uint8_t)len;
	memcpy(out + 3, el, len);
	return len + 3;
}

/* Goertzel: the power of one frequency in the left channel of interleaved s16 stereo */
static double tone_power(const int16_t *s, uint32_t frames, double hz)
{
	double w = 2 * 3.14159265358979323846 * hz / 48000.0, c = 2 * __builtin_cos(w), s1 = 0, s2 = 0;

	for (uint32_t i = 0; i < frames; i++) {
		double s0 = s[2 * i] + c * s1 - s2;

		s2 = s1;
		s1 = s0;
	}
	return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)frames * frames);
}

/* Decodes a LOAS file with ffmpeg; returns the frames of interleaved stereo it gave (allocated), or NULL. */
static int16_t *ffmpeg_decode(const char *path, uint32_t *frames)
{
	char cmd[512], out[256];
	FILE *f;
	long n;
	int16_t *pcm;

	snprintf(out, sizeof(out), "%s.raw", path);
	snprintf(cmd, sizeof(cmd), "ffmpeg -v error -y -f loas -i '%s' -f s16le -ar 48000 -ac 2 '%s' 2>&1", path, out);
	if (system(cmd) != 0)
		return NULL;
	f = fopen(out, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	pcm = malloc((size_t)n + 4);
	CHECK(fread(pcm, 1, (size_t)n, f) == (size_t)n, "read back");
	fclose(f);
	unlink(out);
	*frames = (uint32_t)(n / 4);
	return pcm;
}

/* A two-tone signal (1000 Hz left, 440 Hz right) in blocks of 512 frames */
static void tone_block(uint32_t n0, int16_t *blk)
{
	for (uint32_t i = 0; i < 512; i++) {
		double t = (double)(n0 + i) / 48000.0;

		blk[2 * i] = (int16_t)(12000.0 * __builtin_sin(2 * 3.14159265358979323846 * 1000.0 * t));
		blk[2 * i + 1] = (int16_t)(12000.0 * __builtin_sin(2 * 3.14159265358979323846 * 440.0 * t));
	}
}

/* Checks a decoded stream: all the units decoded, and the tone is there (the left channel is 1000 Hz, strongly, against 2000 Hz) */
static void check_decoded(const int16_t *pcm, uint32_t frames, uint32_t aus, const char *what)
{
	CHECK(frames >= (aus - 1) * 1024 && frames <= (aus + 1) * 1024, "%s: ffmpeg gave %u frames for %u units", what, (unsigned)frames, (unsigned)aus);
	uint32_t from = 8 * 1024, n = frames - from - 1024;
	double p1000 = tone_power(pcm + 2 * from, n, 1000), p2000 = tone_power(pcm + 2 * from, n, 2000);

	CHECK(p1000 > 1000.0 * p2000 && p1000 > 1e6, "%s: the 1000 Hz tone is there (power %g, against %g at 2000 Hz)", what, p1000, p2000);
}

static void t_ffmpeg_decode(void)
{
	static uint8_t au[BTAAC_AU_MAX], el[BTAAC_LATM_MAX], loas[BTAAC_LATM_MAX + 3], pkt[BTAAC_LATM_MAX + 12], whole[BTAAC_LATM_MAX];
	static int16_t blk[512 * 2];
	uint32_t pushed = 0, seq, aus = 0, n;
	FILE *f1, *f2;
	char p1[128], p2[128];

	if (!have_ffmpeg()) {
		printf("  (ffmpeg is not on this machine: the packets were checked against the standard's reader only)\n");
		return;
	}
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 264, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	snprintf(p1, sizeof(p1), "build/test_stream_whole_%d.loas", (int)getpid());
	snprintf(p2, sizeof(p2), "build/test_stream_pieces_%d.loas", (int)getpid());
	f1 = fopen(p1, "wb");
	f2 = fopen(p2, "wb");
	CHECK(f1 && f2, "temporary files (run from the product's folder)");
	while (aus < 200) {
		if (pushed < 512u * 500) {
			tone_block(pushed, blk);
			if (btaac_push_pcm(blk, 512) == 512)
				pushed += 512;
		}
		n = btaac_pop_au(au, sizeof(au), &seq);
		if (!n) {
			usleep(500);
			continue;
		}
		uint32_t latm_len = btaac_latm_wrap(el, sizeof(el), au, n);

		CHECK(latm_len && (int)n == latm_read(el, latm_len, whole, sizeof(whole)) && !memcmp(whole, au, n), "unit %u wrapped and read back", (unsigned)aus);
		fwrite(loas, 1, loas_wrap(loas, el, latm_len), f1);
		/* the same unit cut to a limit of 300 bytes, put back together from the packets */
		uint32_t pieces = btaac_latm_pieces(latm_len, 300), got = 0;

		for (uint32_t k = 0; k < pieces; k++) {
			uint32_t len = btaac_rtp_piece(pkt, el, latm_len, 300, k, (uint16_t)k, aus * 1024, 1);

			memcpy(whole + got, pkt + 12, len - 12);
			got += len - 12;
		}
		CHECK(got == latm_len, "reassembled %u of %u", (unsigned)got, (unsigned)latm_len);
		fwrite(loas, 1, loas_wrap(loas, whole, got), f2);
		aus++;
	}
	fclose(f1);
	fclose(f2);
	engine_down();
	uint32_t frames = 0;
	int16_t *pcm = ffmpeg_decode(p1, &frames);

	CHECK(pcm, "ffmpeg refused the stream of whole elements");
	check_decoded(pcm, frames, aus, "whole elements");
	free(pcm);
	pcm = ffmpeg_decode(p2, &frames);
	CHECK(pcm, "ffmpeg refused the stream put back together from pieces");
	check_decoded(pcm, frames, aus, "elements from pieces");
	free(pcm);
	unlink(p1);
	unlink(p2);
}

/* ---- the signalling hook and the rewrite ----------------------------------------------------------------------------- */

static uint8_t g_msg[256];
static int send_disp(uint8_t *conn, int mt, int sig, const uint8_t *body, int n)   /* one received message through the hook */
{
	g_msg[0] = (uint8_t)(0x10 | mt);
	g_msg[1] = (uint8_t)sig;
	memcpy(g_msg + 2, body, (size_t)n);
	return btaac_disp_hook(conn, g_chan, g_msg, n + 2);
}
static void ask_caps(uint8_t *conn, int idx, const uint8_t *caps, int n)   /* the reply for the endpoint at `idx` of the list */
{
	conn[BTAAC_C_INDEX] = (uint8_t)idx;
	CHECK(send_disp(conn, 2, 2, caps, n) == g_disp_ret, "the original's answer comes back");
}
static void start_negotiation(uint8_t *conn)
{
	memcpy(conn + BTAAC_C_LIST, k_discover, 8);
	conn[BTAAC_C_ADDR_LO] = 0x11; conn[BTAAC_C_ADDR_LO + 1] = 0x22; conn[BTAAC_C_ADDR_LO + 2] = 0x33; conn[BTAAC_C_ADDR_LO + 3] = 0x44;
	conn[BTAAC_C_ADDR_HI] = 0x55; conn[BTAAC_C_ADDR_HI + 1] = 0x66;
	CHECK(send_disp(conn, 2, 1, k_discover, 8) == g_disp_ret, "DISCOVER's reply");
}
static void all_caps(uint8_t *conn)
{
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	ask_caps(conn, 2, k_caps_aac6, sizeof(k_caps_aac6));
	ask_caps(conn, 4, k_caps_sbc5, sizeof(k_caps_sbc5));
	ask_caps(conn, 6, k_caps_vendor9, sizeof(k_caps_vendor9));
}

/* what the code does between the replies and the send: the narrowing of its SBC choice (state 4, the record's buffer), then our H2 */
static const uint8_t k_sbc_cfg[14] = { 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x21, 0x15, 0x02, 0x35, 0x04, 0x02, 0x02, 0x00 };
static void stock_narrows(uint8_t *conn)
{
	uint8_t *rec = conn + BTAAC_C_REC_B;

	put32(rec, 0, 4);
	rec[4] = 0x04;
	rec[5] = 0x14;                                    /* endpoint 5, the SBC one the code chose */
	memcpy(rec + 6, k_sbc_cfg, 14);
	put32(rec, 0x18, 14);
	rec[0x20] = 0x30;                                 /* the label it used */
}

static void t_negotiation(void)
{
	uint8_t *conn = slot(2);

	world(2, 1);
	CHECK(btaac_disp_hook(conn, g_chan, (const uint8_t *)"\x10\x07", 2) == 0x55 && g_disp.calls == 1 && btaac_neg.state == BTAAC_N_NONE, "the original runs for everything");
	/* DISCOVER, then the four replies in the order the headset listed them */
	g_disp.calls = 0;
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_PROBING && btaac_neg.conn == (uintptr_t)conn && !btaac_neg.have && btaac_neg.n_discover == 1, "probing after DISCOVER");
	CHECK(g_disp.calls == 1 && g_disp.conn == conn && g_disp.chan == g_chan && g_disp.msg == g_msg && g_disp.len == 10, "the original got DISCOVER's reply as it came");
	all_caps(conn);
	CHECK(btaac_neg.have && btaac_neg.seid == 0x18 && btaac_neg.object == BTAAC_OBJ_MPEG2_LC && btaac_neg.max_bps == 264630 && btaac_neg.vbr == 1,
	      "the reference headset's AAC endpoint (6, byte 0x18): %02x, object %02x, max %u", (unsigned)btaac_neg.seid, (unsigned)btaac_neg.object, (unsigned)btaac_neg.max_bps);
	CHECK(btaac_neg.n_caps == 4 && btaac_neg.n_chosen == 1 && g_disp.calls == 5, "four replies read, five calls to the original");
	/* the code narrows its SBC choice, then H2 */
	stock_narrows(conn);
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	uint8_t *rec = conn + BTAAC_C_REC_B;
	static const uint8_t want[12] = { 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0x80, 0x00, 0x84, 0x03, 0xE8, 0x00 };

	CHECK(btaac_neg.state == BTAAC_N_PENDING && rec[5] == 0x18 && rec[4] == 0x04 && get32(rec, 0x18) == 12 && !memcmp(rec + 6, want, 12) && get32(rec, 0) == 4 && rec[0x20] == 0x30,
	      "the record: endpoint %02x, length %u, then the 12 bytes (state PENDING)", rec[5], (unsigned)get32(rec, 0x18));
	CHECK(btaac_neg.sbc_seid == 0x14 && btaac_neg.sbc_len == 14 && !memcmp((const void *)btaac_neg.sbc_caps, k_sbc_cfg, 14) && btaac_neg.bps == 256000, "the SBC choice was kept");
	CHECK(btaac_neg.n_rewrite == 1, "one rewrite");
	/* a second call (the code's other path) changes nothing */
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 12);
	CHECK(btaac_neg.n_rewrite == 1 && btaac_neg.state == BTAAC_N_PENDING && !memcmp(rec + 6, want, 12), "no second rewrite");
	/* the sink accepts */
	CHECK(send_disp(conn, 2, 3, (const uint8_t *)"", 0) == g_disp_ret, "the original's answer comes back");
	CHECK(btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.n_accept == 1 && g_l2.calls == 0, "configured, nothing resent");
	/* later messages of the connection change nothing */
	CHECK(send_disp(conn, 2, 3, (const uint8_t *)"", 0) == g_disp_ret && btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.n_accept == 1, "another accept is not ours");
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x13", 2) == g_disp_ret && btaac_neg.state == BTAAC_N_CONFIGURED && g_l2.calls == 0, "a reject now is not ours either");
	/* a new DISCOVER on the same link starts again */
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_PROBING && !btaac_neg.have && !btaac_neg.send_gate && !btaac_neg.pcm_gate, "DISCOVER resets the negotiation and closes the gates");
}

static void t_negotiation_sbc_only(void)
{
	uint8_t *conn = slot(0);

	world(0, 1);
	memcpy(conn + BTAAC_C_LIST, k_discover, 2);
	start_negotiation(conn);
	ask_caps(conn, 0, k_caps_sbc5, sizeof(k_caps_sbc5));
	CHECK(!btaac_neg.have && btaac_neg.state == BTAAC_N_PROBING, "an SBC-only sink: no AAC endpoint");
	stock_narrows(conn);
	uint8_t before[BTAAC_SLOT_SIZE];
	memcpy(before, conn, sizeof(before));
	btaac_h2_after(conn, 8, conn + BTAAC_C_REC_B + 6, 14);
	CHECK(!memcmp(before, conn, sizeof(before)) && btaac_neg.state == BTAAC_N_NONE && btaac_neg.n_rewrite == 0 && btaac_neg.n_declined == 1,
	      "the SBC configuration is left exactly as the code made it, and the state is SBC");
	/* an answer to SET_CONFIGURATION now is not ours: nothing is resent on a reject */
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x13", 2) == g_disp_ret && g_l2.calls == 0 && btaac_neg.state == BTAAC_N_NONE, "a reject of the SBC configuration is the code's own business");
}

/* ---- the headphones' codec list (btcodecs.h), read from the same replies whatever is being sent ------------------------------------------------------- */

static void t_codec_lists(void)
{
	uint8_t *conn = slot(2), *other = slot(4), before[BTAAC_SLOT_SIZE];
	uint8_t list[8];

	/* the reference headset: endpoints 7, 6, 5, 9 (aptX, AAC, SBC, the adaptive aptX), as k_discover lists them */
	world(2, 0);                                  /* nothing is offered: the codec is SBC, the reader is still on */
	btaac_cdc.on = 1;
	memcpy(conn + BTAAC_C_ADDR_LO, "\x44\x33\x22\x11", 4);
	conn[BTAAC_C_ADDR_HI] = 0x66; conn[BTAAC_C_ADDR_HI + 1] = 0x55;
	memcpy(conn + BTAAC_C_LIST, k_discover, 8);
	send_disp(conn, 2, 1, k_discover, 8);
	CHECK(btaac_cdc.conn == (uintptr_t)conn && btaac_cdc.nsep == 4 && btaac_cdc.done_seq == 0 && btaac_neg.n_discover == 0, "DISCOVER's reply starts a list; nothing of the negotiation moves");
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	ask_caps(conn, 2, k_caps_aac6, sizeof(k_caps_aac6));
	ask_caps(conn, 4, k_caps_sbc5, sizeof(k_caps_sbc5));
	CHECK(btaac_cdc.done_seq == 0, "three of four answered: not finished");
	ask_caps(conn, 6, k_caps_vendor9, sizeof(k_caps_vendor9));
	CHECK(btaac_cdc.done_seq == 2 && btaac_cdc.done_mask == (BTT_HC_APTX | BTT_HC_AAC | BTT_HC_SBC | BTT_HC_APTX_ADAPTIVE) && btaac_cdc.done_lo == 0x11223344u &&
	      btaac_cdc.done_hi == 0x5566u && btaac_cdc.conn == 0, "finished: SBC, AAC, aptX and aptX Adaptive, for this address");
	CHECK(g_disp.calls == 5 && btaac_neg.n_caps == 0 && btaac_neg.state == BTAAC_N_NONE, "the original ran for every message, and the negotiation never moved (no codec offered)");
	/* a second reply for an endpoint that has answered, or a late one after the list: ignored */
	ask_caps(conn, 6, k_caps_vendor9, sizeof(k_caps_vendor9));
	CHECK(btaac_cdc.done_seq == 2, "nothing is published twice");

	/* the same with AAC being offered: the choice is made as ever, and the list is read as well */
	world(2, 1);
	btaac_cdc.on = 1;
	memcpy(conn + BTAAC_C_LIST, k_discover, 8);
	send_disp(conn, 2, 1, k_discover, 8);
	CHECK(btaac_neg.state == BTAAC_N_PROBING, "AAC wanted: the negotiation starts as before");
	ask_caps(conn, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	ask_caps(conn, 2, k_caps_aac6, sizeof(k_caps_aac6));
	ask_caps(conn, 4, k_caps_sbc5, sizeof(k_caps_sbc5));
	ask_caps(conn, 6, k_caps_vendor9, sizeof(k_caps_vendor9));
	CHECK(btaac_neg.have && btaac_neg.seid == 0x18 && btaac_cdc.done_seq == 2 && btaac_cdc.done_mask == (BTT_HC_APTX | BTT_HC_AAC | BTT_HC_SBC | BTT_HC_APTX_ADAPTIVE), "both happen");

	/* an endpoint that is not an audio sink adds nothing (a source, a video sink); one in use still counts */
	world(2, 0);
	btaac_cdc.on = 1;
	memcpy(conn + BTAAC_C_LIST, "\x08\x00\x0A\x08", 4);       /* an audio source, then a sink in use */
	send_disp(conn, 2, 1, (const uint8_t *)"\x08\x00\x0A\x08", 4);
	ask_caps(conn, 0, k_caps_aac6, sizeof(k_caps_aac6));
	ask_caps(conn, 2, k_caps_sbc5, sizeof(k_caps_sbc5));
	CHECK(btaac_cdc.done_seq == 2 && btaac_cdc.done_mask == BTT_HC_SBC, "a source's codec is not the headphones': only the sink's, in use or not");
	world(2, 0);
	btaac_cdc.on = 1;
	send_disp(conn, 2, 1, (const uint8_t *)"\x08", 1);   /* too short a list is not one (the code refuses it too) */
	CHECK(btaac_cdc.conn == 0, "a DISCOVER reply of one byte holds no endpoint");

	/* off: nothing is read, nothing is changed in the connection's bytes; another link's replies are not this list's; a unreadable page leaves the list unfinished */
	world(2, 0);
	memcpy(conn + BTAAC_C_LIST, k_discover, 8);
	send_disp(conn, 2, 1, k_discover, 8);
	CHECK(btaac_cdc.conn == 0 && btaac_cdc.n_read == 0, "the reader is off: DISCOVER is not read");
	btaac_cdc.on = 1;
	send_disp(conn, 2, 1, k_discover, 8);
	memcpy(other + BTAAC_C_LIST, k_discover, 8);
	ask_caps(other, 0, k_caps_vendor7, sizeof(k_caps_vendor7));
	CHECK(btaac_cdc.seen == 0, "another link's reply is not this list's");
	memcpy(before, conn, sizeof(before));
	g_msg[0] = 0x12; g_msg[1] = 0x02;
	memcpy(g_msg + 2, k_caps_vendor7, sizeof(k_caps_vendor7));
	conn[BTAAC_C_INDEX] = 0;
	g_unmapped_lo = (uintptr_t)g_msg;
	g_unmapped_hi = (uintptr_t)g_msg + 0x2000;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2 + (int)sizeof(k_caps_vendor7)) == g_disp_ret && btaac_cdc.seen == 0, "an unreadable page is not read; the original still runs");
	g_unmapped_lo = g_unmapped_hi = 0;
	g_msg[0] = 0x16;                                           /* a fragment */
	btaac_disp_hook(conn, g_chan, g_msg, 2 + (int)sizeof(k_caps_vendor7));
	CHECK(btaac_cdc.seen == 0 && !memcmp(before, conn, sizeof(before)), "a fragment is not read, and the connection is not written to");
	conn[BTAAC_C_INDEX] = 1;
	ask_caps(conn, 1, k_caps_vendor7, sizeof(k_caps_vendor7));
	conn[BTAAC_C_INDEX] = 8;
	ask_caps(conn, 8, k_caps_vendor7, sizeof(k_caps_vendor7));
	CHECK(btaac_cdc.seen == 0, "an odd index, or one past the list, answers no endpoint");
	/* a headset that lists eight endpoints: the code keeps eight; a ninth is not read */
	world(2, 0);
	btaac_cdc.on = 1;
	for (int i = 0; i < 8; i++) { list[i] = (uint8_t)(0x04 + 4 * i); }
	uint8_t nine[18];
	for (int i = 0; i < 9; i++) { nine[2 * i] = (uint8_t)(0x04 + 4 * i); nine[2 * i + 1] = 0x08; }
	send_disp(conn, 2, 1, nine, 18);
	CHECK(btaac_cdc.nsep == 8, "at most the eight the code keeps");
	for (int i = 0; i < 8; i++)
		ask_caps(conn, 2 * i, k_caps_sbc5, sizeof(k_caps_sbc5));
	CHECK(btaac_cdc.done_seq == 2 && btaac_cdc.done_mask == BTT_HC_SBC, "eight answered: finished");
	(void)list;
}

static void t_negotiation_rules(void)
{
	uint8_t *conn = slot(1);
	uint8_t caps[64], list[16];

	/* switched off: nothing is looked at, the state stays SBC, everything reaches the original */
	world(1, 0);
	memcpy(conn + BTAAC_C_LIST, k_discover, 8);
	CHECK(send_disp(conn, 2, 1, k_discover, 8) == g_disp_ret && btaac_neg.state == BTAAC_N_NONE && btaac_neg.n_discover == 0 && g_disp.calls == 1, "switched off: DISCOVER is not read");
	CHECK(send_disp(conn, 2, 2, k_caps_aac6, 16) == g_disp_ret && btaac_neg.n_caps == 0 && g_disp.calls == 2, "switched off: replies are not read");
	/* the reference not yet stored: the original is skipped for one message, nothing of ours is done */
	world(1, 1);
	btaac_disp_ref = 0;
	CHECK(send_disp(conn, 2, 1, k_discover, 8) == 0 && g_disp.calls == 0 && btaac_neg.n_discover == 0, "no reference yet: nothing called, nothing done");
	btaac_disp_ref = (tai_hook_ref_t)fake_disp;
	/* a link that is not one of the Bluetooth code's slots is not looked at */
	{
		uint8_t other[BTAAC_SLOT_SIZE];

		memset(other, 0, sizeof(other));
		CHECK(btaac_disp_hook(other, g_chan, k_discover, 8) == g_disp_ret && btaac_neg.n_discover == 0, "not a link slot");
		CHECK(btaac_disp_hook(slot(0) + 4, g_chan, k_discover, 8) == g_disp_ret && btaac_neg.n_discover == 0, "inside a slot is not a slot");
	}
	/* fragments of a longer message, very short ones and unreadable pages are not read */
	start_negotiation(conn);
	g_msg[0] = 0x16; g_msg[1] = 0x02;                      /* packet type 1: the start of a fragmented message */
	memcpy(g_msg + 2, k_caps_aac6, 16);
	conn[BTAAC_C_INDEX] = 2;
	btaac_disp_hook(conn, g_chan, g_msg, 18);
	CHECK(!btaac_neg.have && btaac_neg.n_caps == 0, "a fragment is not read");
	btaac_disp_hook(conn, g_chan, g_msg, 1);
	btaac_disp_hook(conn, g_chan, g_msg, 0);
	btaac_disp_hook(conn, g_chan, g_msg, -5);
	CHECK(!btaac_neg.have, "a message too short");
	g_msg[0] = 0x12;
	g_unmapped_lo = (uintptr_t)g_msg;
	g_unmapped_hi = (uintptr_t)g_msg + 0x1000 + 0x1000;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 18) == g_disp_ret && !btaac_neg.have && btaac_neg.n_caps == 0, "an unreadable message page is not read (the original still runs)");
	g_unmapped_lo = g_unmapped_hi = 0;
	/* the replies of another link are not this negotiation's */
	{
		uint8_t *other = slot(3);

		memcpy(other + BTAAC_C_LIST, k_discover, 8);
		other[BTAAC_C_INDEX] = 2;
		g_msg[0] = 0x12; g_msg[1] = 0x02;
		memcpy(g_msg + 2, k_caps_aac6, 16);
		btaac_disp_hook(other, g_chan, g_msg, 18);
		CHECK(!btaac_neg.have, "another link's reply");
	}
	/* a reply with the index at the end of the list, or past it */
	conn[BTAAC_C_INDEX] = 15;
	btaac_disp_hook(conn, g_chan, g_msg, 18);
	conn[BTAAC_C_INDEX] = 0xFF;
	btaac_disp_hook(conn, g_chan, g_msg, 18);
	CHECK(!btaac_neg.have, "an index outside the list of 16 bytes");
	/* an endpoint in use, a source, a video endpoint: not chosen even when they offer AAC */
	static const uint8_t bad_info[3][2] = { { 0x1A, 0x08 }, { 0x18, 0x00 }, { 0x18, 0x18 } };
	for (int i = 0; i < 3; i++) {
		memcpy(list, bad_info[i], 2);
		memcpy(conn + BTAAC_C_LIST, list, 2);
		ask_caps(conn, 0, k_caps_aac6, 16);
		CHECK(!btaac_neg.have, "endpoint info %d", i);
	}
	/* the first free one wins: two AAC endpoints, the second is not taken */
	memcpy(conn + BTAAC_C_LIST, "\x18\x08\x28\x08", 4);
	ask_caps(conn, 0, k_caps_aac6, 16);
	memcpy(caps, k_caps_aac6, 16);
	caps[6] = 0x40;
	ask_caps(conn, 2, caps, 16);
	CHECK(btaac_neg.have && btaac_neg.seid == 0x18 && btaac_neg.object == BTAAC_OBJ_MPEG2_LC && btaac_neg.n_chosen == 1, "the first free AAC endpoint stays");
	/* a reply longer than the 64 bytes that are read: the element beyond them is not found */
	start_negotiation(conn);
	memset(caps, 0, sizeof(caps));
	memcpy(g_msg + 2, "\x01\x00\x04\x02\x02\x00", 6);          /* transport and content protection... */
	memset(g_msg + 8, 0, 100);
	for (int i = 0; i < 12; i++) { g_msg[8 + 6 * i] = 0x05; g_msg[9 + 6 * i] = 0x04; }   /* ... and 12 elements of category 5, 6 bytes each (72) */
	memcpy(g_msg + 8 + 72, k_caps_aac6 + 2, 14);
	g_msg[0] = 0x12; g_msg[1] = 0x02;
	conn[BTAAC_C_INDEX] = 0;
	memcpy(conn + BTAAC_C_LIST, "\x18\x08", 2);
	btaac_disp_hook(conn, g_chan, g_msg, 2 + 6 + 72 + 14);
	CHECK(!btaac_neg.have, "the AAC element past the 64 bytes that are read is not used");
	/* H2 declines in every other case, leaving the record exactly as the code made it */
	struct { const char *why; uint32_t flags; int state4; int own_buf; uint32_t len; int enable; int ban; } d[] = {
		{ "the acceptor path (flag 4)", 4, 1, 1, 14, 1, 0 }, { "another flag", 0, 1, 1, 14, 1, 0 }, { "a record not in state 4", 8, 0, 1, 14, 1, 0 },
		{ "another buffer than the record's", 8, 1, 0, 14, 1, 0 }, { "a length over the buffer", 8, 1, 1, 19, 1, 0 }, { "switched off since", 8, 1, 1, 14, 0, 0 },
		{ "a device that was marked", 8, 1, 1, 14, 1, 1 },
	};
	for (unsigned i = 0; i < sizeof(d) / sizeof(d[0]); i++) {
		static uint8_t scratch[32];
		uint8_t *rec = conn + BTAAC_C_REC_B, before[BTAAC_SLOT_SIZE];

		world(1, 1);
		start_negotiation(conn);
		ask_caps(conn, 2, k_caps_aac6, 16);
		CHECK(btaac_neg.have, "%s: set up", d[i].why);
		stock_narrows(conn);
		if (!d[i].state4)
			put32(rec, 0, 3);
		btaac_neg.enable = (uint32_t)d[i].enable;
		if (d[i].ban)
			btaac_ban((uintptr_t)conn);
		memcpy(before, conn, sizeof(before));
		btaac_h2_after(conn, d[i].flags, d[i].own_buf ? (void *)(rec + 6) : (void *)scratch, d[i].len);
		CHECK(!memcmp(before, conn, sizeof(before)) && btaac_neg.n_rewrite == 0 && btaac_neg.state == (d[i].flags == 8 ? BTAAC_N_NONE : BTAAC_N_PROBING),
		      "%s: the record is left alone (state %u)", d[i].why, (unsigned)btaac_neg.state);
	}
	/* a connection the AAC code never marked: H2 does nothing at all (the common case for every user of the connection quality hook) */
	{
		uint8_t before[BTAAC_SLOT_SIZE], *c0 = slot(0);

		world(0, 1);
		stock_narrows(c0);
		memcpy(before, c0, sizeof(before));
		btaac_h2_after(c0, 8, c0 + BTAAC_C_REC_B + 6, 14);
		btaac_h2_after(NULL, 8, NULL, 0);
		CHECK(!memcmp(before, c0, sizeof(before)) && btaac_neg.n_declined == 0 && btaac_neg.state == BTAAC_N_NONE, "an unmarked connection is not touched");
	}
}

static void t_fallback(void)
{
	uint8_t *conn = slot(4);
	uint8_t *rec = conn + BTAAC_C_REC_B;

	world(4, 1);
	put32(g_chan, 0x38, 0);
	*(uint16_t *)(g_chan + 0x38) = 0x0043;                      /* the signalling channel's number */
	start_negotiation(conn);
	all_caps(conn);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, rec + 6, 14);
	CHECK(btaac_neg.state == BTAAC_N_PENDING && rec[5] == 0x18, "pending");
	/* the sink rejects: the original runs first (it ignores a reject of this signal), then the SBC choice is sent again */
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x13", 2) == g_disp_ret, "the original's answer comes back");
	CHECK(btaac_neg.state == BTAAC_N_FALLBACK && btaac_neg.n_reject == 1 && btaac_neg.n_resend == 1 && btaac_neg.n_resend_none == 0, "fallback: one resend");
	CHECK(g_l2.calls == 1 && g_l2.conn == conn && g_l2.cid == 0x43 && g_l2.fmt == (const void *)0x1234 && g_l2.label == 0x30 && g_l2.sig == 3 && g_l2.acp == 0x14 &&
	      g_l2.intr == 0x04 && g_l2.len == 14 && !memcmp(g_l2.caps, k_sbc_cfg, 14) && g_l2.caps_is_rec,
	      "the call the code itself makes: channel %x, label %x, signal %u, endpoints %x %x, %u bytes, the record's own buffer %d", g_l2.cid, g_l2.label, (unsigned)g_l2.sig,
	      g_l2.acp, g_l2.intr, (unsigned)g_l2.len, g_l2.caps_is_rec);
	CHECK(rec[5] == 0x14 && get32(rec, 0x18) == 14 && !memcmp(rec + 6, k_sbc_cfg, 14) && get32(rec, 0) == 4, "the record is the code's own choice again, state 4 as it was");
	/* the answer to the second SET_CONFIGURATION is the code's: another reject is not resent (no loop) */
	CHECK(send_disp(conn, 3, 3, (const uint8_t *)"\x01\x13", 2) == g_disp_ret && g_l2.calls == 1, "no second resend");
	CHECK(send_disp(conn, 2, 3, (const uint8_t *)"", 0) == g_disp_ret && btaac_neg.state == BTAAC_N_FALLBACK, "the accept of the SBC configuration leaves it SBC");
	/* the device is not offered AAC again this boot: the next connection of it reads its endpoints no more */
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_NONE && btaac_banned((uintptr_t)conn), "a marked device stays SBC");
	/* a different device is still offered it */
	conn[BTAAC_C_ADDR_HI] = 0x77;
	start_negotiation(conn);
	conn[BTAAC_C_ADDR_HI] = 0x77;
	CHECK(btaac_neg.state == BTAAC_N_PROBING || btaac_neg.state == BTAAC_N_NONE, "(the second device)");
	/* no sender for the frame, or an unreadable channel: the reject is still handled (the state moves on), nothing is called */
	world(4, 1);
	btaac_neg.l2_send = 0;
	start_negotiation(conn);
	all_caps(conn);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, rec + 6, 14);
	send_disp(conn, 3, 3, (const uint8_t *)"\x01\x13", 2);
	CHECK(btaac_neg.state == BTAAC_N_FALLBACK && g_l2.calls == 0 && btaac_neg.n_resend_none == 1, "no frame sender: no resend, counted");
	world(4, 1);
	start_negotiation(conn);
	all_caps(conn);
	stock_narrows(conn);
	btaac_h2_after(conn, 8, rec + 6, 14);
	g_unmapped_lo = (uintptr_t)g_chan;
	g_unmapped_hi = (uintptr_t)g_chan + 0x1000;
	send_disp(conn, 3, 3, (const uint8_t *)"\x01\x13", 2);
	CHECK(g_l2.calls == 0 && btaac_neg.n_resend_none == 1, "an unreadable channel: no resend");
	/* ban table: more devices than slots go round */
	world(4, 1);
	for (uint32_t i = 0; i < 6; i++) {
		conn[BTAAC_C_ADDR_LO] = (uint8_t)(i + 1);
		btaac_ban((uintptr_t)conn);
	}
	conn[BTAAC_C_ADDR_LO] = 6;
	CHECK(btaac_banned((uintptr_t)conn), "the newest is kept");
	conn[BTAAC_C_ADDR_LO] = 1;
	CHECK(!btaac_banned((uintptr_t)conn), "the oldest went round");
}

/* A mark is for one codec: the device that rejected AAC is still offered aptX and LDAC, the one that rejected aptX is still offered AAC, and each mark is made by the
 * codec of the connection that failed. A failure of the module's own (the install, the check) never marks a device. */
static void t_ban_per_codec(void)
{
	uint8_t *conn = slot(4);
	uintptr_t c = (uintptr_t)conn;

	world(4, 1);
	start_negotiation(conn);
	btaac_ban_for(c, BTAAC_BAN_AAC);
	CHECK(btaac_neg.ban_n == 1 && btaac_neg.ban_cdc[0] == BTAAC_BAN_AAC && btaac_ban_bits(0x44332211u, 0x6655u) == BTAAC_BAN_AAC, "marked for AAC");
	CHECK(btaac_banned(c) && (btaac_neg.want_aptx = 1, !btaac_banned(c)) && (btaac_neg.want_aptx = 0, btaac_neg.want_ldac = 1, !btaac_banned(c)), "the mark counts for AAC alone");
	btaac_neg.want_ldac = 0;
	btaac_ban_for(c, BTAAC_BAN_APTX);
	CHECK(btaac_neg.ban_n == 1 && btaac_neg.ban_cdc[0] == (BTAAC_BAN_AAC | BTAAC_BAN_APTX), "a second codec joins the device's entry, no new one");
	CHECK(btaac_ban_bits(0x44332211u, 0x6655u) == (BTAAC_BAN_AAC | BTAAC_BAN_APTX) && btaac_ban_bits(0x44332212u, 0x6655u) == 0 && btaac_ban_bits(0x44332211u, 0x6656u) == 0, "found by the address only");
	/* the hooks: AAC marked and AAC wanted: SBC; AAC marked and aptX wanted: the endpoints are read, as for any device */
	world(4, 1);
	conn[BTAAC_C_ADDR_LO] = 0x11; conn[BTAAC_C_ADDR_LO + 1] = 0x22; conn[BTAAC_C_ADDR_LO + 2] = 0x33; conn[BTAAC_C_ADDR_LO + 3] = 0x44;
	conn[BTAAC_C_ADDR_HI] = 0x55; conn[BTAAC_C_ADDR_HI + 1] = 0x66;
	btaac_ban_for(c, BTAAC_BAN_AAC);
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_NONE, "AAC wanted, the device marked for AAC: SBC");
	btaac_neg.want_aptx = 1;
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_PROBING, "aptX wanted, the device marked for AAC only: its endpoints are read (state %u)", (unsigned)btaac_neg.state);
	btaac_neg.want_aptx = 0;
	btaac_neg.want_ldac = 1;
	start_negotiation(conn);
	CHECK(btaac_neg.state == BTAAC_N_PROBING, "LDAC wanted, the device marked for AAC only: its endpoints are read");
	/* the codec that failed is the connection's: a rejected aptX marks aptX, not AAC (the real reject is the fallback tests') */
	world(4, 1);
	conn[BTAAC_C_ADDR_LO] = 0x11; conn[BTAAC_C_ADDR_HI] = 0x55;
	btaac_neg.aptx = 1;
	btaac_ban_codec(c);
	CHECK(btaac_neg.ban_cdc[0] == BTAAC_BAN_APTX && btaac_neg.ban_n == 1, "aptX failed: marked for aptX");
	btaac_neg.aptx = 0; btaac_neg.ldac = 1;
	btaac_ban_codec(c);
	CHECK(btaac_neg.ban_cdc[0] == (BTAAC_BAN_APTX | BTAAC_BAN_LDAC) && btaac_neg.ban_n == 1, "LDAC failed: marked for LDAC as well");
	btaac_neg.ldac = 0;
	btaac_ban_codec(c);
	CHECK(btaac_neg.ban_cdc[0] == (BTAAC_BAN_APTX | BTAAC_BAN_LDAC | BTAAC_BAN_AAC), "AAC failed: marked for AAC as well");
	btaac_neg.aptx = btaac_neg.apt_hd = 1;
	world(4, 1);
	btaac_neg.aptx = btaac_neg.apt_hd = 1;
	btaac_ban_codec(c);
	CHECK(btaac_neg.ban_n == 0 && btaac_neg.hd_off, "an aptX HD failure marks nothing: HD goes off for the boot");
	/* the module's own marks: a separate set in the same entry; the headphones' bits never show them, and the offer is kept back by either */
	world(4, 1);
	conn[BTAAC_C_ADDR_LO] = 0x11; conn[BTAAC_C_ADDR_LO + 1] = 0x22; conn[BTAAC_C_ADDR_LO + 2] = 0x33; conn[BTAAC_C_ADDR_LO + 3] = 0x44;
	conn[BTAAC_C_ADDR_HI] = 0x55; conn[BTAAC_C_ADDR_HI + 1] = 0x66;
	btaac_local_for(c, BTAAC_BAN_AAC);
	CHECK(btaac_neg.ban_n == 1 && btaac_ban_bits(0x44332211u, 0x6655u) == 0 && btaac_local_bits(0x44332211u, 0x6655u) == BTAAC_BAN_AAC && btaac_banned(c), "a local mark for AAC: not a refusal, but not offered");
	CHECK((btaac_neg.want_aptx = 1, !btaac_banned(c)) && (btaac_neg.want_aptx = 0, btaac_neg.want_ldac = 1, !btaac_banned(c)), "the local mark counts for AAC alone");
	btaac_neg.want_ldac = 0;
	btaac_ban_for(c, BTAAC_BAN_APTX);
	CHECK(btaac_neg.ban_n == 1 && btaac_ban_bits(0x44332211u, 0x6655u) == BTAAC_BAN_APTX && btaac_local_bits(0x44332211u, 0x6655u) == BTAAC_BAN_AAC,
	      "the headphones' mark for aptX joins the entry and stays apart from the local one for AAC");
	CHECK(btaac_local_bits(0x44332212u, 0x6655u) == 0 && btaac_local_bits(0x44332211u, 0x6656u) == 0, "a local mark is found by the address only");
	world(4, 1);
	conn[BTAAC_C_ADDR_LO] = 0x11; conn[BTAAC_C_ADDR_HI] = 0x55;
	btaac_neg.aptx = 1;
	btaac_local_codec(c);
	CHECK(btaac_local_bits(0x11u, 0x55u) == BTAAC_BAN_APTX && btaac_ban_bits(0x11u, 0x55u) == 0, "aptX: a local mark for aptX");
	btaac_neg.aptx = 0; btaac_neg.ldac = 1;
	btaac_local_codec(c);
	CHECK(btaac_local_bits(0x11u, 0x55u) == (BTAAC_BAN_APTX | BTAAC_BAN_LDAC) && btaac_ban_bits(0x11u, 0x55u) == 0 && btaac_neg.ban_n == 1, "LDAC: a local mark for LDAC as well");
	world(4, 1);
	btaac_neg.aptx = btaac_neg.apt_hd = 1;
	btaac_local_codec(c);
	CHECK(btaac_neg.ban_n == 0 && btaac_neg.hd_off, "an aptX HD session failure marks nothing, locally either: HD goes off for the boot");
}

static void t_fallback_general_reject(void)
{
	uint8_t *c2 = slot(5);

	world(5, 1);
	*(uint16_t *)(g_chan + 0x38) = 0x0043;
	start_negotiation(c2);
	all_caps(c2);
	stock_narrows(c2);
	btaac_h2_after(c2, 8, c2 + BTAAC_C_REC_B + 6, 14);
	CHECK(btaac_neg.state == BTAAC_N_PENDING, "pending");
	CHECK(send_disp(c2, 1, 3, (const uint8_t *)"", 0) == g_disp_ret && btaac_neg.state == BTAAC_N_FALLBACK && g_l2.calls == 1 && btaac_banned((uintptr_t)c2),
	      "a general reject (the sink does not know the command) falls back and marks the device too");
}

/* ---- the media hook ------------------------------------------------------------------------------------------------- */

static uint8_t g_sbc[640];
static void media_world(uint32_t mtu, uint32_t neg_state)
{
	uint8_t *conn = slot(1);

	world(1, 1);
	btaac_neg.conn = (uintptr_t)conn;
	btaac_neg.state = neg_state;
	btaac_neg.send_gate = 1;
	btaac_neg.pcm_gate = 1;
	btaac_neg.ssrc = 0x56544101;
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = (uintptr_t)g_chan;
	put32(g_chan, 8, mtu);
	memset(g_sbc, 0x5B, sizeof(g_sbc));
}

static void t_media_passthrough(void)
{
	uint8_t *conn = slot(1);

	/* not an AAC stream: the original gets the call as it came, the engine is never asked for a unit */
	media_world(672, BTAAC_N_NONE);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1 && g_send_last.conn == conn && g_send_last.buf == g_sbc && g_send_last.len == 600 && g_send_last.flag == 1,
	      "SBC stream: every argument as it came, the original's answer back");
	media_world(672, BTAAC_N_FALLBACK);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1 && g_send_last.buf == g_sbc, "a stream that fell back to SBC is the code's own");
	media_world(672, BTAAC_N_PROBING);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1, "while the endpoints are read");
	/* another link than the AAC one */
	media_world(672, BTAAC_N_CONFIGURED);
	CHECK(btaac_send_hook(slot(2), g_sbc, 600, 1) == g_send_ret && g_send_last.buf == g_sbc, "another link's media is the code's own");
	/* no reference yet */
	media_world(672, BTAAC_N_NONE);
	btaac_send_ref = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0, "no reference yet: an error number, nothing sent");
	/* an AAC stream (or one being configured, or one that broke) never gets the code's SBC packet */
	static const uint32_t states[] = { BTAAC_N_PENDING, BTAAC_N_CONFIGURED, BTAAC_N_BROKEN };
	for (unsigned i = 0; i < 3; i++) {
		media_world(672, states[i]);
		btaac_neg.send_gate = 0;
		CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_swallowed == 1, "state %u with no session: the SBC packet is dropped", states[i]);
	}
	media_world(672, BTAAC_N_BROKEN);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0, "a broken AAC stream sends nothing even with the gate up");
	/* the hook counts itself in and out */
	CHECK(btaac_send_in == 0 && btaac_disp_in == 0, "nobody is left inside the hooks");
}

/* The sender's loop: a block of PCM through the hook the audio sender has, then the media send, every 10.67 ms; here as fast as the engine goes. */
static void run_sender(uint32_t blocks, uint32_t mtu_for_check)
{
	static int16_t blk[512 * 2];
	uint8_t *conn = slot(1);

	for (uint32_t b = 0; b < blocks; b++) {
		tone_block(b * 512, blk);
		btaac_pcm_block(blk);
		btaac_send_hook(conn, g_sbc, 600, 1);
		usleep(900);
	}
	(void)mtu_for_check;
}

/* Packets into elements: sequence, timestamp, marker, the limit, then the standard's reader on every element; returns the elements (whole, LOAS framed) in a file. */
static uint32_t collect(uint32_t mtu, const char *path, int *ok_stream)
{
	static uint8_t el[BTAAC_LATM_MAX], au[BTAAC_AU_MAX], loas[BTAAC_LATM_MAX + 3];
	uint32_t got = 0, units = 0, last_ts = 0, expect_seq = 0;
	FILE *f = path ? fopen(path, "wb") : NULL;

	*ok_stream = 1;
	for (int i = 0; i < g_npkts; i++) {
		const uint8_t *p = g_pkts[i].data;
		uint32_t len = g_pkts[i].len, ts = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
		uint16_t seq = (uint16_t)(p[2] << 8 | p[3]);

		CHECK(len > 12 && len <= mtu, "packet %d is %u bytes at a limit of %u", i, (unsigned)len, (unsigned)mtu);
		CHECK(p[0] == 0x80 && (p[1] & 0x7F) == 96, "RTP version 2, payload type 96");
		CHECK(seq == (uint16_t)expect_seq, "packet %d: sequence %u, expected %u (every packet counts, none is skipped)", i, seq, (unsigned)(uint16_t)expect_seq);
		expect_seq++;
		CHECK(!(p[8] == 0 && p[9] == 0 && p[10] == 0 && p[11] == 0), "an SSRC");
		if (got == 0 && units > 0)
			CHECK(ts >= last_ts + 1024 && (ts - last_ts) % 1024 == 0, "packet %d: timestamp %u after %u (1024 a unit)", i, (unsigned)ts, (unsigned)last_ts);
		else if (got > 0)
			CHECK(ts == last_ts, "a piece carries its unit's timestamp");
		last_ts = ts;
		memcpy(el + got, p + 12, len - 12);
		got += len - 12;
		if (p[1] & 0x80) {
			int n = latm_read(el, got, au, sizeof(au));

			CHECK(n > 0, "the element of unit %u reads back", (unsigned)units);
			if (f)
				fwrite(loas, 1, loas_wrap(loas, el, got), f);
			units++;
			got = 0;
		} else {
			CHECK(len == mtu, "a piece that is not the last fills the limit (%u of %u)", (unsigned)len, (unsigned)mtu);
		}
	}
	CHECK(got == 0, "the stream ends on an element's end (the marker)");
	if (f)
		fclose(f);
	return units;
}

static void media_session(uint32_t mtu, uint32_t blocks, const char *name)
{
	char path[128];
	int ok;

	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 264, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	media_world(mtu, BTAAC_N_CONFIGURED);
	run_sender(blocks, mtu);
	snprintf(path, sizeof(path), "build/test_media_%d.loas", (int)getpid());
	CHECK(g_send_calls > 0 && btaac_neg.n_sent > 20, "units were sent: %u", (unsigned)btaac_neg.n_sent);
	uint32_t units = collect(mtu, have_ffmpeg() ? path : NULL, &ok);

	CHECK(units == btaac_neg.n_sent, "%u elements in the packets, %u units counted", (unsigned)units, (unsigned)btaac_neg.n_sent);
	CHECK(btaac_neg.last_mtu == mtu && btaac_neg.n_pieces == (uint32_t)g_npkts && btaac_neg.n_send_err == 0, "counters: mtu %u, %u pieces for %d packets", (unsigned)btaac_neg.last_mtu, (unsigned)btaac_neg.n_pieces, g_npkts);
	CHECK(btaac_neg.n_no_au > 0, "blocks with nothing to send were counted (a unit is made every second block)");
	CHECK(btaac_neg.max_au_len > 400 && btaac_neg.max_au_len <= BTAAC_AU_MAX, "the longest unit %u bytes", (unsigned)btaac_neg.max_au_len);
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
	if (have_ffmpeg()) {
		uint32_t frames = 0;
		int16_t *pcm = ffmpeg_decode(path, &frames);

		CHECK(pcm, "%s: ffmpeg refused the packets' elements", name);
		check_decoded(pcm, frames, units, name);
		free(pcm);
		unlink(path);
	} else {
		printf("  (ffmpeg is not on this machine: %s checked against the standard's reader only)\n", name);
	}
	if (mtu < 700)
		CHECK(btaac_neg.n_pieces > btaac_neg.n_sent, "at a limit of %u some units are cut into pieces (%u pieces for %u units)", (unsigned)mtu, (unsigned)btaac_neg.n_pieces, (unsigned)btaac_neg.n_sent);
	else if (mtu >= 1100)
		CHECK(btaac_neg.n_pieces == btaac_neg.n_sent, "at a limit of %u no unit is cut", (unsigned)mtu);
}

static void t_media_672(void) { media_session(672, 400, "mtu 672"); }
static void t_media_1021(void) { media_session(1021, 400, "mtu 1021"); }
static void t_media_300(void) { media_session(300, 300, "mtu 300"); }
static void t_media_unbelievable_mtu(void)
{
	uint8_t *conn = slot(1);

	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 264, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	media_world(0xFFFF, BTAAC_N_CONFIGURED);
	run_sender(40, 0);
	CHECK(btaac_neg.n_sent > 3 && btaac_neg.last_mtu == 672, "a limit of 65,535 is not believed: 672 (%u)", (unsigned)btaac_neg.last_mtu);
	/* no media channel, or an unreadable one: the default */
	world(1, 1);
	media_world(900, BTAAC_N_CONFIGURED);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = 0;
	g_npkts = 0;
	run_sender(40, 0);
	CHECK(btaac_neg.last_mtu == 672, "no media channel: 672");
	media_world(900, BTAAC_N_CONFIGURED);
	g_unmapped_lo = (uintptr_t)g_chan;
	g_unmapped_hi = (uintptr_t)g_chan + 0x1000;
	run_sender(20, 0);
	CHECK(btaac_neg.last_mtu == 672, "an unreadable channel: 672");
	engine_down();
}

static void t_media_edges(void)
{
	uint8_t *conn = slot(1);
	static int16_t blk[512 * 2];

	engine_up();
	run_selftest();
	/* no session running: nothing to send, an error number, the SBC packet never goes */
	media_world(672, BTAAC_N_CONFIGURED);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && btaac_neg.n_no_au == 1, "no unit: nothing sent, an error number (so the code counts nothing in flight)");
	/* the stream record says the media channel is not open: no unit is taken from the engine */
	CHECK(btaac_start(256, 48000, 2, 264, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	for (int i = 0; i < 12; i++) {
		tone_block((uint32_t)i * 512, blk);
		btaac_pcm_block(blk);
	}
	usleep(100000);
	put32(conn, BTAAC_C_REC_B, 6);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_send_calls == 0 && info().aus_popped == 0, "record not open: no send, no unit taken");
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	/* the code's send fails: the unit is lost (counted), the hook says so, and the next works */
	g_send_fail_at = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && btaac_neg.n_send_err == 1 && btaac_neg.n_sent == 0, "the code refused the packet");
	g_send_fail_at = -1;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) >= 0 && btaac_neg.n_sent == 1, "the next unit is sent");
	/* the PCM goes in only while the stream is started */
	uint32_t in = info().pcm_frames_in;
	put32(conn, BTAAC_C_PROFILES, 0);
	btaac_pcm_block(blk);
	CHECK(info().pcm_frames_in == in && btaac_neg.n_pcm_blocks >= 1, "a stream that is not started is not fed");
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	engine_down();
	/* after the engine stopped: the hooks do not fail (a unit the session left in the ring may still go; the worker has closed the gates before it ends one) */
	btaac_send_hook(conn, g_sbc, 600, 1);
	uint32_t after = btaac_neg.n_pcm_blocks;
	btaac_pcm_block(blk);
	CHECK(btaac_neg.n_pcm_blocks == after, "no more input is accepted once the engine stopped");
}

/* ---- the acceptor path: the headphones open the stream -------------------------------------------------------------------- */

/* the code's frame sender as the replies call it (7 arguments), and a model of the code's own acceptor: its two endpoints by raw byte, record B (endpoint 1) the source, the
 * states of text 0xA54C-0xAA5A (idle 1, configured 5, open sent 6, open 8, started 10), the rejects it would send counted by kind */
typedef struct { void *conn; uint32_t cid; uintptr_t fmt; uint32_t b0, sig; uintptr_t a; uint32_t b; uint8_t data[32]; } Rep;
static Rep g_rep[16];
static int g_nrep;
static int fake_l2_reply(void *conn, uint32_t cid, const void *fmt, uint32_t b0, uint32_t sig, uintptr_t a, uint32_t b)
{
	Rep *r = &g_rep[g_nrep++];

	CHECK(g_nrep <= 16, "too many replies");
	r->conn = conn; r->cid = cid; r->fmt = (uintptr_t)fmt; r->b0 = b0; r->sig = sig; r->a = a; r->b = b;
	if ((uintptr_t)fmt == 0x2345 && a && b <= sizeof(r->data))
		memcpy(r->data, (const void *)a, b);
	return 0;
}
static struct { int calls, n_discover, n_getcaps, n_conf, n_unknown, n_inuse, n_badstate, n_a; const uint8_t *ptr; int len; uint8_t msg[32]; } g_stock;
static int fake_disp_stock(void *connp, void *chan, const uint8_t *msg, int len)
{
	uint8_t *conn = connp, *rb = conn + BTAAC_C_REC_B;
	uint32_t sig = msg[1] & 0x3F, seid = msg[2], st = get32(rb, 0);

	g_stock.calls++; g_stock.ptr = msg; g_stock.len = len;
	memcpy(g_stock.msg, msg, len < 32 ? (size_t)len : 32u);
	if ((msg[0] & 3) != 0)
		return g_disp_ret;                       /* a reply: nothing for the model to do */
	if (sig == 1) { g_stock.n_discover++; return g_disp_ret; }
	if (len < 3 || (seid != 4 && seid != 8)) { g_stock.n_unknown++; return g_disp_ret; }   /* BAD_ACP_SEID (0x12) */
	if (seid == 8) { g_stock.n_a++; return g_disp_ret; }
	switch (sig) {
	case 2: g_stock.n_getcaps++; break;
	case 3:
		if (st != 1) { g_stock.n_inuse++; break; }                                     /* SEP_IN_USE (0x13) */
		rb[5] = msg[3]; put32(rb, 0x18, (uint32_t)len - 4u); memcpy(rb + 6, msg + 4, (size_t)len - 4u); put32(rb, 0, 5); g_stock.n_conf++;
		break;
	case 6: if (st == 5) put32(rb, 0, 6); else g_stock.n_badstate++; break;
	case 7: if (st == 8 || st == 9) put32(rb, 0, 10); else g_stock.n_badstate++; break;
	case 9: if (st == 10) put32(rb, 0, 8); else g_stock.n_badstate++; break;
	case 8: if (st == 8 || st == 10) put32(rb, 0, 1); else g_stock.n_badstate++; break;
	case 10: put32(rb, 0, 1); break;
	}
	return g_disp_ret;
}

static uint8_t *g_aconn;
static void acc_world(int enable)
{
	g_aconn = slot(1);
	world(1, enable);
	btaac_disp_ref = (tai_hook_ref_t)fake_disp_stock;
	btaac_neg.l2_send = (uintptr_t)fake_l2_reply;
	*(uint16_t *)(g_chan + 0x38) = 0x0043;
	memset(&g_stock, 0, sizeof(g_stock));
	memset(g_rep, 0, sizeof(g_rep));
	g_nrep = 0;
	put32(g_aconn, BTAAC_C_REC_B, 1);                 /* the code's records start idle, with their own endpoint bytes */
	g_aconn[BTAAC_C_REC_B + 4] = 0x04;
	g_aconn[0x3B4C + 4] = 0x08;
	g_aconn[BTAAC_C_ADDR_LO] = 0x11; g_aconn[BTAAC_C_ADDR_HI] = 0x55;
}
static int send_cmd(uint32_t label, uint32_t sig, const uint8_t *body, int n)    /* a command from the headphones through the hook */
{
	g_msg[0] = (uint8_t)(label << 4);
	g_msg[1] = (uint8_t)sig;
	memcpy(g_msg + 2, body, (size_t)n);
	return btaac_disp_hook(g_aconn, g_chan, g_msg, n + 2);
}
static int cmd1(uint32_t label, uint32_t sig, uint8_t seid) { return send_cmd(label, sig, &seid, 1); }
#define REC_STATE() get32(g_aconn, BTAAC_C_REC_B)
/* the configuration the reference headset would choose for endpoint 3: MPEG-2 LC, 48 kHz, stereo, its highest rate in the bit rate field */
static int send_conf(uint32_t label, uint8_t acp, uint8_t int_seid, const uint8_t *caps, int n)
{
	uint8_t b[2 + 16 + 8];

	CHECK(n <= 22, "test message size");
	b[0] = acp; b[1] = int_seid;
	memcpy(b + 2, caps, (size_t)n);
	return send_cmd(label, 3, b, n + 2);
}
static const uint8_t k_aac_cfg[12] = { 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0x80, 0x00, 0x84, 0x04, 0x09, 0xB6 };   /* 264,630 b/s in the bit rate field */
static const uint8_t k_subst[14] = { 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x11, 0x15, 0x02, 0x35 };   /* (its first four bytes are the message's own) */

/* the reference headset connecting itself: every message it sends and every answer */
static void t_acc_flow(void)
{
	uint8_t *conn;

	acc_world(1);
	conn = g_aconn;
	/* DISCOVER: answered here, the code's own endpoint 1 and 2 first, endpoint 3 (an AAC source, free) after them; the code does not see it */
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == 0 && g_stock.calls == 0 && g_nrep == 1, "DISCOVER is answered here (the original did not run, %d calls)", g_stock.calls);
	CHECK(g_rep[0].conn == conn && g_rep[0].cid == 0x43 && g_rep[0].fmt == 0x2345 && g_rep[0].b0 == 0x52 && g_rep[0].sig == 1 && g_rep[0].b == 6 &&
	      !memcmp(g_rep[0].data, "\x04\x00\x08\x08\x0C\x00", 6), "the accept: label 5, signal 1, '04 00 08 08 0C 00': endpoint 1 and 2 as the code lists them, then endpoint 3");
	CHECK(btaac_neg.state == BTAAC_N_OFFERED && btaac_neg.conn == (uintptr_t)conn && btaac_neg.init == BTAAC_INIT_HEADSET && btaac_neg.acceptor && btaac_neg.acc_flags == BTAAC_ACC_ADVERTISED &&
	      btaac_neg.n_acc_discover == 1 && btaac_neg.n_discover == 1 && !btaac_neg.send_gate, "offered, opened by the headphones");
	/* GET_CAPABILITIES: the code's own endpoints pass untouched (same pointer, same length), endpoint 3 is answered here */
	CHECK(cmd1(6, 2, 0x04) == g_disp_ret && g_stock.calls == 1 && g_stock.ptr == g_msg && g_stock.len == 3 && g_stock.n_getcaps == 1 && g_nrep == 1, "endpoint 1's capabilities: the code's own");
	CHECK(cmd1(7, 2, 0x08) == g_disp_ret && g_stock.calls == 2 && g_stock.n_a == 1 && g_nrep == 1, "endpoint 2's capabilities: the code's own");
	CHECK(cmd1(8, 2, 0x0C) == 0 && g_stock.calls == 2 && g_nrep == 2 && g_rep[1].fmt == 0x2345 && g_rep[1].b0 == 0x82 && g_rep[1].sig == 2 && g_rep[1].b == 12, "endpoint 3's capabilities: answered here");
	{
		BtAacSink sk;

		CHECK(btaac_sep_scan(g_rep[1].data + 2, 10, &sk) && sk.object == BTAAC_OBJ_MPEG2_LC && sk.max_bps == 320000 && !sk.vbr, "the reply scans as AAC LC, 48 kHz stereo, up to 320 kb/s, constant rate");
		CHECK(!memcmp(g_rep[1].data, "\x01\x00\x07\x08\x00\x02\xC0\x00\x84\x04\xE2\x00", 12), "the bytes: transport, then MPEG-2 and MPEG-4 LC, 48 kHz, 2 channels");
	}
	CHECK(btaac_neg.acc_flags == (BTAAC_ACC_ADVERTISED | BTAAC_ACC_ASKED) && btaac_neg.n_acc_caps == 1, "asked");
	/* SET_CONFIGURATION for endpoint 3 (the headphones' own endpoint 5: byte 0x14): the code gets one for its endpoint 1, SBC, and accepts it */
	CHECK(send_conf(9, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_stock.calls == 3 && g_stock.n_conf == 1 && g_nrep == 2, "SET_CONFIGURATION: the code ran, no reject sent here");
	CHECK(g_stock.ptr != g_msg && g_stock.len == 14 && g_stock.msg[0] == 0x90 && g_stock.msg[1] == 0x03 && g_stock.msg[2] == 0x04 && g_stock.msg[3] == 0x14 &&
	      !memcmp(g_stock.msg + 4, k_subst + 4, 10), "what the code was given: endpoint 1, the sink's own endpoint byte and label, SBC 48 kHz joint stereo 16 blocks 8 subbands bitpool 2-53");
	{
		const uint8_t *rec = conn + BTAAC_C_REC_B;
		static const uint8_t want[12] = { 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0x80, 0x00, 0x84, 0x03, 0xE8, 0x00 };   /* 256,000 b/s: ours, under their 264,630 */

		CHECK(get32(rec, 0) == 5 && rec[5] == 0x14 && get32(rec, 0x18) == 12 && !memcmp(rec + 6, want, 12), "the record: configured, the sink's endpoint, the 12 AAC bytes");
		CHECK(btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.seid == 0x14 && btaac_neg.object == BTAAC_OBJ_MPEG2_LC && btaac_neg.max_bps == 264630 && btaac_neg.bps == 256000 &&
		      !memcmp((const void *)btaac_neg.config, want, 12) && btaac_neg.have && btaac_neg.n_acc_config == 1 && (btaac_neg.acc_flags & BTAAC_ACC_CONFIGURED) &&
		      btaac_neg.n_acc_stock_no == 0, "configured: object, rates, flags, counters");
	}
	/* OPEN, then the media channel (the test: the code's record goes to open), START, SUSPEND, START, CLOSE: each reaches the code as endpoint 1, byte for byte otherwise */
	CHECK(cmd1(0xA, 6, 0x0C) == g_disp_ret && g_stock.calls == 4 && g_stock.msg[0] == 0xA0 && g_stock.msg[1] == 6 && g_stock.msg[2] == 4 && g_stock.len == 3 && REC_STATE() == 6, "OPEN as endpoint 1");
	put32(conn, BTAAC_C_REC_B, 8);
	CHECK(cmd1(0xB, 7, 0x0C) == g_disp_ret && g_stock.msg[0] == 0xB0 && g_stock.msg[1] == 7 && g_stock.msg[2] == 4 && REC_STATE() == 10, "START as endpoint 1: streaming");
	CHECK(cmd1(0xC, 9, 0x0C) == g_disp_ret && g_stock.msg[1] == 9 && g_stock.msg[2] == 4 && REC_STATE() == 8, "SUSPEND");
	CHECK(cmd1(0xD, 7, 0x0C) == g_disp_ret && REC_STATE() == 10, "START again");
	/* a START that lists two endpoints: the rest of the list is kept */
	{
		static const uint8_t two[2] = { 0x0C, 0x08 };

		CHECK(send_cmd(0xE, 9, two, 2) == g_disp_ret && g_stock.len == 4 && g_stock.msg[2] == 4 && g_stock.msg[3] == 0x08 && REC_STATE() == 8, "SUSPEND of two endpoints: the first translated, the second as it came");
	}
	CHECK(cmd1(0xF, 8, 0x0C) == g_disp_ret && g_stock.msg[1] == 8 && g_stock.msg[2] == 4 && REC_STATE() == 1 && g_stock.n_badstate == 0, "CLOSE: the record idle again");
	CHECK(btaac_neg.n_acc_cmd == 6 && btaac_neg.state == BTAAC_N_CONFIGURED && g_stock.n_unknown == 0, "six commands handed on, none unknown to the code, still AAC");
	/* the sink opens again after a CLOSE: a new SET_CONFIGURATION for endpoint 3 on the same connection (the record is idle) is taken the same way */
	CHECK(send_conf(1, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_stock.n_conf == 2 && REC_STATE() == 5 && btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.n_acc_config == 2, "configured again after a CLOSE");
	CHECK(cmd1(2, 10, 0x0C) == g_disp_ret && REC_STATE() == 1 && g_stock.msg[2] == 4, "ABORT as endpoint 1");
	/* endpoint 1 and 2 commands during all this are the code's own, as they came */
	CHECK(cmd1(3, 6, 0x04) == g_disp_ret && g_stock.ptr == g_msg && g_stock.len == 3, "a command for endpoint 1 is passed as it came");
	/* the headphones disconnect and reconnect: the next DISCOVER starts over */
	g_msg[0] = 0x40; g_msg[1] = 0x01;
	btaac_neg.send_gate = btaac_neg.pcm_gate = 1;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == 0 && btaac_neg.state == BTAAC_N_OFFERED && !btaac_neg.send_gate && !btaac_neg.pcm_gate && btaac_neg.acc_flags == BTAAC_ACC_ADVERTISED &&
	      btaac_neg.n_acc_discover == 2, "a new DISCOVER: offered again, the gates closed, what happened on the last connection forgotten");
	CHECK(btaac_disp_in == 0, "nobody left inside the hook");
}

/* every configuration the hook refuses itself: rejected with the right category and error, the code never sees it, the record is not touched, and a good one after it is taken */
static void t_acc_rejects(void)
{
	uint8_t *conn;
	uint8_t c[24];
	static const struct { const char *what; uint8_t cfg[16]; int n; uint32_t cat, err; } bad[] = {
		{ "MPEG-2 and MPEG-4 both set", { 1, 0, 7, 8, 0, 2, 0xC0, 0, 0x84, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "no object type", { 1, 0, 7, 8, 0, 2, 0x00, 0, 0x84, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "long term prediction", { 1, 0, 7, 8, 0, 2, 0x20, 0, 0x84, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "44.1 kHz", { 1, 0, 7, 8, 0, 2, 0x80, 0x01, 0x04, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "48 kHz and 44.1 kHz", { 1, 0, 7, 8, 0, 2, 0x80, 0x01, 0x84, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "96 kHz", { 1, 0, 7, 8, 0, 2, 0x80, 0x00, 0x14, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "mono", { 1, 0, 7, 8, 0, 2, 0x80, 0x00, 0x88, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "both channel counts", { 1, 0, 7, 8, 0, 2, 0x80, 0x00, 0x8C, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "SBC", { 1, 0, 7, 6, 0, 0, 0x11, 0x15, 2, 0x35 }, 10, 7, 0x29 },
		{ "a video codec", { 1, 0, 7, 8, 0x10, 2, 0x80, 0x00, 0x84, 4, 0xE2, 0 }, 12, 7, 0x29 },
		{ "content protection", { 1, 0, 4, 2, 2, 0, 7, 8, 0, 2, 0x80, 0, 0x84, 4, 0xE2, 0 }, 16, 4, 0x17 },
		{ "delay reporting", { 1, 0, 7, 8, 0, 2, 0x80, 0, 0x84, 4, 0xE2, 0, 8, 0 }, 14, 8, 0x17 },
		{ "an element too long for its category", { 1, 0, 7, 9, 0, 2, 0x80, 0, 0x84, 4, 0xE2, 0 }, 12, 7, 0x11 },
		{ "an element running past the end", { 1, 0, 7, 8, 0, 2, 0x80, 0, 0x84, 4 }, 10, 7, 0x11 },
		{ "a transport with a body", { 1, 1, 0, 7, 8, 0, 2, 0x80, 0, 0x84, 4, 0xE2, 0 }, 13, 1, 0x11 },
		{ "two media codec elements", { 7, 8, 0, 2, 0x80, 0, 0x84, 4, 0xE2, 0, 7, 8 }, 12, 7, 0x11 },
		{ "no transport", { 7, 8, 0, 2, 0x80, 0, 0x84, 4, 0xE2, 0 }, 10, 1, 0x1A },
		{ "no codec", { 1, 0 }, 2, 7, 0x1A },
		{ "a trailing byte", { 1, 0, 7, 8, 0, 2, 0x80, 0, 0x84, 4, 0xE2, 0, 0 }, 13, 0, 0x11 },
	};

	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	g_nrep = 0;
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		uint8_t before[BTAAC_SLOT_SIZE];

		memcpy(before, conn, sizeof(before));
		g_nrep = 0;
		CHECK(send_conf(0xC, 0x0C, 0x14, bad[i].cfg, bad[i].n) == 0 && g_stock.calls == 0 && g_nrep == 1, "%s: rejected here, the code did not run", bad[i].what);
		CHECK(g_rep[0].fmt == 0x3456 && g_rep[0].b0 == 0xC3 && g_rep[0].sig == 3 && g_rep[0].a == bad[i].cat && g_rep[0].b == bad[i].err && g_rep[0].cid == 0x43,
		      "%s: reject '1111': label C, signal 3, category %u error 0x%x (got %u 0x%x)", bad[i].what, bad[i].cat, bad[i].err, (unsigned)g_rep[0].a, g_rep[0].b);
		CHECK(!memcmp(before, conn, sizeof(before)) && btaac_neg.state == BTAAC_N_OFFERED, "%s: the record and the state are as they were", bad[i].what);
	}
	CHECK(btaac_neg.n_acc_reject == sizeof(bad) / sizeof(bad[0]) && btaac_neg.n_acc_config == 0, "counted");
	/* the code's own limit on the length (16 bytes of capabilities), and a message too short to hold a configuration */
	memset(c, 0, sizeof(c));
	g_nrep = 0;
	g_msg[0] = 0x60; g_msg[1] = 3; g_msg[2] = 0x0C; g_msg[3] = 0x14;
	memcpy(g_msg + 4, k_aac_cfg, 12);
	memset(g_msg + 16, 0, 8);
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 21) == 0 && g_nrep == 1 && g_rep[0].b == 0x11 && g_rep[0].a == 0 && g_stock.calls == 0, "17 bytes of capabilities: BAD_LENGTH like the code's own");
	g_nrep = 0;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 3) == 0 && g_nrep == 1 && g_rep[0].b == 0x11 && g_stock.calls == 0, "no capabilities at all: BAD_LENGTH");
	g_nrep = 0;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 200) == 0 && g_nrep == 1 && g_rep[0].b == 0x11 && g_stock.calls == 0, "a long message: BAD_LENGTH, not read");
	/* a good one now */
	g_nrep = 0;
	CHECK(send_conf(0xD, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_nrep == 0 && btaac_neg.state == BTAAC_N_CONFIGURED && REC_STATE() == 5, "a correct configuration after the refusals is taken");
	/* MPEG-4 LC alone, and no bit rate stated (0): ours, 256 kb/s */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	{
		static const uint8_t m4[12] = { 1, 0, 7, 8, 0, 2, 0x40, 0, 0x84, 0, 0, 0 };

		CHECK(send_conf(1, 0x0C, 0x24, m4, 12) == g_disp_ret && btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.object == BTAAC_OBJ_MPEG4_LC && btaac_neg.max_bps == 0 && btaac_neg.bps == 256000 &&
		      conn[BTAAC_C_REC_B + 5] == 0x24 && conn[BTAAC_C_REC_B + 12] == 0x40, "MPEG-4 LC with no rate stated: 256 kb/s, the record says MPEG-4 and the sink's endpoint byte");
	}
	/* the code refuses what we handed it (the record is in use): its reject went to the sink, nothing of ours is written, the state is not AAC */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	put32(conn, BTAAC_C_REC_B, 5);                 /* already configured: the code answers SEP_IN_USE */
	conn[BTAAC_C_REC_B + 5] = 0x14;                /* (even with the same endpoint byte) */
	CHECK(send_conf(1, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_stock.n_inuse == 1 && btaac_neg.state == BTAAC_N_OFFERED && btaac_neg.n_acc_stock_no == 1 && btaac_neg.n_acc_config == 0 &&
	      REC_STATE() == 5 && conn[BTAAC_C_REC_B + 6] == 0, "the code said no: still offered, the record untouched, counted");
}

/* ---- Music Source Only: the Vita offers sources only -------------------------------------------------------------------- */

/* the SBC source endpoint's configuration, as a headphone-side device would send it to endpoint 1 (and as a source would send it to the sink, endpoint 2) */
static const uint8_t k_sbc_cfg2[14] = { 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x21, 0x15, 0x02, 0x35, 0x04, 0x02, 0x02, 0x00 };

static void src_discover_check(const char *what, const uint8_t *want, uint32_t n)
{
	g_nrep = 0;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(g_aconn, g_chan, g_msg, 2) == 0 && g_stock.calls == 0 && g_nrep == 1 && g_rep[0].fmt == 0x2345 && g_rep[0].b0 == 0x52 && g_rep[0].sig == 1 && g_rep[0].b == n &&
	      !memcmp(g_rep[0].data, want, n), "%s: DISCOVER answered here with the endpoint list %02x %02x... (%u bytes), label 5, the code did not run", what, want[0], want[1], (unsigned)n);
}

static void t_src_only(void)
{
	static const uint8_t l_sbc[2] = { 0x04, 0x00 }, l_aac[4] = { 0x04, 0x00, 0x0C, 0x00 }, l_apt[4] = { 0x04, 0x00, BTAPTX_SEID_BYTE, 0x00 }, l_ldac[4] = { 0x04, 0x00, BTLDAC_SEID_BYTE, 0x00 };
	uint8_t *conn;

	/* an SBC connection (no codec on offer): the reply lists endpoint 1 alone; no negotiation is begun, nothing of the code's is touched */
	acc_world(0);
	btaac_neg.src_only = 1;
	conn = g_aconn;
	src_discover_check("SBC, sources only", l_sbc, 2);
	CHECK(btaac_neg.state == BTAAC_N_NONE && btaac_neg.n_discover == 0 && !btaac_neg.acceptor && btaac_neg.n_src_discover == 1 && btaac_neg.n_acc_discover == 0, "no negotiation was begun, counted");
	/* a source that configures the sink (endpoint 2, byte 0x08): rejected here with BAD_ACP_SEID, category 0, its own label; the code never sees it, so its sink record stays idle */
	g_nrep = 0;
	CHECK(send_conf(0xB, 0x08, 0x10, k_sbc_cfg2, 14) == 0 && g_stock.calls == 0 && g_stock.n_a == 0 && g_nrep == 1, "SET_CONFIGURATION of the sink: rejected here, the code did not run");
	CHECK(g_rep[0].fmt == 0x3456 && g_rep[0].b0 == 0xB3 && g_rep[0].sig == 3 && g_rep[0].a == 0 && g_rep[0].b == 0x12 && g_rep[0].cid == 0x43, "the reject '1111': label B, signal 3, category 0, BAD_ACP_SEID (0x12)");
	CHECK(btaac_neg.n_src_reject == 1 && btaac_neg.state == BTAAC_N_NONE, "counted, no state of ours");
	/* the sink's other commands, and endpoint 1's: the code's own (with the sink never configured its record is idle, so it rejects OPEN, START... with its state error) */
	g_nrep = 0;
	CHECK(cmd1(0xC, 6, 0x08) == g_disp_ret && g_stock.calls == 1 && g_stock.n_a == 1 && g_nrep == 0, "OPEN of the sink: the code's (it rejects: its record is idle)");
	CHECK(cmd1(0xD, 2, 0x04) == g_disp_ret && g_stock.n_getcaps == 1, "endpoint 1's capabilities: the code's");
	CHECK(send_conf(0xE, 0x04, 0x14, k_sbc_cfg2, 14) == g_disp_ret && g_stock.n_conf == 1 && g_nrep == 0 && REC_STATE() == 5, "SET_CONFIGURATION of endpoint 1 (the source role): the code's, accepted as always");
	/* a DISCOVER accept, a fragment, a short or unreadable message: not touched */
	g_msg[0] = 0x12; g_msg[1] = 0x01; memcpy(g_msg + 2, k_discover, 8);
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 10) == g_disp_ret && g_stock.ptr == g_msg && g_nrep == 0, "a DISCOVER reply (the Vita is the initiator): the code's");
	g_msg[0] = 0x14; g_msg[1] = 0x03; g_msg[2] = 0x08;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 3) == g_disp_ret && g_stock.ptr == g_msg && g_nrep == 0, "a fragment naming the sink: the code's");
	g_msg[0] = 0x60; g_msg[1] = 0x03;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == g_disp_ret && g_stock.ptr == g_msg && g_nrep == 0, "a SET_CONFIGURATION with no endpoint: the code's (BAD_LENGTH)");
	/* another message on an unreadable page: the code's */
	g_unmapped_lo = (uintptr_t)g_msg;
	g_unmapped_hi = (uintptr_t)g_msg + 0x1000;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 8) == g_disp_ret && g_nrep == 0, "an unreadable message: the code's");
	g_unmapped_lo = g_unmapped_hi = 0;

	/* the same with the setting off: everything stock (DISCOVER, the sink configuration) */
	acc_world(0);
	btaac_neg.src_only = 0;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(g_aconn, g_chan, g_msg, 2) == g_disp_ret && g_stock.calls == 1 && g_stock.n_discover == 1 && g_nrep == 0 && btaac_neg.n_src_discover == 0, "off: the code answers DISCOVER");
	CHECK(send_conf(0xB, 0x08, 0x10, k_sbc_cfg2, 14) == g_disp_ret && g_stock.calls == 2 && g_stock.n_a == 1 && g_nrep == 0 && btaac_neg.n_src_reject == 0, "off: the code gets the sink's configuration");

	/* a codec on offer: the list is endpoint 1 and the codec's endpoint, no sink (AAC, aptX, LDAC); the offer is made as before */
	acc_world(1);
	btaac_neg.src_only = 1;
	src_discover_check("AAC", l_aac, 4);
	CHECK(btaac_neg.state == BTAAC_N_OFFERED && btaac_neg.acceptor && btaac_neg.init == BTAAC_INIT_HEADSET && btaac_neg.acc_flags == BTAAC_ACC_ADVERTISED && btaac_neg.n_acc_discover == 1 &&
	      btaac_neg.n_src_discover == 1, "AAC offered as before (endpoint 3)");
	g_nrep = 0;
	CHECK(send_conf(0xB, 0x08, 0x10, k_sbc_cfg2, 14) == 0 && g_stock.calls == 0 && g_nrep == 1 && g_rep[0].fmt == 0x3456 && g_rep[0].b == 0x12 && btaac_neg.state == BTAAC_N_OFFERED,
	      "AAC offered: the sink's configuration is still rejected, the offer stands");
	g_nrep = 0;
	CHECK(send_conf(0xC, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && btaac_neg.state == BTAAC_N_CONFIGURED && REC_STATE() == 5 && g_nrep == 0, "and endpoint 3 is configured as before");
	acc_world(1);
	btaac_neg.src_only = 1; btaac_neg.want_aptx = 1;
	src_discover_check("aptX", l_apt, 4);
	acc_world(1);
	btaac_neg.src_only = 1; btaac_neg.want_ldac = 1;
	src_discover_check("LDAC", l_ldac, 4);
	/* the same codecs with the setting off: the code's two endpoints and ours (the lists that were always sent) */
	acc_world(1);
	btaac_neg.src_only = 0;
	src_discover_check("AAC, setting off", (const uint8_t *)"\x04\x00\x08\x08\x0C\x00", 6);

	/* a device marked for the codec (not offered): the plain source list, and a negotiation of the code's own kind is begun as before */
	acc_world(1);
	btaac_neg.src_only = 1;
	btaac_ban_codec((uintptr_t)g_aconn);
	src_discover_check("a marked device", l_sbc, 2);
	CHECK(btaac_neg.state == BTAAC_N_NONE && btaac_neg.n_discover == 1 && !btaac_neg.acceptor, "not offered, state SBC");

	/* a rediscovery on a live Vita-opened link: the list without the sink, nothing of ours changes */
	acc_world(1);
	btaac_neg.src_only = 1;
	conn = g_aconn;
	g_msg[0] = 0x12; g_msg[1] = 0x01; memcpy(g_msg + 2, k_discover, 8);
	btaac_disp_hook(conn, g_chan, g_msg, 10);
	btaac_neg.state = BTAAC_N_CONFIGURED;
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	btaac_neg.send_gate = btaac_neg.pcm_gate = 1;
	g_stock.calls = 0;   /* (the Vita's own DISCOVER reply above went to the code) */
	src_discover_check("a rediscovery", l_sbc, 2);
	CHECK(btaac_neg.state == BTAAC_N_CONFIGURED && btaac_neg.init == BTAAC_INIT_VITA && btaac_neg.send_gate && btaac_neg.pcm_gate && !btaac_neg.acceptor, "the state and the gates stay");

	/* no way to answer (no frame sender, an unreadable channel): the code's own reply stands, counted */
	acc_world(0);
	btaac_neg.src_only = 1;
	btaac_neg.l2_send = 0;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(g_aconn, g_chan, g_msg, 2) == g_disp_ret && g_stock.n_discover == 1 && btaac_neg.n_src_noreply == 1, "no frame sender: the code answers DISCOVER");
	CHECK(send_conf(0xB, 0x08, 0x10, k_sbc_cfg2, 14) == g_disp_ret && g_stock.n_a == 1 && btaac_neg.n_src_noreply == 2, "... and takes the sink's configuration (counted)");
	acc_world(1);
	btaac_neg.src_only = 1;
	g_unmapped_lo = (uintptr_t)g_chan;
	g_unmapped_hi = (uintptr_t)g_chan + 0x1000;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(g_aconn, g_chan, g_msg, 2) == g_disp_ret && g_stock.n_discover == 1 && g_nrep == 0 && btaac_neg.n_src_noreply == 1, "an unreadable channel: the code answers DISCOVER");
	g_unmapped_lo = g_unmapped_hi = 0;

	/* another connection that is not a link slot: not looked at */
	acc_world(0);
	btaac_neg.src_only = 1;
	{
		static uint8_t other[BTAAC_SLOT_SIZE];   /* (the code's model reads a slot's records) */

		memset(other, 0, sizeof(other));
		g_msg[0] = 0x50; g_msg[1] = 0x01;
		CHECK(btaac_disp_hook(other, g_chan, g_msg, 2) == g_disp_ret && g_stock.calls == 1 && g_nrep == 0, "not a link slot: the code's");
	}
}

/* an SBC-only sink, AAC turned off, a sink that remembers endpoint 3: everything the headphones send is the code's own, and a connection of SBC is untouched */
static void t_acc_sbc(void)
{
	uint8_t *conn;
	static const uint8_t sbc_cfg[14] = { 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x21, 0x15, 0x02, 0x35, 0x04, 0x02, 0x02, 0x00 };

	/* an SBC-only sink on a connection that was offered AAC: it asks endpoint 1, configures endpoint 1; the stream is SBC, the media hook passes the code's packets */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	CHECK(btaac_neg.state == BTAAC_N_OFFERED && g_nrep == 1, "offered");
	CHECK(cmd1(1, 2, 0x04) == g_disp_ret && g_stock.ptr == g_msg && g_stock.n_getcaps == 1 && g_nrep == 1, "it asks endpoint 1");
	CHECK(send_conf(2, 0x04, 0x14, sbc_cfg, 14) == g_disp_ret && g_stock.ptr == g_msg && g_stock.len == 18 && g_stock.n_conf == 1 && g_nrep == 1, "it configures endpoint 1: the code's own, as it came");
	CHECK(btaac_neg.state == BTAAC_N_OFFERED && REC_STATE() == 5 && !memcmp(conn + BTAAC_C_REC_B + 6, sbc_cfg, 14) && get32(conn, BTAAC_C_REC_B + 0x18) == 14, "the record is the SBC configuration, as the code made it");
	CHECK(cmd1(3, 6, 0x04) == g_disp_ret && g_stock.ptr == g_msg && cmd1(4, 7, 0x04) == g_disp_ret && g_stock.ptr == g_msg, "OPEN and START for endpoint 1: as they came");
	/* the media hook: the code's SBC packet goes out as it is */
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	put32(g_chan, 8, 672);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = (uintptr_t)g_chan;
	g_send_calls = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_last.buf == g_sbc && g_send_last.len == 600 && g_send_last.flag == 1, "the SBC packet is the code's own, every argument as it came");
	/* a command for endpoint 3 on it, though (a sink that configured SBC and then asks for AAC's OPEN): not ours, the code rejects the unknown endpoint */
	CHECK(cmd1(5, 6, 0x0C) == g_disp_ret && g_stock.ptr == g_msg && g_stock.n_unknown == 1, "OPEN for endpoint 3 on an SBC stream: the code's own (it rejects the unknown endpoint)");

	/* AAC off (the codec set to SBC, the module absent, the master switch off): DISCOVER is not read, nothing is offered, every command passes as it came */
	acc_world(0);
	conn = g_aconn;
	{
		uint8_t before[BTAAC_SLOT_SIZE];

		memcpy(before, conn, sizeof(before));
		g_msg[0] = 0x50; g_msg[1] = 0x01;
		CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == g_disp_ret && g_stock.calls == 1 && g_stock.ptr == g_msg && g_nrep == 0 && btaac_neg.state == BTAAC_N_NONE && btaac_neg.n_discover == 0, "AAC off: DISCOVER is the code's");
		CHECK(cmd1(1, 2, 0x0C) == g_disp_ret && g_stock.ptr == g_msg && g_stock.n_unknown == 1, "a sink that remembers endpoint 3 and asks for it: the code's own reject");
		CHECK(send_conf(2, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_stock.ptr == g_msg && g_stock.len == 16 && g_stock.n_unknown == 2 && g_nrep == 0, "... and its SET_CONFIGURATION: refused cleanly by the code");
		for (uint32_t sig = 6; sig <= 10; sig++)
			CHECK(cmd1(3, sig, 0x0C) == g_disp_ret && g_stock.ptr == g_msg && g_stock.len == 3, "signal %u for endpoint 3: as it came", sig);
		CHECK(!memcmp(before, conn, sizeof(before)) && btaac_neg.n_acc_cmd == 0 && btaac_neg.n_acc_discover == 0 && btaac_neg.state == BTAAC_N_NONE, "AAC off: not one byte of the connection written, nothing counted");
	}
	/* AAC offered, then switched off before the sink asks: the code answers (rejects the unknown endpoint), the state is not AAC */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	btaac_neg.enable = 0;
	CHECK(cmd1(1, 2, 0x0C) == g_disp_ret && g_stock.n_unknown == 1 && g_nrep == 1, "switched off after the offer: the capabilities of endpoint 3 are not answered");
	CHECK(send_conf(2, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_stock.n_unknown == 2 && g_nrep == 1 && btaac_neg.state == BTAAC_N_OFFERED && REC_STATE() == 1, "... nor configured: the record is the code's, untouched");
	/* a stream already AAC keeps working when the setting changes: its commands still reach the code as endpoint 1 */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	send_conf(2, 0x0C, 0x14, k_aac_cfg, 12);
	CHECK(btaac_neg.state == BTAAC_N_CONFIGURED, "configured");
	btaac_neg.enable = 0;
	CHECK(cmd1(3, 6, 0x0C) == g_disp_ret && g_stock.msg[2] == 4 && REC_STATE() == 6 && cmd1(4, 8, 0x0C) == g_disp_ret, "AAC switched off mid-stream: the stream's own commands are still translated");
	/* a sink that, after an AAC stream is closed, configures SBC on endpoint 1: the stream is SBC from then on (the media hook sends the code's packets) */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	send_conf(2, 0x0C, 0x14, k_aac_cfg, 12);
	btaac_neg.send_gate = btaac_neg.pcm_gate = 1;
	put32(conn, BTAAC_C_REC_B, 8);
	cmd1(3, 8, 0x0C);                                  /* CLOSE */
	CHECK(REC_STATE() == 1 && btaac_neg.state == BTAAC_N_CONFIGURED, "closed");
	CHECK(send_conf(4, 0x04, 0x14, sbc_cfg, 14) == g_disp_ret && g_stock.ptr == g_msg && btaac_neg.state == BTAAC_N_NONE && !btaac_neg.send_gate && !btaac_neg.pcm_gate && btaac_neg.n_acc_sbc == 1 &&
	      !memcmp(conn + BTAAC_C_REC_B + 6, sbc_cfg, 14), "SBC after AAC on one connection: the state is SBC, the gates closed, the record the code's");
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = (uintptr_t)g_chan;
	g_send_calls = 0;
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) == g_send_ret && g_send_calls == 1, "and its packets are the code's");
}

/* the hook can not answer (no frame sender, an unreadable channel), a Vita-opened connection, fragments, unreadable messages: nothing is changed, the code answers */
static void t_acc_edges(void)
{
	uint8_t *conn;

	acc_world(1);
	conn = g_aconn;
	btaac_neg.l2_send = 0;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == g_disp_ret && g_stock.calls == 1 && btaac_neg.state == BTAAC_N_NONE && btaac_neg.n_discover == 1 && !btaac_neg.acceptor, "no frame sender: the code answers DISCOVER (SBC only)");
	acc_world(1);
	conn = g_aconn;
	g_unmapped_lo = (uintptr_t)g_chan;
	g_unmapped_hi = (uintptr_t)g_chan + 0x1000;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == g_disp_ret && g_stock.calls == 1 && btaac_neg.state == BTAAC_N_NONE && g_nrep == 0, "an unreadable channel: the code answers DISCOVER");
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	g_unmapped_lo = (uintptr_t)g_chan;
	g_unmapped_hi = (uintptr_t)g_chan + 0x1000;
	CHECK(cmd1(1, 2, 0x0C) == g_disp_ret && g_stock.n_unknown == 1 && g_nrep == 1, "an unreadable channel: endpoint 3's capabilities are left to the code (it rejects the unknown endpoint)");
	CHECK(send_conf(2, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_stock.n_unknown == 2 && g_nrep == 1 && REC_STATE() == 1, "... and its configuration");
	g_unmapped_lo = g_unmapped_hi = 0;
	/* a message on an unreadable page, a fragment, a one-byte message */
	g_unmapped_lo = (uintptr_t)g_msg;
	g_unmapped_hi = (uintptr_t)g_msg + 0x1000;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 8) == g_disp_ret && g_stock.calls == 3 && g_nrep == 1, "an unreadable message: the code's");
	g_unmapped_lo = g_unmapped_hi = 0;
	g_msg[0] = 0x14; g_msg[1] = 0x03; g_msg[2] = 0x0C;      /* a fragment (packet type 1) of a command */
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 3) == g_disp_ret && g_stock.ptr == g_msg && g_nrep == 1, "a fragment: the code's");
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 1) == g_disp_ret && g_stock.len == 1, "one byte: the code's");
	/* another connection's command for endpoint 3 (not the offered one) */
	{
		uint8_t *other = slot(3);

		put32(other, BTAAC_C_REC_B, 1);
		other[BTAAC_C_REC_B + 4] = 4;
		g_msg[0] = 0x60; g_msg[1] = 0x06; g_msg[2] = 0x0C;
		CHECK(btaac_disp_hook(other, g_chan, g_msg, 3) == g_disp_ret && g_stock.ptr == g_msg && btaac_neg.n_acc_cmd == 0, "another connection: its commands are the code's");
	}
	/* a Vita-opened connection (the code sent DISCOVER): endpoint 3 is not ours there, a command naming it is the code's */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x12; g_msg[1] = 0x01; memcpy(g_msg + 2, k_discover, 8);   /* DISCOVER's accept */
	btaac_disp_hook(conn, g_chan, g_msg, 10);
	CHECK(btaac_neg.state == BTAAC_N_PROBING && btaac_neg.init == BTAAC_INIT_VITA && !btaac_neg.acceptor, "the Vita opened it");
	btaac_neg.state = BTAAC_N_CONFIGURED;
	CHECK(cmd1(1, 6, 0x0C) == g_disp_ret && g_stock.ptr == g_msg && btaac_neg.n_acc_cmd == 0, "a command for endpoint 3 on the Vita's own AAC connection: the code's (it is not an endpoint of ours there)");
	CHECK(cmd1(1, 2, 0x0C) == g_disp_ret && g_nrep == 0, "... nor are its capabilities answered");
	/* the headphones send DISCOVER on that live connection (the record is in use): the code's own answer, nothing of ours is touched */
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	btaac_neg.send_gate = btaac_neg.pcm_gate = 1;
	g_msg[0] = 0x70; g_msg[1] = 0x01;
	{
		int calls = g_stock.calls;

		CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == g_disp_ret && g_stock.calls == calls + 1 && g_stock.ptr == g_msg && g_nrep == 0 && btaac_neg.state == BTAAC_N_CONFIGURED &&
		      btaac_neg.init == BTAAC_INIT_VITA && !btaac_neg.acceptor && btaac_neg.send_gate && btaac_neg.pcm_gate, "a rediscovery on a live Vita-opened AAC link: the code answers, the state and the gates stay");
	}
	/* the record idle again (a new connection of the slot): the headphones' DISCOVER starts over as usual */
	put32(conn, BTAAC_C_REC_B, 1);
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == 0 && btaac_neg.state == BTAAC_N_OFFERED && btaac_neg.init == BTAAC_INIT_HEADSET && !btaac_neg.send_gate, "with the record idle it is a new connection: offered");
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x12; g_msg[1] = 0x01; memcpy(g_msg + 2, k_discover, 8);
	btaac_disp_hook(conn, g_chan, g_msg, 10);
	btaac_neg.state = BTAAC_N_CONFIGURED;
	/* a banned device is not offered AAC */
	acc_world(1);
	conn = g_aconn;
	btaac_ban((uintptr_t)conn);
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	CHECK(btaac_disp_hook(conn, g_chan, g_msg, 2) == g_disp_ret && g_nrep == 0 && btaac_neg.state == BTAAC_N_NONE, "a device that failed AAC this boot: the code's own SBC list");
	/* a stream that broke: the headphones' CLOSE and ABORT still reach the code as endpoint 1 */
	acc_world(1);
	conn = g_aconn;
	g_msg[0] = 0x50; g_msg[1] = 0x01;
	btaac_disp_hook(conn, g_chan, g_msg, 2);
	send_conf(2, 0x0C, 0x14, k_aac_cfg, 12);
	put32(conn, BTAAC_C_REC_B, 8);
	btaac_neg.state = BTAAC_N_BROKEN;
	CHECK(cmd1(3, 8, 0x0C) == g_disp_ret && g_stock.msg[2] == 4 && REC_STATE() == 1, "a broken stream: CLOSE for endpoint 3 still reaches the code (endpoint 1)");
	CHECK(send_conf(4, 0x0C, 0x14, k_aac_cfg, 12) == g_disp_ret && g_stock.n_unknown >= 1 && REC_STATE() == 1, "a broken stream is not configured again (until the headphones reconnect)");
}

/* The count the code keeps of packets in flight: one more for each call of the send routine that returns >= 0 (text 0x768C, after our hook), one less, never below 0, for each packet
 * that carries a non-zero flag when the link has completed it (the frame sender's "C" format files the flag with the packet; 0x107E0 runs the count down). A unit cut into pieces
 * is several packets: only the last carries the flag, so the count is up once and down once, whatever the cutting. */
static void t_split_count(void)
{
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 264, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	media_world(300, BTAAC_N_CONFIGURED);
	run_sender(300, 300);
	CHECK(btaac_neg.n_sent > 20 && btaac_neg.n_pieces > btaac_neg.n_sent, "units were cut: %u pieces for %u units", (unsigned)btaac_neg.n_pieces, (unsigned)btaac_neg.n_sent);
	int up = (int)btaac_neg.n_sent, down = 0, flagged_pieces = 0, units = 0;

	for (int i = 0; i < g_npkts; i++) {
		int last = (g_pkts[i].data[1] & 0x80) != 0;      /* the marker: the unit's last piece */

		CHECK(last ? g_pkts[i].flag == 1 : g_pkts[i].flag == 0, "packet %d: flag %u, marker %d (only a unit's last piece carries the flag the caller gave)", i, g_pkts[i].flag, last);
		if (g_pkts[i].flag)
			down++;
		units += last;
		flagged_pieces += g_pkts[i].flag != 0;
	}
	CHECK(units == up && down == up && flagged_pieces == up, "in flight: up %d, down %d: balanced after every packet has completed (the old count: down %d)", up, down, g_npkts);
	/* a flag that is not 1 is passed on as it is, on the last piece */
	g_npkts = 0;
	{
		static int16_t blk[512 * 2];

		for (uint32_t b = 0; b < 40 && !g_npkts; b++) {
			tone_block(100000u + b * 512, blk);
			btaac_pcm_block(blk);
			btaac_send_hook(slot(1), g_sbc, 600, 0x77);
			usleep(900);
		}
		for (int i = 0; i < g_npkts; i++)
			CHECK(g_pkts[i].flag == ((g_pkts[i].data[1] & 0x80) ? 0x77u : 0u), "packet %d: the caller's flag value on the last piece only", i);
	}
	/* a piece the code refuses: the unit's earlier pieces carry no flag and the call returns an error, so the code counts nothing up and nothing completes down */
	g_npkts = 0;
	btaac_neg.n_sent = 0;
	{
		int16_t blk[512 * 2];
		int tries = 0, cut_seen = 0;

		while (!cut_seen && tries++ < 200) {
			g_send_calls = 0;
			g_send_fail_at = 1;                    /* the second piece of a unit fails */
			tone_block(200000u + (uint32_t)tries * 512, blk);
			btaac_pcm_block(blk);
			int before = g_npkts, r = btaac_send_hook(slot(1), g_sbc, 600, 1);

			if (g_send_calls >= 2) {                 /* a cut unit: its first piece went, its second was refused */
				cut_seen = 1;
				CHECK(r < 0 && g_npkts == before + 1 && g_pkts[before].flag == 0 && !(g_pkts[before].data[1] & 0x80), "the refused second piece: an error, one piece went out, without the flag (%d, %d)", r, g_npkts - before);
			}
			g_send_fail_at = -1;
			usleep(900);
		}
		CHECK(cut_seen, "a cut unit was met");
	}
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

/* The engine's output is part of the delay: with a bound set the oldest units wait no more than the latency level allows, and what is dropped leaves no gap in the timestamps. */
static void t_ring_bound(void)
{
	static int16_t blk[512 * 2];
	uint8_t *conn = slot(1);

	CHECK(btaac_lat_limit(0) == 11 && btaac_lat_limit(1) == 5 && btaac_lat_limit(2) == 3 && btaac_lat_limit(3) == 11 && btaac_lat_limit(99) == 11, "11, 5, 3 units for Standard, Low, Lowest; anything else Standard");
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 264, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	media_world(1021, BTAAC_N_CONFIGURED);
	/* one unit through, so the timeline has a last unit */
	for (uint32_t b = 0; b < 20 && !g_npkts; b++) {
		tone_block(b * 512, blk);
		btaac_pcm_block(blk);
		btaac_send_hook(conn, g_sbc, 600, 1);
		usleep(900);
	}
	CHECK(g_npkts == 1 && btaac_neg.n_trim == 0, "the first unit, nothing trimmed (no bound set)");
	/* fill the ring: the code is not asking (it is in back-off, or between blocks): the engine encodes on and the units wait */
	for (uint32_t b = 0; b < 200 && info().aus_out - info().aus_popped < 8; b++) {
		tone_block(10000u + b * 512, blk);
		btaac_pcm_block(blk);
		usleep(2000);
	}
	BtAacInfo i = info();

	CHECK(i.aus_out - i.aus_popped >= 8, "the ring holds 8 units (%u made, %u taken)", (unsigned)i.aus_out, (unsigned)i.aus_popped);
	/* no bound: one send takes the oldest and the rest wait */
	uint32_t last_ts = (uint32_t)g_pkts[0].data[4] << 24 | (uint32_t)g_pkts[0].data[5] << 16 | (uint32_t)g_pkts[0].data[6] << 8 | g_pkts[0].data[7];

	btaac_neg.keep = 0;
	btaac_send_hook(conn, g_sbc, 600, 1);
	CHECK(g_npkts == 2 && btaac_neg.n_trim == 0 && btaac_neg.last_au == 1, "not bounded: the next unit (number 1), nothing dropped");
	/* bounded to Lowest's 3: the oldest go, at least 5 of the 8 (the engine may have added one), and the next unit sent is one of the newest */
	btaac_neg.keep = btaac_lat_limit(2);
	btaac_send_hook(conn, g_sbc, 600, 1);
	CHECK(g_npkts == 3 && btaac_neg.n_trim == 1 && btaac_neg.n_trimmed_units >= 4, "bounded to 3: %u units dropped in one go", (unsigned)btaac_neg.n_trimmed_units);
	uint32_t ts1 = (uint32_t)g_pkts[1].data[4] << 24 | (uint32_t)g_pkts[1].data[5] << 16 | (uint32_t)g_pkts[1].data[6] << 8 | g_pkts[1].data[7];
	uint32_t ts2 = (uint32_t)g_pkts[2].data[4] << 24 | (uint32_t)g_pkts[2].data[5] << 16 | (uint32_t)g_pkts[2].data[6] << 8 | g_pkts[2].data[7];

	CHECK(ts1 - last_ts == 1024 && ts2 - ts1 == 1024, "no gap in the timestamps: the dropped units never existed for the sink (%u, %u)", (unsigned)(ts1 - last_ts), (unsigned)(ts2 - ts1));
	CHECK(btaac_neg.last_au >= 1 + btaac_neg.n_trimmed_units, "the unit sent is one of the newest: number %u after %u dropped", (unsigned)btaac_neg.last_au, (unsigned)btaac_neg.n_trimmed_units);
	/* what is left in the ring is at most the bound (plus what the engine adds meanwhile: it is waiting for room, so one at most per call) */
	i = info();
	CHECK(i.aus_out - i.aus_popped <= 8, "the ring");
	/* Standard's 11 is more than the 8 units waiting here: nothing to drop */
	uint32_t trims = btaac_neg.n_trim;

	btaac_neg.keep = btaac_lat_limit(0);
	btaac_send_hook(conn, g_sbc, 600, 1);
	CHECK(btaac_neg.n_trim == trims, "Standard (11 units), 8 or 9 waiting: nothing to drop");
	/* the module stop and the worker's setter */
	btt_aac_stream_set_latency(1);
	CHECK(btaac_neg.keep == 5, "the worker's setter: Low is 5");
	btt_aac_stream_set_latency(2);
	CHECK(btaac_neg.keep == 3, "Lowest is 3");
	btt_aac_stream_set_latency(0);
	CHECK(btaac_neg.keep == 11, "Standard is 11");
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

/* ---- the link information -------------------------------------------------------------------------------------------- */

static void t_link_info(void)
{
	BtLinkIn in;
	BttLinkInfo o;

	memset(&in, 0, sizeof(in));
	in.on = 1; in.recognised = 1; in.latency = BTT_LAT_LOW;
	btlink_compose(&in, &o);
	CHECK(!o.connected && o.codec == BTT_LINK_CODEC_NONE && o.latency == BTT_LAT_LOW && !o.sample_rate && !o.streaming, "not connected: only the latency is known");
	in.connected = 1;
	btlink_compose(&in, &o);
	CHECK(o.connected && !o.streaming && o.codec == BTT_LINK_CODEC_SBC && o.sample_rate == 48000 && o.sbc_bitpool == 0 && o.sbc_channel_mode == 0 && o.bitrate_kbps == 0,
	      "connected, not streaming: SBC, 48 kHz, the details not known yet");
	in.streaming = 1;
	btlink_compose(&in, &o);
	CHECK(o.streaming && o.sbc_bitpool == 0, "started, the encoder not yet set up: no bitpool");
	in.enc_valid = 1; in.sbc_bitpool = 53;
	for (uint32_t m = 0; m < 4; m++) {
		in.sbc_chmode = m;
		btlink_compose(&in, &o);
		CHECK(o.codec == BTT_LINK_CODEC_SBC && o.sbc_bitpool == 53 && o.sbc_channel_mode == m + 1, "channel mode %u -> %u (mono 1, dual 2, stereo 3, joint 4)", m, o.sbc_channel_mode);
	}
	in.sbc_chmode = 7;
	btlink_compose(&in, &o);
	CHECK(o.sbc_bitpool == 0 && o.sbc_channel_mode == 0, "a channel mode the code does not have is not reported");
	in.aac_stream = 1; in.aac_bps = 256000; in.aac_neon = 1;
	btlink_compose(&in, &o);
	CHECK(o.codec == BTT_LINK_CODEC_AAC && o.bitrate_kbps == 256 && o.accel == 1 && o.sbc_bitpool == 0 && o.sbc_channel_mode == 0 && o.sample_rate == 48000, "AAC at 256 kb/s, accelerated");
	in.aac_neon = 0; in.aac_bps = 191600;
	btlink_compose(&in, &o);
	CHECK(o.bitrate_kbps == 192 && o.accel == 0, "the rate rounds to kb/s: 191,600 is 192");
	in.on = 0;
	btlink_compose(&in, &o);
	CHECK(!o.connected && !o.codec && !o.latency && !o.addr_lo && !o.addr_hi, "Bluetooth Turbo off: nothing is known");
	/* version 2: the device's address, only while connected, the high word's upper half dropped */
	in.on = 1; in.connected = 1; in.addr_lo = 0x11223344u; in.addr_hi = 0xFFFF5566u;
	btlink_compose(&in, &o);
	CHECK(o.addr_lo == 0x11223344u && o.addr_hi == 0x5566u, "the address: low word as it is, high word 16 bits");
	in.connected = 0;
	btlink_compose(&in, &o);
	CHECK(!o.addr_lo && !o.addr_hi, "not connected: no address");
	in.on = 1; in.recognised = 0;
	btlink_compose(&in, &o);
	CHECK(!o.connected && !o.codec, "the Bluetooth code not recognised: nothing is known");
}


/* ---- the worker's side -------------------------------------------------------------------------------------------------- */

static uint64_t g_us = 100u * 1000000u;
static uint64_t fake_clock(void) { return g_us; }

static uint8_t *sslot(int i) { return g_seg1 + 0x104A8 + (size_t)i * BTAAC_SLOT_SIZE; }

static void text_stock(void)
{
	memset(g_text, 0x5A, sizeof(g_text));
	memcpy(g_text + BTAAC_OFF_DISP, k_disp_head, sizeof(k_disp_head));
	memcpy(g_text + BTAAC_OFF_DISP_W1, k_disp_w1, sizeof(k_disp_w1));
	memcpy(g_text + BTAAC_OFF_DISP_W2, k_disp_w2, sizeof(k_disp_w2));
	memcpy(g_text + BTAAC_OFF_DISP_W3, k_disp_w3, sizeof(k_disp_w3));
	memcpy(g_text + BTAAC_OFF_DISP_W4, k_disp_w4, sizeof(k_disp_w4));
	memcpy(g_text + BTAAC_OFF_SEND, k_send_a, sizeof(k_send_a));
	memcpy(g_text + BTAAC_OFF_SEND_B, k_send_b, sizeof(k_send_b));
	memcpy(g_text + BTAAC_OFF_L2, k_l2_head, sizeof(k_l2_head));
	memcpy(g_text + BTAAC_OFF_L2_W, k_l2_w, sizeof(k_l2_w));
	memcpy(g_text + BTAAC_OFF_FMT, k_fmt, sizeof(k_fmt));
	hash_table_for(g_text);
}

static void stream_world(void)
{
	text_stock();
	memset(g_seg1, 0, sizeof(g_seg1));
	memset((void *)&btaac_neg, 0, sizeof(btaac_neg));
	g_seg1_ok = g_modinfo_ok = g_qual_hook_ok = 1;
	g_hook_fail_at = -1;
	g_qual_hook_calls = g_ninst = g_nrel = g_sev_registered = g_sev_unregistered = g_hook_calls = 0;
	btaac_link_api = &k_api;
	g_clock_hook = fake_clock;
	btaac_disp_ref = btaac_send_ref = 0;
	/* the worker's own state, as at module load (a scenario may start its world again) */
	g_h_disp = g_h_send = g_sev = -1;
	g_installed = g_failed = g_gate = g_gate_ok = g_ours = 0;
	g_start_audio = 0;
	memset(&g_rt, 0, sizeof(g_rt));
	g_n_retry = g_n_retry_ok = g_n_retry_bans = g_n_watchdog = g_last_retry_ret = 0;
	memset(&g_sa, 0, sizeof(g_sa));
	g_sa_ret = g_sa_fail = 0;
	g_sa_found = 1;
	g_seen_state = BTAAC_N_NONE;
	g_seen_discover = 0;
	g_src_only = 1;
	g_src_seen = 0;
	g_src_seen_disc = g_src_seen_rej = g_src_seen_nore = 0;
}

static void t_stream_install(void)
{
	engine_up();
	run_selftest();
	stream_world();
	/* master switch off: nothing at all */
	btaac_link_api = NULL;
	btt_aac_stream_pass(0, BTT_CODEC_AAC, 1);
	CHECK(g_hook_calls == 0 && !btaac_cdc.on && !btaac_neg.enable, "the master switch off: nothing installed");
	/* no codec to send (the AAC module absent): only the signalling hook goes in, to read the headphones' codec lists; nothing is offered */
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_hook_calls == 1 && g_ninst == 1 && g_inst[0].off == 0x9F4C && g_inst[0].hook == btaac_disp_hook && g_qual_hook_calls == 0 && g_sev_registered == 0 &&
	      btaac_cdc.on == 1 && !btaac_neg.enable && !btt_aac_stream_wants_pcm_hook() && !btt_aac_stream_fast(), "the AAC module absent: the reader only (%d hooks)", g_ninst);
	btaac_link_api = &k_api;
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(g_hook_calls == 1 && btaac_cdc.on == 1 && !btaac_neg.enable && !btt_aac_stream_wants_pcm_hook(), "the codec SBC: still only the reader, nothing offered, not hooked twice");
	btt_aac_stream_pass(0, BTT_CODEC_AAC, 1);
	CHECK(g_hook_calls == 1 && btaac_cdc.on == 0 && !btaac_neg.enable && g_nrel == 0, "the master switch off: nothing is read, the hook stays in and passes through");
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(btaac_cdc.on == 1, "and reads again once it is on");
	/* wanted */
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 2 && g_inst[0].off == 0x9F4C && g_inst[0].hook == btaac_disp_hook && g_inst[0].thumb == 1 && g_inst[1].off == 0xAD6C &&
	      g_inst[1].hook == btaac_send_hook && g_inst[1].thumb == 1, "the two hooks, Thumb code, at the two sites (%d installed)", g_ninst);
	CHECK(g_qual_hook_calls == 1 && g_sev_registered == 1 && btaac_neg.enable == 1 && btt_aac_stream_wants_pcm_hook(), "the shared hook ensured, the standby handler registered, AAC offered");
	CHECK(btaac_neg.conn_lo == (uintptr_t)g_seg1 + 0x104A8 && btaac_neg.conn_hi == btaac_neg.conn_lo + 7u * 0x41D0u &&
	      btaac_neg.l2_send == (((uintptr_t)g_text + 0x109EC) | 1u) && btaac_neg.l2_fmt == (uintptr_t)g_text + 0x1EDD8 && btaac_neg.kbps == 256,
	      "the link table's bounds, the frame sender (Thumb), the format");
	CHECK(BTAAC_PREROLL_DEFAULT == 0 && btaac_neg.preroll == 0, "the pre-roll is off by default: no delay is added to the stream");
	CHECK(log_has("hooks on"), "logged:\n%s", g_log);
	/* the AAC Bit Rate setting: taken at each pass for the next negotiation; 0 is the default again */
	btt_aac_stream_set_kbps(BTT_AACBR_KBPS(BTT_AACBR_320));
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.kbps == 320, "the AAC Bit Rate 320 is not asked for (%u)", (unsigned)btaac_neg.kbps);
	btt_aac_stream_set_kbps(BTT_AACBR_KBPS(BTT_AACBR_128));
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.kbps == 128, "the AAC Bit Rate 128 is not asked for (%u)", (unsigned)btaac_neg.kbps);
	btt_aac_stream_set_kbps(0);
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.kbps == 256, "no bit rate given: not 256 (%u)", (unsigned)btaac_neg.kbps);
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 2 && g_qual_hook_calls == 1 && g_sev_registered == 1, "not installed again");
	/* switched off: no longer offered; the hooks stay in (passing through) */
	btt_aac_stream_pass(0, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.enable == 0 && g_nrel == 0 && g_ninst == 2 && btt_aac_stream_wants_pcm_hook(), "off: not offered, hooks stay");
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(btaac_neg.enable == 0, "the codec back to SBC: not offered");
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.enable == 1 && g_ninst == 2, "on again: offered, nothing installed twice");
	g_bteq_in = 0;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.enable == 0, "the sender's hook is not in: AAC is not offered");
	g_bteq_in = 1;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.enable == 1, "and is once it is");
	engine_down();
}

/* A failure case: the reader's hook went in at the start, then the AAC module came and a codec was wanted: the gate looked at the bytes again, found the reader's own jump at
 * the head of the signalling routine and refused (window 1), so the stream stayed SBC. The first verdict is kept; the reader is not hooked twice. */
static void t_stream_reader_then_codec(void)
{
	engine_up();
	run_selftest();
	stream_world();
	btaac_link_api = NULL;                           /* the codec modules are not loaded yet: the reader only */
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 1 && g_inst[0].off == BTAAC_OFF_DISP && !memcmp(g_text + BTAAC_OFF_DISP, "\xDF\xF8\x00\xF0", 4) && log_has("it reads the headphones' codec lists"),
	      "the reader is in and its jump is in the routine's first bytes");
	CHECK(btaac_win_diff(g_text, 0) == 0, "(the bytes now differ from the stock ones, from byte 0)");
	g_log[0] = 0;
	btaac_link_api = &k_api;                         /* the module is there, the codec is AAC */
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(!log_has("not recognised") && !log_has("window"), "the gate does not refuse the reader's own hook\n%s", g_log);
	CHECK(g_ninst == 2 && g_inst[0].off == BTAAC_OFF_DISP && g_inst[1].off == BTAAC_OFF_SEND && g_hook_calls == 2, "the media hook is added and the signalling routine is not hooked twice (%d)", g_ninst);
	CHECK(btaac_neg.enable == 1 && g_installed && !g_failed && g_gate == 0 && log_has("hooks on"), "AAC is offered");
	/* the codec back to SBC: the hooks stay, nothing is looked at or released */
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(btaac_neg.enable == 0 && g_ninst == 2 && g_nrel == 0 && btaac_cdc.on == 1 && !g_failed, "the codec out: the reader stays in, nothing offered");
	/* and in again, a second and third time: nothing re-checked, nothing installed */
	g_log[0] = 0;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	btt_aac_stream_pass(0, BTT_CODEC_AAC, 1);
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(btaac_neg.enable == 1 && g_ninst == 2 && g_hook_calls == 2 && !log_has("not recognised"), "again: nothing re-checked, nothing hooked");
	/* a refusal before any hook of ours is in is not kept: after off and on it is looked at again */
	stream_world();
	g_log[0] = 0;
	g_text[BTAAC_OFF_SEND] ^= 1;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 0 && !g_gate_ok && log_has("not recognised (window 6)"), "bytes the code does not have: refused, not remembered as good");
	engine_down();
}

/* Music Source Only in the worker's pass: in force with the master switch on and the signalling hook in; what the hooks counted goes to the log */
static void t_stream_src_only(void)
{
	engine_up();
	run_selftest();
	stream_world();
	btaac_link_api = NULL;
	btt_aac_stream_pass(0, BTT_CODEC_SBC, 1);
	CHECK(!btaac_neg.src_only && !log_has("music source only"), "the master switch off: the console's own list, nothing said");
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(btaac_neg.src_only == 1 && g_ninst == 1 && log_has("music source only: on: devices are offered the Vita's source endpoint only"), "on by default, with the reader in (SBC)");
	g_log[0] = 0;
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(!log_has("music source only"), "said once");
	btt_aac_stream_set_source_only(0);
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(btaac_neg.src_only == 0 && log_has("music source only: off"), "the setting off: stock");
	btt_aac_stream_set_source_only(1);
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(btaac_neg.src_only == 1, "and on again");
	btaac_neg.n_src_discover = 2; btaac_neg.n_src_reject = 1; btaac_neg.n_src_noreply = 1;
	g_log[0] = 0;
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(log_has("answered with the source endpoint only (2 so far)") && log_has("tried to use the Vita as a speaker") && log_has("(1 so far)") && log_has("could not answer"), "what the hooks did is logged\n%s", g_log);
	btt_aac_stream_pass(0, BTT_CODEC_SBC, 1);
	CHECK(btaac_neg.src_only == 0, "the master switch off again: stock");
	/* the code not recognised: the signalling hook is not in, so nothing is in force, and the log says so */
	stream_world();
	g_log[0] = 0;
	g_text[BTAAC_OFF_SEND] ^= 1;
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(g_ninst == 0 && btaac_neg.src_only == 0 && log_has("music source only: on, but the signalling hook is not in"), "the hook refused: not in force, said so\n%s", g_log);
	/* the hook released at module stop: nothing in force */
	stream_world();
	g_obs_failed = 0;   /* (the refusal above is not kept past off and on) */
	btt_aac_stream_pass(1, BTT_CODEC_SBC, 1);
	CHECK(btaac_neg.src_only == 1, "(in force again)");
	btt_aac_stream_restore();
	CHECK(btaac_neg.src_only == 0, "module stop: the console's own list");
	engine_down();
}

/* A failure of the module's own (the check of the code, the link table, a hook that is refused): no mark for the headphones, and no notice that they refused a codec. */
static void t_stream_local_failure(void)
{
	uint32_t lo, hi;

	engine_up();
	run_selftest();
	for (int why = 0; why < 4; why++) {
		stream_world();
		g_log[0] = 0;
		if (why == 0) g_text[BTAAC_OFF_DISP_W2] ^= 1;      /* the gate refuses */
		if (why == 1) g_seg1_ok = 0;                       /* the link table cannot be read */
		if (why == 2) g_hook_fail_at = 0;                  /* the signalling hook is refused */
		if (why == 3) g_qual_hook_ok = 0;                  /* the configuration hook is refused */
		btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
		btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
		CHECK(!btaac_neg.enable && btaac_neg.ban_n == 0 && btt_aac_stream_take_failure(&lo, &hi) == 0, "local failure %d: nothing marked, no notice for the headphones", why);
	}
	engine_down();
}

static void t_stream_refusals(void)
{
	engine_up();
	run_selftest();
	stream_world();
	/* other bytes: refused with the window's number, not tried again until AAC has been off and on */
	g_text[BTAAC_OFF_DISP_W2] ^= 1;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 0 && !btaac_neg.enable && !btt_aac_stream_wants_pcm_hook() && log_has("not recognised (window 3)") && log_has("window 3 at text +0x9f8e differs at +0: live"),
	      "bytes the code does not have: refused, with where they differ\n%s", g_log);
	g_text[BTAAC_OFF_DISP_W2] ^= 1;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 0, "the bytes are right now, but a refusal is not retried by itself");
	btt_aac_stream_pass(0, BTT_CODEC_AAC, 1);
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 2 && btaac_neg.enable, "after off and on it is tried again, and goes in");
	/* the link table not readable */
	stream_world();
	g_log[0] = 0;
	g_seg1_ok = 0;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 0 && log_has("link table is not readable"), "no link table: refused");
	/* the Bluetooth code not found */
	stream_world();
	g_log[0] = 0;
	g_modinfo_ok = 0;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 0 && log_has("Bluetooth code not found"), "no Bluetooth code: refused");
	/* the shared hook refused */
	stream_world();
	g_log[0] = 0;
	g_qual_hook_ok = 0;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 0 && !btaac_neg.enable && log_has("configuration hook did not go in"), "the configuration hook refused: nothing else installed");
	/* the signalling hook refused */
	stream_world();
	g_log[0] = 0;
	g_hook_fail_at = 0;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 0 && !btaac_neg.enable && log_has("signalling hook failed") && !btt_aac_stream_wants_pcm_hook(), "the first hook refused");
	/* the media hook refused: the first stays in, is not hooked twice on the next try, and AAC is not offered meanwhile */
	stream_world();
	g_log[0] = 0;
	g_hook_fail_at = 1;
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 1 && !btaac_neg.enable && log_has("media hook failed") && !btt_aac_stream_wants_pcm_hook(), "the second hook refused: not offered");
	g_hook_fail_at = -1;
	btt_aac_stream_pass(0, BTT_CODEC_AAC, 1);
	btt_aac_stream_pass(1, BTT_CODEC_AAC, 1);
	CHECK(g_ninst == 2 && g_inst[0].off == 0x9F4C && g_inst[1].off == 0xAD6C && btaac_neg.enable, "the retry hooks only the media routine: the other is not hooked twice");
	engine_down();
}

static void stream_up(uint8_t *conn)
{
	btaac_neg.conn = (uintptr_t)conn;
	btaac_neg.state = BTAAC_N_CONFIGURED;
	btaac_neg.bps = 256000;
	btaac_neg.max_bps = 264630;
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	put32(conn, BTAAC_C_PROFILES, 0);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = (uintptr_t)g_chan;     /* the reference headset: a packet limit of 895 bytes */
	put32(g_chan, 8, 895);
}
static void pass(int accel) { btt_aac_stream_pass(1, BTT_CODEC_AAC, accel); }
static int c_gates(void) { return btaac_neg.send_gate && btaac_neg.pcm_gate; }
static void pass_until(int (*cond)(void), int accel, const char *what)
{
	for (int i = 0; i < 4000 && !cond(); i++) {
		pass(accel);
		if (cond())
			return;
		usleep(1000);
	}
	CHECK(cond(), "timed out waiting for: %s", what);
}

static void t_stream_session(void)
{
	uint8_t *conn = sslot(2);

	engine_up();
	run_selftest();
	stream_world();
	pass(0);
	CHECK(g_ninst == 2, "installed");
	stream_up(conn);
	pass(0);
	CHECK(btt_aac_stream_fast() && info().state == BTAAC_S_IDLE && !c_gates(), "an AAC stream that is not started: watched closely, no session, no gates");
	/* watched closely while the stream's record is in use, at any of its states (a microphone taking the link for the headphones' voice and giving it back goes 10, 11, 8, 9, 10:
	 * the session starts again within 0.1 s), and not once it is idle (the link gone, closed) */
	for (uint32_t rec = 0; rec <= 14; rec++) {
		put32(conn, BTAAC_C_REC_B, rec);
		CHECK(btt_aac_stream_fast() == (rec >= 3 && rec <= 13), "the stream record at %u: fast %d", rec, btt_aac_stream_fast());
	}
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	btaac_neg.state = BTAAC_N_OFFERED;
	CHECK(!btt_aac_stream_fast() && !btt_aac_stream_is_aac(), "offered, not configured: the slow pass, not an AAC stream (the send queue's limit stays the SBC one)");
	btaac_neg.state = BTAAC_N_CONFIGURED;
	CHECK(btt_aac_stream_fast() && btt_aac_stream_is_aac(), "configured: AAC");
	/* the stream starts: the session is asked for at the rate the configuration carries, the gates go up when it runs */
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	pass_until(c_gates, 0, "the gates");
	BtAacInfo i = info();
	CHECK(i.state == BTAAC_S_RUNNING && i.bps == 256000 && i.rate == 48000 && i.channels == 2 && !i.neon_in_use && g_n_start == 1 && log_has("AAC is playing"),
	      "session: state %u, %u b/s, %u Hz, %u channels, started %u times", i.state, i.bps, i.rate, i.channels, g_n_start);
	/* the session is held to the link's packet limit, read when it starts */
	CHECK(i.au_cap == btaac_au_cap(895) && i.au_cap == 873 && i.cap_state == BTAAC_CAP_APPLIED && i.reservoir == btaac_cap_reservoir(873, 256000, 48000) && log_has("access units held to 873 bytes"),
	      "cap %u, state %u, reservoir %u", i.au_cap, i.cap_state, i.reservoir);
	/* a pause shorter than the grace keeps the session */
	put32(conn, BTAAC_C_PROFILES, 0);
	g_us += 2u * 1000000u;
	pass(0);
	CHECK(info().state == BTAAC_S_RUNNING && c_gates() && g_n_start == 1, "a pause of 2 s keeps the session");
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	g_us += 1000000u;
	pass(0);
	CHECK(info().state == BTAAC_S_RUNNING && c_gates() && g_n_start == 1 && g_n_stop == 0, "and the stream resumes on it");
	/* a pause longer than the grace ends it, gates first */
	put32(conn, BTAAC_C_PROFILES, 0);
	g_us += 3500000u;
	pass(0);
	CHECK(!c_gates() && !btaac_neg.send_gate && !btaac_neg.pcm_gate && g_n_stop == 1 && log_has("stopped for 3 s"), "a pause over 3 s: gates down, session asked to end\n%s", g_log);
	wait_for(c_idle, "the session to end");
	fs_expect('K', 'K', 0);
	/* the stream starts again: a new session */
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	pass_until(c_gates, 0, "the gates again");
	CHECK(g_n_start == 2 && info().state == BTAAC_S_RUNNING, "a new session");
	/* the link goes (the record is no longer open) */
	put32(conn, BTAAC_C_REC_B, 0);
	pass(0);
	CHECK(!c_gates() && g_n_stop == 2 && log_has("stream is gone") && !btt_aac_stream_fast(), "the stream record closed: gates down, session ended, the worker back to its slow pass");
	wait_for(c_idle, "the session to end");
	pass(0);
	CHECK(info().state == BTAAC_S_IDLE && !c_gates(), "and no new session while it is gone");
	/* back */
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	pass_until(c_gates, 0, "the gates after the link came back");
	CHECK(g_n_start == 3, "a third session");
	/* the console goes down: the session is over before the handler returns, the crash record clean */
	CHECK(g_sev_handler && g_sev_handler(1, 0x20F, NULL, NULL) == 0 && c_gates() && info().state == BTAAC_S_RUNNING, "a wake-up event changes nothing (even one that carries the number of the standby's)");
	CHECK(g_sev_handler(0, 0x20F, NULL, NULL) == 0, "the handler never holds up the standby");
	CHECK(info().state == BTAAC_S_IDLE && !c_gates() && g_n_down == 1, "standby: the session ended, the gates down");
	fs_expect('K', 'K', 0);
	/* the connection ends with another negotiation (a new DISCOVER resets the state): nothing is AAC any more */
	btaac_neg.state = BTAAC_N_NONE;
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	pass(0);
	CHECK(info().state == BTAAC_S_IDLE && !c_gates() && !btt_aac_stream_fast(), "an SBC stream is never fed to the engine");
	engine_down();
}

/* The power code sends a few dozen events for one power-down (0x100, 0x102, 0x20F down to 0x200, 0x400, a loop of 0x401 and 0x402 every 10 ms, 0x1000, 0x400F down to
 * 0x4000), and for a vetoed or cancelled attempt two (0x100, 0x101). Only 0x20F, past the point of no return, is a standby: it ends the session, counted once; 0x101 is counted
 * as cancelled and leaves the session alone; every other event is ignored, and none of them waits (only a session that is ended waits, in stream_end). */
/* A link whose packet limit cannot be read is the default one (672 bytes): the session is held to its cap, and 256 kb/s does not fit it, so the rate comes down to the highest that does. */
static void t_stream_session_default_limit(void)
{
	uint8_t *conn = sslot(2);

	engine_up();
	run_selftest();
	stream_world();
	pass(0);
	stream_up(conn);
	*(uintptr_t *)(conn + BTAAC_C_MEDIA_CH) = 0;
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	pass_until(c_gates, 0, "the gates");
	BtAacInfo i = info();

	CHECK(i.state == BTAAC_S_RUNNING && i.au_cap == 651 && i.cap_state == BTAAC_CAP_APPLIED && i.bps == btaac_cap_max_bps(651, 48000) && i.bps < 256000 && i.bps > 230000,
	      "the default limit: cap %u, state %u, %u b/s", i.au_cap, i.cap_state, i.bps);
	CHECK(log_has("session started: 239 kb/s"), "the log says the rate");
	engine_down();
}

static void t_stream_standby(void)
{
	uint8_t *conn = sslot(2);
	static const int k_down[] = { 0x100, 0x102, 0x20E, 0x20D, 0x20C, 0x20B, 0x20A, 0x209, 0x208, 0x207, 0x206, 0x205, 0x204, 0x203, 0x202, 0x201, 0x200, 0x400, 0x401, 0x402, 0x401, 0x402,
	                              0x1000, 0x400F, 0x400E, 0x4001, 0x4000 };

	engine_up();
	run_selftest();
	stream_world();
	pass(0);
	stream_up(conn);
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	pass_until(c_gates, 0, "the gates");
	CHECK(g_sev_handler && g_n_down == 0 && g_n_cancelled == 0 && g_n_ignored == 0, "nothing counted yet");
	/* a cancelled attempt: 0x100, 0x101 */
	CHECK(g_sev_handler(0, 0x100, NULL, NULL) == 0 && g_sev_handler(0, 0x101, NULL, NULL) == 0, "the handler never holds up a power-down");
	CHECK(c_gates() && info().state == BTAAC_S_RUNNING && g_n_stop == 0 && g_n_down == 0 && g_n_cancelled == 1 && g_n_ignored == 1, "a cancelled attempt: the session is left alone (cancelled %u, ignored %u, down %u)",
	      g_n_cancelled, g_n_ignored, g_n_down);
	/* the coming back from standby, and the cancelled attempt's own events when resumed: nothing */
	CHECK(g_sev_handler(1, 0x20F, NULL, NULL) == 0 && g_sev_handler(1, 0x101, NULL, NULL) == 0 && c_gates() && g_n_stop == 0 && g_n_down == 0 && g_n_cancelled == 1, "events of a resume count for nothing");
	/* a real power-down, up to the point of no return: none of these ends the session, none waits */
	for (unsigned k = 0; k < 2; k++)
		CHECK(g_sev_handler(0, k_down[k], NULL, NULL) == 0, "event %x", k_down[k]);
	CHECK(c_gates() && info().state == BTAAC_S_RUNNING && g_n_stop == 0 && g_n_down == 0, "0x100 and 0x102 (interrupts masked, may still be vetoed) leave the session alone");
	/* ... 0x20F is the standby */
	CHECK(g_sev_handler(0, 0x20F, NULL, NULL) == 0, "the standby event");
	CHECK(info().state == BTAAC_S_IDLE && !c_gates() && g_n_stop == 1 && g_n_down == 1, "0x20F: the session ended, the gates down, counted once");
	fs_expect('K', 'K', 0);
	/* the rest of the same power-down: ignored, however many, and still one standby */
	uint32_t ign = g_n_ignored;

	for (unsigned k = 2; k < sizeof(k_down) / sizeof(k_down[0]); k++)
		CHECK(g_sev_handler(0, k_down[k], NULL, NULL) == 0, "event %x", k_down[k]);
	CHECK(g_n_down == 1 && g_n_stop == 1 && g_n_cancelled == 1 && g_n_ignored == ign + (sizeof(k_down) / sizeof(k_down[0]) - 2), "the other %u events are ignored (down %u, cancelled %u, ignored %u)",
	      (unsigned)(sizeof(k_down) / sizeof(k_down[0]) - 2), g_n_down, g_n_cancelled, g_n_ignored);
	/* a standby with no session running: counted, nothing to end */
	CHECK(g_sev_handler(0, 0x20F, NULL, NULL) == 0 && g_n_down == 2 && g_n_stop == 1, "a standby with no session: counted, nothing ended");
	/* the AAC module not there: the standby event is still counted as ignored, no crash */
	btaac_link_api = 0;
	CHECK(g_sev_handler(0, 0x20F, NULL, NULL) == 0 && g_n_down == 2, "no engine to end: nothing");
	engine_down();
}

static void t_stream_engine_failure(void)
{
	uint8_t *conn = sslot(1);
	static int16_t blk[512 * 2];

	engine_up();
	run_selftest();
	stream_world();
	pass(0);
	stream_up(conn);
	conn[BTAAC_C_ADDR_LO] = 0x21; conn[BTAAC_C_ADDR_HI] = 0x43;
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	g_calls[0] = 0;
	g_fault[0] = F_ERROR;                      /* the plain C build's third encode fails */
	pass_until(c_gates, 0, "the gates");
	for (int b = 0; b < 8; b++) {
		tone_block((uint32_t)b * 512, blk);
		btaac_push_pcm(blk, 512);
	}
	wait_for(c_failed, "the engine to fail");
	pass(0);
	CHECK(btaac_neg.state == BTAAC_N_BROKEN && !c_gates() && btaac_banned((uintptr_t)conn) && g_n_broken == 1 && log_has("AAC failed"),
	      "the engine failed: the stream is broken (no sound), the device banned\n%s", g_log);
	/* the engine's failure is the module's own: the device is kept from AAC this boot (no silent reconnect loop), but as a local mark: no refusal, no notice */
	{
		uint32_t lo = btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_LO), hi = btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_HI), nl = 0, nh = 0;

		CHECK(btaac_local_bits(lo, hi) == BTAAC_BAN_AAC && btaac_ban_bits(lo, hi) == 0, "the engine failed: a local mark for AAC, not a refusal by the headphones");
		CHECK(btt_aac_stream_take_failure(&nl, &nh) == 0, "and no codec notice is reported for it");
		CHECK(log_has("not offered this start: the AAC add-on could not start a session"), "the log says why\n%s", g_log);
	}
	CHECK(btaac_send_ref && btaac_send_hook(conn, g_sbc, 100, 1) < 0 && btaac_neg.n_swallowed == 1, "and the code's own SBC packet is never sent on it");
	pass(0);
	CHECK(btaac_neg.state == BTAAC_N_BROKEN && g_n_start == 1, "no restart by itself");
	/* the headphones reconnect: a new DISCOVER, and they are not offered AAC again */
	btaac_disp_ref = (tai_hook_ref_t)fake_disp;
	g_msg[0] = 0x12; g_msg[1] = 0x01;
	memcpy(g_msg + 2, k_discover, 8);
	btaac_disp_hook(conn, g_chan, g_msg, 10);
	CHECK(btaac_neg.state == BTAAC_N_NONE, "the next connection is SBC");
	engine_down();
}

static void t_stream_start_refused(void)
{
	uint8_t *conn = sslot(5);

	engine_up();
	run_selftest();
	stream_world();
	pass(1);
	stream_up(conn);
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	g_hold = 1;                                 /* the crash record holds AAC off from now on */
	pass(1);
	CHECK(btaac_neg.state == BTAAC_N_BROKEN && !c_gates() && g_last_start_err == (uint32_t)-BTAAC_ERR_HELD && log_has("would not start"), "a refused session: the stream is broken\n%s", g_log);
	CHECK(btaac_banned((uintptr_t)conn) && btaac_ban_bits(btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_LO), btaac_rd32((uintptr_t)conn, BTAAC_C_ADDR_HI)) == 0 &&
	      log_has("not offered this start: the AAC add-on could not start a session"), "a refused session: still not offered, but a local mark only (no refusal)\n%s", g_log);
	engine_down();
}

static void t_stream_restore(void)
{
	uint8_t *conn = sslot(0);

	engine_up();
	run_selftest();
	stream_world();
	pass(1);
	stream_up(conn);
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	pass_until(c_gates, 1, "the gates");
	btt_aac_stream_restore();
	CHECK(info().state == BTAAC_S_IDLE && !c_gates() && !btaac_neg.enable && g_nrel == 2 && g_sev_unregistered == 1 && !btaac_disp_ref && !btaac_send_ref &&
	      !btt_aac_stream_wants_pcm_hook(), "module stop: the session ended, the hooks released, the handler removed, nothing offered");
	fs_expect('K', 'K', 0);
	btt_aac_stream_restore();                  /* harmless twice */
	CHECK(g_nrel == 2, "nothing released twice");
	engine_down();
}

static void t_stream_log(void)
{
	engine_up();
	run_selftest();
	stream_world();
	pass(1);
	CHECK(!log_has("new connection"), "nothing yet");
	btaac_neg.n_discover = 1; btaac_neg.state = BTAAC_N_PROBING;
	pass(1);
	CHECK(log_has("new connection: reading the headphones"), "probing");
	btaac_neg.have = 1; btaac_neg.seid = 0x18; btaac_neg.object = 0x80; btaac_neg.bps = 256000; btaac_neg.max_bps = 264630;
	btaac_neg.state = BTAAC_N_PENDING;
	pass(1);
	CHECK(log_has("AAC configuration sent: endpoint 6, MPEG-2 LC, 48 kHz stereo, 256000 b/s (the headphones' highest 264630)"), "configuration sent\n%s", g_log);
	btaac_neg.state = BTAAC_N_NONE;
	pass(1);
	CHECK(log_has("stays SBC"), "declined");
	btaac_neg.state = BTAAC_N_FALLBACK; btaac_neg.n_resend = 1;
	pass(1);
	CHECK(log_has("rejected AAC: the SBC configuration was sent again (1 sent, 0 not possible)"), "rejected\n%s", g_log);
	engine_down();
}

/* ---- a START the headphones reject, and the watchdog ---------------------------------------------------------------------- */

#define R_ADDR_LO 0x11223344u
#define R_ADDR_HI 0xABCD5566u
static uint64_t g_base_us;
static void at(uint32_t ms) { g_us = g_base_us + (uint64_t)ms * 1000u; pass(0); }
/* the world of these scenarios: the hooks in, an AAC stream configured on slot 2 (the record at 10, not started), the clock scripted from `g_base_us` */
static uint8_t *retry_world(void)
{
	uint8_t *conn = sslot(2);
	uint32_t fl, fh;

	(void)btt_aac_stream_take_failure(&fl, &fh);   /* a report of an earlier scenario is not this one's */
	engine_up();
	run_selftest();
	stream_world();
	pass(0);
	CHECK(g_start_audio == fake_start_audio, "the Bluetooth code's start call was looked up when the hooks went in");
	stream_up(conn);
	put32(conn, BTAAC_C_ADDR_LO, R_ADDR_LO);
	put32(conn, BTAAC_C_ADDR_HI, R_ADDR_HI);
	g_sa_conn = conn;
	g_base_us = g_us;
	return conn;
}
/* what the Bluetooth code leaves after the headphones reject its START: the record back at 8, the retry byte 1, the "starting" bit still set, nothing started */
static void rejected(uint8_t *conn)
{
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_READY);
	conn[BTAAC_C_REC_B + BTAAC_R_RETRY] = 1;
	put32(conn, BTAAC_C_WANT, BTAAC_STARTING_BIT);
	put32(conn, BTAAC_C_PROFILES, 0);
}

static void t_retry_after_reject(void)
{
	uint8_t *conn = retry_world();
	int prio_calls;

	rejected(conn);
	pass(0);
	CHECK(g_sa.calls == 0 && log_has("rejected the START"), "seen: logged, no call yet\n%s", g_log);
	at(0);
	at(499);
	CHECK(g_sa.calls == 0, "no call inside the grace of 500 ms (the headphones may start it themselves)");
	g_cur_prio = 0x70;
	prio_calls = g_prio_calls;
	at(500);
	CHECK(g_sa.calls == 1 && g_sa.lo == R_ADDR_LO && g_sa.hi == R_ADDR_HI && g_sa.flags == 8, "one call at 500 ms: %d calls, address %08x %08x, flags %u", g_sa.calls, (unsigned)g_sa.lo, (unsigned)g_sa.hi, (unsigned)g_sa.flags);
	CHECK(g_sa.prio == 0x40 && g_prio_calls == prio_calls + 2 && g_prio_last == 0x70 && g_cur_prio == 0x70, "the call runs at the raised priority (%d) and the worker's own comes back", g_sa.prio);
	CHECK(get32(conn, BTAAC_C_REC_B) == 9 && conn[BTAAC_C_REC_B + BTAAC_R_RETRY] == 0 && (get32(conn, BTAAC_C_WANT) & 8),
	      "the \"starting\" bit was cleared before the call, so the code sent its START again: record 9, retry byte 0, starting again");
	CHECK(log_has("asked for the START again (attempt 1 of 3)") && log_has("START sent") && g_n_retry == 1, "logged\n%s", g_log);
	at(600);
	CHECK(g_sa.calls == 1, "while the answer is awaited (record 9) nothing more is done");
	/* the headphones accept: the code marks the stream started (record 10, the started bit, the starting bit gone) and the session begins as ever */
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	put32(conn, BTAAC_C_WANT, 0);
	pass_until(c_gates, 0, "the gates");
	CHECK(g_sa.calls == 1 && g_n_retry_ok == 1 && g_n_start == 1 && log_has("started after 1 retry(ies)") && btaac_neg.ban_n == 0 && g_n_watchdog == 0 && !g_rt.tries,
	      "accepted: the session started, the retry counted as a success, no device marked, no watchdog");
	engine_down();
}

static void t_retry_headset_starts_first(void)
{
	uint8_t *conn = retry_world();

	rejected(conn);
	at(0);
	at(300);
	CHECK(g_sa.calls == 0, "inside the grace");
	/* the headphones send their own START: the code accepts it (started, record 10, the starting bit cleared by it; the retry byte is cleared too here only because on this
	 * 64-bit host the media channel's pointer, 8 bytes at 0x3B8C, shares that byte: on the console it is 4 bytes) */
	put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	conn[BTAAC_C_REC_B + BTAAC_R_RETRY] = 0;
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	put32(conn, BTAAC_C_WANT, 0);
	at(400);
	at(500);
	at(2000);
	CHECK(g_sa.calls == 0 && g_n_retry == 0 && g_n_retry_ok == 0 && !log_has("asked for the START again") && btaac_neg.ban_n == 0, "no call: the headphones started it themselves");
	pass_until(c_gates, 0, "the gates");
	engine_down();
}

static void t_retry_three_rejects(void)
{
	uint8_t *conn = retry_world();

	rejected(conn);
	at(0);                                       /* seen at 0 */
	at(499);
	CHECK(g_sa.calls == 0, "none at 499");
	at(500);
	CHECK(g_sa.calls == 1 && get32(conn, BTAAC_C_REC_B) == 9, "first retry at 500 ms");
	rejected(conn);                              /* rejected again; seen at 600 */
	at(600);
	at(1599);
	CHECK(g_sa.calls == 1, "the second waits 1 s from the rejection: none at 1599");
	at(1600);
	CHECK(g_sa.calls == 2 && get32(conn, BTAAC_C_REC_B) == 9, "second retry 1 s after it was seen");
	rejected(conn);                              /* seen at 1700 */
	at(1700);
	at(3699);
	CHECK(g_sa.calls == 2, "the third waits 2 s: none at 3699");
	CHECK(btaac_neg.ban_n == 0 && g_n_watchdog == 0, "while the retries go on nothing is marked, however long it takes");
	at(3700);
	CHECK(g_sa.calls == 3 && get32(conn, BTAAC_C_REC_B) == 9, "third retry 2 s after it was seen");
	at(3750);
	CHECK(btaac_neg.ban_n == 0, "awaiting the answer to the third: not yet");
	rejected(conn);                              /* the third is rejected too */
	at(3800);
	CHECK(g_sa.calls == 3 && btaac_neg.ban_n == 1 && btaac_banned((uintptr_t)conn) && g_n_retry_bans == 1 && g_n_watchdog == 0 && log_has("rejected 4 times") &&
	      g_rt.given_up && g_n_retry == 3,
	      "three retries rejected: the device is marked (the next connection is SBC), counted once, logged\n%s", g_log);
	for (uint32_t t = 3900; t <= 20000; t += 100)
		at(t);
	CHECK(g_sa.calls == 3 && btaac_neg.ban_n == 1 && g_n_retry_bans == 1 && g_n_watchdog == 0, "no fourth call, and the device is marked once");
	{
		uint32_t lo = 0, hi = 0;

		CHECK(btt_aac_stream_take_failure(&lo, &hi) == BTFALL_C_AAC && lo == R_ADDR_LO && hi == (R_ADDR_HI & 0xFFFFu), "the give-up is reported for the codec fallback notice, with the headphones' address (%08x %08x)", (unsigned)lo, (unsigned)hi);
		CHECK(btt_aac_stream_take_failure(&lo, &hi) == 0, "a report is taken once");
	}
	engine_down();
}

/* every case where nothing may be done: the call is not made and the Bluetooth code's slot is left exactly as it was */
static void t_retry_never_touches(void)
{
	uint8_t *conn = retry_world(), before[BTAAC_SLOT_SIZE];
	struct { const char *what; int enable; uint32_t state; uint32_t rec, w18, w24, prof; int r21; } c[] = {
		{ "SBC (state NONE)",                       1, BTAAC_N_NONE,       8,  8, 0, 0, 1 },
		{ "the headphones were offered AAC, not configured", 1, BTAAC_N_OFFERED, 8, 8, 0, 0, 1 },
		{ "the AAC stream was reconfigured away (state FALLBACK)", 1, BTAAC_N_FALLBACK, 8, 8, 0, 0, 1 },
		{ "Bluetooth Turbo off (not offered)",       0, BTAAC_N_CONFIGURED, 8,  8, 0, 0, 1 },
		{ "the voice link has the link (bit 29 of 3E18)", 1, BTAAC_N_CONFIGURED, 8, 8 | BTAAC_VOICE_BIT, 0, 0, 1 },
		{ "the voice link (bit 29 of 3E1C)",        1, BTAAC_N_CONFIGURED, 8,  8, 0, BTAAC_VOICE_BIT, 1 },
		{ "the voice link (bit 29 of 3E24)",        1, BTAAC_N_CONFIGURED, 8,  8, BTAAC_VOICE_BIT, 0, 1 },
		{ "the console does not want the sound (the starting bit clear)", 1, BTAAC_N_CONFIGURED, 8, 0, 0, 0, 1 },
		{ "no retry byte (the START was not rejected)", 1, BTAAC_N_CONFIGURED, 8, 8, 0, 0, 0 },
		{ "streaming (record 10, started)",         1, BTAAC_N_CONFIGURED, 10, 0, 0, BTAAC_PROF_STARTED, 0 },
		{ "started, the retry byte left behind",    1, BTAAC_N_CONFIGURED, 8,  0, 0, BTAAC_PROF_STARTED, 1 },
		{ "a record that is not 8 with the retry byte set", 1, BTAAC_N_CONFIGURED, 7, 8, 0, 0, 1 },
		{ "the START awaiting its answer (record 9)", 1, BTAAC_N_CONFIGURED, 9, 8, 0, 0, 1 },
	};

	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		btaac_neg.state = c[i].state;
		put32(conn, BTAAC_C_REC_B, c[i].rec);
		conn[BTAAC_C_REC_B + BTAAC_R_RETRY] = (uint8_t)c[i].r21;
		put32(conn, BTAAC_C_WANT, c[i].w18);
		put32(conn, BTAAC_C_PARKED, c[i].w24);
		put32(conn, BTAAC_C_PROFILES, c[i].prof);
		memcpy(before, conn, BTAAC_SLOT_SIZE);
		g_sa.calls = 0;
		memset(&g_rt, 0, sizeof(g_rt));
		g_n_retry = 0;
		for (uint32_t t = 0; t <= 2900; t += 100) {   /* under the watchdog's 3 s, so only the retry could act */
			g_us = g_base_us + (uint64_t)(i * 10000u + t) * 1000u;
			btt_aac_stream_pass(c[i].enable, BTT_CODEC_AAC, 0);
		}
		CHECK(g_sa.calls == 0 && !memcmp(before, conn, BTAAC_SLOT_SIZE) && g_n_retry == 0, "%s: no call and the slot is untouched", c[i].what);
	}
	engine_down();
}

/* the console stops wanting the sound while the retries go on (the starting bit is gone), or the call sends nothing and leaves the bit clear: no more calls, and the next rejected
 * START starts the count afresh */
static void t_retry_stops_when_not_wanted(void)
{
	uint8_t *conn = retry_world();

	rejected(conn);
	at(0);
	at(500);
	CHECK(g_sa.calls == 1 && g_rt.tries == 1, "first retry");
	rejected(conn);
	at(600);
	put32(conn, BTAAC_C_WANT, 0);               /* the app paused the sound: the code cleared the starting bit */
	for (uint32_t t = 700; t <= 8000; t += 100)
		at(t);
	CHECK(g_sa.calls == 1 && g_rt.tries == 0 && btaac_neg.ban_n == 0 && g_n_watchdog == 0, "not wanted any more: no second retry, nothing marked, the count forgotten");
	/* the next time the START is rejected the wait is the first one's again, 0.5 s */
	g_base_us = g_us;
	rejected(conn);
	at(0);
	at(499);
	CHECK(g_sa.calls == 1, "not yet");
	at(500);
	CHECK(g_sa.calls == 2 && g_rt.tries == 1, "0.5 s again");
	/* a call that fails and leaves the bit clear: the same */
	g_base_us = g_us;
	g_sa_fail = 1;
	rejected(conn);
	at(0);
	at(999);
	CHECK(g_sa.calls == 2, "(the second retry of this START: 1 s)");
	at(1000);
	CHECK(g_sa.calls == 3 && get32(conn, BTAAC_C_REC_B) == BTAAC_REC_READY && !(get32(conn, BTAAC_C_WANT) & 8) && log_has("nothing sent"), "the call failed: record still 8, the bit left clear, logged\n%s", g_log);
	for (uint32_t t = 600; t <= 6000; t += 100)
		at(t);
	CHECK(g_sa.calls == 3 && g_rt.tries == 0 && btaac_neg.ban_n == 0, "no more calls after a failed one; nothing is marked (the console's next START, when it wants one, goes through the code as ever)");
	engine_down();
}

static void t_retry_resets_with_the_link(void)
{
	uint8_t *conn = retry_world();

	/* the link goes during the grace: the wait starts again when a rejected START is seen again */
	rejected(conn);
	at(0);
	at(300);
	put32(conn, BTAAC_C_REC_B, 0);
	at(350);
	CHECK(g_rt.stuck_ms == 0 && g_rt.tries == 0, "the link is gone: nothing is waited for");
	rejected(conn);
	at(400);                                     /* seen again at 400 */
	at(800);
	CHECK(g_sa.calls == 0, "a new wait: 500 ms after it was seen again, not after the first time");
	at(900);
	CHECK(g_sa.calls == 1, "and then the retry");
	/* a new negotiation (DISCOVER) after a retry was made: the count of retries starts again, so the next wait is 0.5 s, not 1 s */
	btaac_neg.n_discover++;
	rejected(conn);
	at(1000);
	CHECK(g_rt.tries == 0, "a new negotiation: the retries forgotten");
	at(1499);
	CHECK(g_sa.calls == 1, "not yet");
	at(1500);
	CHECK(g_sa.calls == 2, "the first retry of the new connection waited 500 ms, not 1 s");
	/* the record at 1 */
	rejected(conn);
	at(1600);
	put32(conn, BTAAC_C_REC_B, 1);
	at(1650);
	CHECK(g_rt.stuck_ms == 0 && g_rt.tries == 0, "record 1: forgotten");
	engine_down();
}

static void t_watchdog(void)
{
	uint8_t *conn = retry_world();
	static const uint32_t recs[] = { 5, 6, 7, 9 };
	uint64_t t = 0;

	for (unsigned i = 0; i < sizeof(recs) / sizeof(recs[0]); i++) {
		uint8_t before[BTAAC_SLOT_SIZE];

		btaac_neg.ban_n = 0;
		g_n_watchdog = 0;
		memset(&g_rt, 0, sizeof(g_rt));
		put32(conn, BTAAC_C_REC_B, recs[i]);
		conn[BTAAC_C_REC_B + BTAAC_R_RETRY] = 0;
		put32(conn, BTAAC_C_WANT, 8);
		put32(conn, BTAAC_C_PARKED, 0);
		put32(conn, BTAAC_C_PROFILES, 0);
		memcpy(before, conn, BTAAC_SLOT_SIZE);
		t += 100000;
		g_base_us = g_us = g_base_us + t * 1000u;
		at(0);
		at(3000);
		CHECK(btaac_neg.ban_n == 0 && g_n_watchdog == 0, "record %u: not yet at 3 s", (unsigned)recs[i]);
		at(3100);
		CHECK(btaac_neg.ban_n == 1 && btaac_banned((uintptr_t)conn) && g_n_watchdog == 1 && log_has("configured but never started"), "record %u: marked after 3 s\n%s", (unsigned)recs[i], g_log);
		at(20000);
		at(30000);
		CHECK(btaac_neg.ban_n == 1 && g_n_watchdog == 1 && g_sa.calls == 0 && !memcmp(before, conn, BTAAC_SLOT_SIZE), "record %u: marked once, nothing written to the Bluetooth code, no retry", (unsigned)recs[i]);
		{
			uint32_t lo = 0, hi = 0;

			CHECK(btt_aac_stream_take_failure(&lo, &hi) == BTFALL_C_AAC && lo == R_ADDR_LO && hi == (R_ADDR_HI & 0xFFFFu) && btt_aac_stream_take_failure(&lo, &hi) == 0,
			      "record %u: the watchdog's mark is reported once, for the codec fallback notice", (unsigned)recs[i]);
		}
		g_log[0] = 0;
	}
	/* not marked: the console does not want the sound; started; the voice link; the other records; the request parked only still counts */
	struct { const char *what; uint32_t rec, w18, w24, prof; int mark; } c[] = {
		{ "not wanted",             6, 0, 0, 0, 0 },
		{ "the voice link",         6, 8 | BTAAC_VOICE_BIT, 0, 0, 0 },
		{ "started",                6, 8, 0, BTAAC_PROF_STARTED, 0 },
		{ "streaming (record 10)",  10, 8, 0, 0, 0 },
		{ "the record is not yet a stream's (4)", 4, 8, 0, 0, 0 },
		{ "closing (record 11)",    11, 8, 0, 0, 0 },
		{ "the request is parked until the profile is up", 6, 0, 8, 0, 1 },
	};
	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		btaac_neg.ban_n = 0;
		g_n_watchdog = 0;
		memset(&g_rt, 0, sizeof(g_rt));
		put32(conn, BTAAC_C_REC_B, c[i].rec);
		conn[BTAAC_C_REC_B + BTAAC_R_RETRY] = 0;
		put32(conn, BTAAC_C_WANT, c[i].w18);
		put32(conn, BTAAC_C_PARKED, c[i].w24);
		put32(conn, BTAAC_C_PROFILES, c[i].prof);
		g_base_us = g_us = g_base_us + 100000u * 1000u;
		at(0);
		at(5000);
		at(10000);
		CHECK(btaac_neg.ban_n == (c[i].mark ? 1u : 0u) && g_n_watchdog == (c[i].mark ? 1u : 0u), "%s: %s", c[i].what, c[i].mark ? "marked" : "not marked");
	}
	/* an SBC stream is never watched */
	btaac_neg.ban_n = 0;
	g_n_watchdog = 0;
	btaac_neg.state = BTAAC_N_NONE;
	put32(conn, BTAAC_C_REC_B, 6);
	put32(conn, BTAAC_C_WANT, 8);
	put32(conn, BTAAC_C_PARKED, 0);
	put32(conn, BTAAC_C_PROFILES, 0);
	g_base_us = g_us = g_base_us + 100000u * 1000u;
	at(0);
	at(5000);
	CHECK(btaac_neg.ban_n == 0 && g_n_watchdog == 0, "SBC: not watched");
	engine_down();
}

static void t_retry_without_the_call(void)
{
	uint8_t *conn;

	engine_up();
	run_selftest();
	stream_world();
	g_sa_found = 0;
	pass(0);
	CHECK(g_installed && g_start_audio == 0 && log_has("start call was not found"), "the hooks go in without the call; logged\n%s", g_log);
	conn = sslot(2);
	stream_up(conn);
	g_sa_conn = conn;
	g_base_us = g_us;
	rejected(conn);
	at(0);
	at(2900);
	CHECK(g_sa.calls == 0 && btaac_neg.ban_n == 0 && log_has("not asking again"), "no call to make; the watchdog has not fired yet\n%s", g_log);
	at(3100);
	CHECK(g_sa.calls == 0 && btaac_neg.ban_n == 1 && g_n_watchdog == 1, "but the stream that never starts is still marked after 3 s");
	engine_down();
}

/* ---- link information (the pass and the call) ---------------------------------------------------------------------------- */

/* What the codec fallback notice is told by the stream code: a configuration the headphones rejected (the SBC configuration is sent again), a START rejected until the retries
 * gave up, a stream that never started; for AAC, aptX, aptX HD and LDAC. aptX HD given up on after it was configured is not reported (the next connection is plain aptX). */
static void t_failure_report(void)
{
	uint8_t *conn = retry_world();
	uint32_t lo = 0, hi = 0;
	static const struct { const char *what; int aptx, hd, ldac; int codec; } k[] = {
		{ "AAC", 0, 0, 0, BTFALL_C_AAC }, { "aptX", 1, 0, 0, BTFALL_C_APTX }, { "aptX HD", 1, 1, 0, BTFALL_C_APTX_HD }, { "LDAC", 0, 0, 1, BTFALL_C_LDAC },
	};

	CHECK(btt_aac_stream_take_failure(&lo, &hi) == 0, "nothing reported at first");
	for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
		/* the headphones rejected the configuration: the SBC one was sent again, so this connection is SBC now */
		btaac_neg.aptx = (uint32_t)k[i].aptx;
		btaac_neg.apt_hd = (uint32_t)k[i].hd;
		btaac_neg.ldac = (uint32_t)k[i].ldac;
		btaac_neg.hd_off = 0;
		btaac_neg.state = BTAAC_N_CONFIGURED;
		at(0);
		btaac_neg.state = BTAAC_N_FALLBACK;
		at(100);
		CHECK(btt_aac_stream_take_failure(&lo, &hi) == k[i].codec && lo == R_ADDR_LO && hi == (R_ADDR_HI & 0xFFFFu), "%s: the rejected configuration is reported with the address (%08x)", k[i].what, (unsigned)lo);
		CHECK(btt_aac_stream_take_failure(&lo, &hi) == 0, "%s: once", k[i].what);
		at(200);
		CHECK(btt_aac_stream_take_failure(&lo, &hi) == 0, "%s: not again while the state stays", k[i].what);
		/* a stream that never starts: marked after 3 s, reported unless it is aptX HD */
		btaac_neg.state = BTAAC_N_CONFIGURED;
		btaac_neg.ban_n = 0;
		memset(&g_rt, 0, sizeof(g_rt));
		put32(conn, BTAAC_C_REC_B, 6);
		conn[BTAAC_C_REC_B + BTAAC_R_RETRY] = 0;
		put32(conn, BTAAC_C_WANT, 8);
		put32(conn, BTAAC_C_PARKED, 0);
		put32(conn, BTAAC_C_PROFILES, 0);
		g_base_us = g_us = g_base_us + 100000u * 1000u;
		at(0);
		at(3100);
		if (k[i].hd)
			CHECK(btt_aac_stream_take_failure(&lo, &hi) == 0 && btaac_neg.hd_off == 1, "%s: refused, but the device keeps plain aptX: not reported as SBC", k[i].what);
		else
			CHECK(btt_aac_stream_take_failure(&lo, &hi) == k[i].codec && lo == R_ADDR_LO, "%s: the watchdog's mark is reported", k[i].what);
		put32(conn, BTAAC_C_REC_B, BTAAC_REC_OPEN);
	}
	btaac_neg.aptx = btaac_neg.apt_hd = btaac_neg.ldac = btaac_neg.hd_off = 0;
	engine_down();
}

static void t_link_pass(void)
{
	BttLinkInfo o;
	uint8_t *conn = sslot(3);

	stream_world();
	btt_link_pass(1, BTT_LAT_LOW);
	btt_link_read(&o);
	CHECK(!o.connected && !o.codec && o.latency == BTT_LAT_LOW, "nothing connected");
	/* the A2DP stream's connection, started, with the encoder set up */
	*(uintptr_t *)(g_seg1 + 0x36528) = (uintptr_t)conn;
	put32(conn, BTAAC_C_PROFILES, BTAAC_PROF_STARTED);
	put32(g_seg1, 0x37AF4, 0);
	put32(g_seg1, 0x3652C + 0x14, 3);
	put32(g_seg1, 0x3652C + 0x28, 53);
	put32(conn, BTAAC_C_ADDR_LO, 0x11223344u);
	put32(conn, BTAAC_C_ADDR_HI, 0xAB005566u);
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(o.addr_lo == 0x11223344u && o.addr_hi == 0x5566u, "the slot's address: 556611223344 as the device page prints it");
	CHECK(o.connected && o.streaming && o.codec == BTT_LINK_CODEC_SBC && o.sbc_bitpool == 53 && o.sbc_channel_mode == BTT_LINK_CH_JOINT && o.sample_rate == 48000 &&
	      !o.bitrate_kbps && !o.accel && o.latency == BTT_LAT_STANDARD, "SBC joint stereo at bitpool 53");
	put32(g_seg1, 0x37AF4, 1);                      /* the first packet has not set the encoder up */
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(o.streaming && o.sbc_bitpool == 0 && o.sbc_channel_mode == 0, "the encoder not set up yet: no SBC details");
	/* the AAC stream of this connection */
	btaac_neg.conn = (uintptr_t)conn; btaac_neg.state = BTAAC_N_CONFIGURED; btaac_neg.bps = 256000;
	btt_link_pass(1, BTT_LAT_LOWEST);
	btt_link_read(&o);
	CHECK(o.codec == BTT_LINK_CODEC_AAC && o.bitrate_kbps == 256 && o.latency == BTT_LAT_LOWEST && o.connected, "AAC, 256 kb/s");
	/* the AAC state belongs to another connection: this one is SBC */
	btaac_neg.conn = (uintptr_t)sslot(4);
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(o.codec == BTT_LINK_CODEC_SBC, "another connection's AAC state is not this one's");
	/* a pointer that is not a slot of the table is no connection */
	*(uintptr_t *)(g_seg1 + 0x36528) = (uintptr_t)conn + 8;
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(!o.connected && !o.codec, "not a link slot");
	*(uintptr_t *)(g_seg1 + 0x36528) = 0;
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(!o.connected, "no stream");
	/* off, or the code not recognised */
	*(uintptr_t *)(g_seg1 + 0x36528) = (uintptr_t)conn;
	btt_link_pass(0, BTT_LAT_LOW);
	btt_link_read(&o);
	CHECK(!o.connected && !o.latency && !o.codec, "Bluetooth Turbo off");
	g_seg1_ok = 0;
	btt_link_pass(1, BTT_LAT_LOW);
	btt_link_read(&o);
	CHECK(!o.connected && !o.latency && !o.codec, "the Bluetooth code not recognised");
	g_seg1_ok = 1;
	/* the call */
	*(uintptr_t *)(g_seg1 + 0x36528) = (uintptr_t)conn;
	btaac_neg.state = BTAAC_N_NONE;
	put32(g_seg1, 0x37AF4, 0);
	btt_link_pass(1, BTT_LAT_LOW);
	struct { BttLinkInfo i; unsigned int extra[8]; } big;
	memset(&big, 0xEE, sizeof(big));
	big.i.size = sizeof(big);
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == sizeof(BttLinkInfo) && big.i.version == BTT_LINK_VERSION && big.i.codec == BTT_LINK_CODEC_SBC && big.i.latency == BTT_LAT_LOW &&
	      big.i.addr_lo == 0x11223344u && big.i.addr_hi == 0x5566u && big.extra[0] == 0xEEEEEEEEu, "a bigger caller: our fields, the rest as it had it");
	memset(&big, 0xEE, sizeof(big));
	big.i.size = 8;
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 8 && big.i.version == BTT_LINK_VERSION && big.i.connected == 0xEEEEEEEEu, "the least a caller can pass: size and version only");
	big.i.size = 20;
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 20 && big.i.connected == 1 && big.i.streaming == 1 && big.i.codec == BTT_LINK_CODEC_SBC && big.i.sample_rate == 0xEEEEEEEEu,
	      "a caller from an older version, 20 bytes: the fields it has");
	big.i.size = 44;
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 44 && big.i.latency == BTT_LAT_LOW && big.i.addr_lo == 0xEEEEEEEEu, "a caller from version 1, 44 bytes: no address written");
	big.i.size = 7;
	CHECK(bttGetLinkInfo(&big.i) == BTT_ERR_ARG && bttGetLinkInfo(NULL) == BTT_ERR_ARG, "under 8, or no buffer: refused");
	CHECK(sizeof(BttLinkInfo) == 72 && offsetof(BttLinkInfo, latency) == 40 && offsetof(BttLinkInfo, addr_hi) == 48 && offsetof(BttLinkInfo, codec_mask) == 52 &&
	      offsetof(BttLinkInfo, link_quality) == 56 && offsetof(BttLinkInfo, rssi) == 60 && offsetof(BttLinkInfo, signal_level) == 64 && offsetof(BttLinkInfo, voice_profile) == 68 && BTT_LINK_VERSION == 4,
	      "the structure is 72 bytes in version 4: version 1's 44, version 2's 52 and version 3's 56 are unchanged");
}

/* ---- the signal ---------------------------------------------------------------------------------------------------------- */

/* The word from the two readings: the worse of what each says, at the thresholds' edges. */
static void t_signal_levels(void)
{
	static const struct { int rssi; unsigned level; } rs[] = {
		{ 100, BTT_LINK_SIGNAL_EXCELLENT }, { 1, BTT_LINK_SIGNAL_EXCELLENT }, { 0, BTT_LINK_SIGNAL_EXCELLENT }, { -3, BTT_LINK_SIGNAL_EXCELLENT },
		{ -4, BTT_LINK_SIGNAL_GOOD }, { -10, BTT_LINK_SIGNAL_GOOD }, { -11, BTT_LINK_SIGNAL_FAIR }, { -20, BTT_LINK_SIGNAL_FAIR },
		{ -21, BTT_LINK_SIGNAL_WEAK }, { -128, BTT_LINK_SIGNAL_WEAK },
	};
	static const struct { unsigned lq; unsigned level; } ls[] = {
		{ 255, BTT_LINK_SIGNAL_EXCELLENT }, { 240, BTT_LINK_SIGNAL_EXCELLENT }, { 239, BTT_LINK_SIGNAL_GOOD }, { 200, BTT_LINK_SIGNAL_GOOD },
		{ 199, BTT_LINK_SIGNAL_FAIR }, { 128, BTT_LINK_SIGNAL_FAIR }, { 127, BTT_LINK_SIGNAL_WEAK }, { 1, BTT_LINK_SIGNAL_WEAK }, { 0, BTT_LINK_SIGNAL_WEAK },
	};

	for (unsigned i = 0; i < sizeof(rs) / sizeof(rs[0]); i++)
		CHECK(btsig_level_rssi(rs[i].rssi) == rs[i].level && btsig_level(255, rs[i].rssi) == rs[i].level, "signal strength %d -> %u, got %u", rs[i].rssi, rs[i].level, btsig_level_rssi(rs[i].rssi));
	for (unsigned i = 0; i < sizeof(ls) / sizeof(ls[0]); i++)
		CHECK(btsig_level_lq(ls[i].lq) == ls[i].level && btsig_level(ls[i].lq, 0) == ls[i].level, "link quality %u -> %u, got %u", ls[i].lq, ls[i].level, btsig_level_lq(ls[i].lq));
	/* the worse decides, in either order, and the combination is symmetric */
	CHECK(btsig_level(255, -15) == BTT_LINK_SIGNAL_FAIR && btsig_level(150, 0) == BTT_LINK_SIGNAL_FAIR && btsig_level(150, -15) == BTT_LINK_SIGNAL_FAIR &&
	      btsig_level(100, -15) == BTT_LINK_SIGNAL_WEAK && btsig_level(255, -30) == BTT_LINK_SIGNAL_WEAK && btsig_level(220, -2) == BTT_LINK_SIGNAL_GOOD,
	      "the worse of the two decides");
	CHECK(!strcmp(btsig_name(BTT_LINK_SIGNAL_EXCELLENT), "excellent") && !strcmp(btsig_name(BTT_LINK_SIGNAL_GOOD), "good") && !strcmp(btsig_name(BTT_LINK_SIGNAL_FAIR), "fair") &&
	      !strcmp(btsig_name(BTT_LINK_SIGNAL_WEAK), "weak") && !strcmp(btsig_name(BTT_LINK_SIGNAL_NONE), "measuring"), "the words");
	/* a headset at close range reads link quality 255 and signal strength 0: the best word */
	CHECK(btsig_level(255, 0) == BTT_LINK_SIGNAL_EXCELLENT, "link quality 255, signal strength 0: Excellent");
}

/* The average: the first real reading starts it, each later one moves it a quarter of the way, it never overshoots, and rounding is the same either side of zero. */
static void t_signal_smoothing(void)
{
	BtSignal s;
	unsigned lq, level;
	int rssi;

	btsig_reset(&s);
	s.linked = 1;
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(lq == 0 && rssi == 0 && level == BTT_LINK_SIGNAL_NONE, "nothing read: nothing said");
	btsig_feed(&s, 255, 0);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(lq == 255 && rssi == 0 && level == BTT_LINK_SIGNAL_EXCELLENT, "the first reading is the average (%u, %d, %u)", lq, rssi, level);
	/* the link gets bad: quality 100, signal -30: a quarter of the way each time, so a single bad reading does not flip the word */
	btsig_feed(&s, 100, -30);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(lq == 216 && rssi == -8 && level == BTT_LINK_SIGNAL_GOOD, "one bad reading: 255 -> 216, 0 -> -8, still Good (%u, %d, %u)", lq, rssi, level);
	unsigned n = 1;
	while (level != BTT_LINK_SIGNAL_WEAK && n < 40) {
		btsig_feed(&s, 100, -30);
		btsig_out(&s, &lq, &rssi, &level);
		n++;
	}
	CHECK(n >= 4 && n <= 12 && lq >= 100 && rssi >= -30, "a steady bad link reads Weak after %u readings (about %u s at the 3 s pace), never past the readings (%u, %d)", n, n * 3, lq, rssi);
	for (int i = 0; i < 80; i++)
		btsig_feed(&s, 100, -30);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(lq >= 100 && lq <= 103 && rssi <= -28 && rssi >= -30, "it settles at the readings (%u, %d)", lq, rssi);
	/* and back: quality 255 reads the other way */
	for (int i = 0; i < 80; i++)
		btsig_feed(&s, 255, 5);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(lq >= 250 && rssi >= 0 && level == BTT_LINK_SIGNAL_EXCELLENT, "a link that recovers reads Excellent again (%u, %d)", lq, rssi);
	/* rounding is symmetric: -0.5 and +0.5 are each rounded away from zero, 16 steps to the unit */
	CHECK(btsig_round(8) == 1 && btsig_round(-8) == -1 && btsig_round(7) == 0 && btsig_round(-7) == 0 && btsig_round(24) == 2 && btsig_round(-24) == -2, "rounding of the averages");
	/* a reading above 255 (the call fills a byte, so it cannot, but) is held at 255 */
	btsig_reset(&s);
	s.linked = 1;
	btsig_feed(&s, 1000, 0);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(lq == 255, "link quality is held at 255");
}

/* All zeros is the first call after a connection, not a measurement: it joins nothing, and says nothing until a real reading comes. */
static void t_signal_zeros(void)
{
	BtSignal s;
	unsigned lq, level;
	int rssi;

	btsig_reset(&s);
	s.linked = 1;
	btsig_feed(&s, 0, 0);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(!s.have && s.n_read == 1 && s.n_zero == 1 && level == BTT_LINK_SIGNAL_NONE && lq == 0 && rssi == 0, "the first call's zeros: still measuring");
	btsig_feed(&s, 0, 0);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(level == BTT_LINK_SIGNAL_NONE && s.n_zero == 2, "zeros again: still measuring");
	btsig_feed(&s, 255, 0);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(level == BTT_LINK_SIGNAL_EXCELLENT && lq == 255, "the first real reading is taken as it is, not mixed with the zeros");
	btsig_feed(&s, 0, 0);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(lq == 255 && level == BTT_LINK_SIGNAL_EXCELLENT, "a stray all-zero reading later changes nothing");
	/* a quality of zero with a signal reading, or a signal of zero with a quality, is a real reading */
	btsig_reset(&s);
	s.linked = 1;
	btsig_feed(&s, 0, -25);
	btsig_out(&s, &lq, &rssi, &level);
	CHECK(s.have && level == BTT_LINK_SIGNAL_WEAK && rssi == -25, "quality 0 with a signal reading is a real, Weak, reading");
	btsig_reset(&s);
	s.linked = 1;
	btsig_feed(&s, 255, 0);
	CHECK(s.have, "quality 255 with signal 0 (inside the range) is a real reading");
}

/* When a reading is asked for: at once on a new link, fast until the first real one (at most BTSIG_FAST_TRIES), then every BTSIG_PERIOD_MS; a link that goes or another device starts afresh. */
static void t_signal_schedule(void)
{
	BtSignal s;
	uint64_t t = 1000000;
	unsigned asked = 0;

	btsig_reset(&s);
	CHECK(!btsig_want(&s, 0, 0, 0, t), "no link: nothing asked");
	CHECK(btsig_want(&s, 1, 0x11, 0x22, t), "a new link: asked at once");
	CHECK(!btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_FAST_MS - 1) && btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_FAST_MS), "no real reading yet: asked again after the fast period");
	/* the readings stay zero: fast tries run out, then the normal pace */
	t += BTSIG_FAST_MS;
	asked = 2;
	while (asked < BTSIG_FAST_TRIES) {
		t += BTSIG_FAST_MS;
		CHECK(btsig_want(&s, 1, 0x11, 0x22, t), "fast try %u", asked + 1);
		asked++;
	}
	CHECK(!btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_FAST_MS - 1) && btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_FAST_MS), "the last fast try is followed by one more ask at the fast spacing");
	t += BTSIG_FAST_MS;
	CHECK(!btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_PERIOD_MS - 1) && btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_PERIOD_MS), "the fast tries are spent: the normal period");
	/* the first ask gave zeros and the second a real reading: from then on the normal period */
	btsig_reset(&s);
	t += 100000;
	CHECK(btsig_want(&s, 1, 0x11, 0x22, t), "a new start");
	btsig_feed(&s, 0, 0);
	CHECK(!btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_FAST_MS - 1) && btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_FAST_MS), "the second ask comes at the fast spacing");
	btsig_feed(&s, 250, 0);
	t += BTSIG_FAST_MS;
	CHECK(!btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_PERIOD_MS - 1) && btsig_want(&s, 1, 0x11, 0x22, t + BTSIG_PERIOD_MS), "with a real reading: every %u ms", (unsigned)BTSIG_PERIOD_MS);
	t += BTSIG_PERIOD_MS;
	/* the link goes: everything is forgotten; the same device again is a new link */
	CHECK(!btsig_want(&s, 0, 0, 0, t + 5000) && !s.have && !s.linked && s.n_read == 0, "the link went: forgotten");
	CHECK(btsig_want(&s, 1, 0x11, 0x22, t + 5001), "the same device again: a new link, asked at once");
	btsig_feed(&s, 250, 0);
	/* another device: the average is not carried over */
	CHECK(btsig_want(&s, 1, 0x33, 0x22, t + 5002) && !s.have && s.lo == 0x33, "another device: forgotten, asked at once");
}

/* The worker's pass with the information call faked: what the link information says, and when the call is made. */
static void t_signal_pass(void)
{
	BttLinkInfo o;
	uint8_t *conn = sslot(2);
	uint64_t t0;

	stream_world();
	g_log[0] = 0;
	g_clock_hook = fake_clock;
	g_us = t0 = 5000u * 1000000u;
	memset(&g_sigcall, 0, sizeof(g_sigcall));
	g_sigcall.found = 1;
	g_sigcall.lq = 255;
	g_sigcall.rssi = 0;
	g_sigcall.zero_first = 1;
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(g_sigcall.calls == 0 && !o.connected && !o.signal_level && !o.link_quality && !o.rssi, "no link: the call is not made");
	/* a link: the first call returns zeros, so the link is connected but still measuring */
	*(uintptr_t *)(g_seg1 + 0x36528) = (uintptr_t)conn;
	put32(conn, BTAAC_C_ADDR_LO, 0x11223344u);
	put32(conn, BTAAC_C_ADDR_HI, 0xAB005566u);
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(g_sigcall.calls == 1 && g_sigcall.lo == 0x11223344u && g_sigcall.hi == 0x5566u, "the call is made with the address as the Bluetooth code's calls take it: the low word, the high 16 bits (%d calls, %08x %08x)", g_sigcall.calls, (unsigned)g_sigcall.lo, (unsigned)g_sigcall.hi);
	CHECK(o.connected && o.signal_level == BTT_LINK_SIGNAL_NONE && !o.link_quality && !o.rssi, "connected, the zeros of the first call: measuring (level none)");
	btt_link_pass(1, BTT_LAT_STANDARD);
	CHECK(g_sigcall.calls == 1, "not asked on every pass (0.5 s)");
	g_us = t0 + BTSIG_FAST_MS * 1000u;
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(g_sigcall.calls == 2 && o.signal_level == BTT_LINK_SIGNAL_EXCELLENT && o.link_quality == 255 && o.rssi == 0, "the second call has the answers: Excellent, 255, 0");
	CHECK(log_has("link signal: excellent (link quality 255, signal 0"), "the log has the word and the readings behind it\n%s", g_log);
	/* a steady pace afterwards, and the average follows the readings */
	g_sigcall.lq = 150; g_sigcall.rssi = -15;
	unsigned calls = (unsigned)g_sigcall.calls;
	for (int i = 0; i < 12; i++) {
		g_us += 500000u;
		btt_link_pass(1, BTT_LAT_STANDARD);
	}
	CHECK((unsigned)g_sigcall.calls == calls + 2, "six seconds, two calls at the 3 s pace (%d more)", g_sigcall.calls - (int)calls);
	for (int i = 0; i < 160; i++) {
		g_us += 500000u;
		btt_link_pass(1, BTT_LAT_STANDARD);
	}
	btt_link_read(&o);
	CHECK(o.signal_level == BTT_LINK_SIGNAL_FAIR && o.link_quality >= 150 && o.link_quality <= 154 && o.rssi <= -14 && o.rssi >= -15, "a steady link at 150 / -15: Fair (%u, %d, %u)", o.link_quality, o.rssi, o.signal_level);
	/* a call that fails keeps the old reading and is logged once */
	g_sigcall.rc = (int)0x802F1503;
	calls = (unsigned)g_sigcall.calls;
	for (int i = 0; i < 20; i++) {
		g_us += 500000u;
		btt_link_pass(1, BTT_LAT_STANDARD);
	}
	btt_link_read(&o);
	CHECK((unsigned)g_sigcall.calls > calls && o.signal_level == BTT_LINK_SIGNAL_FAIR && log_has("information call failed (0x802f1503)"), "a failed call: the old reading stays, the failure is logged\n%s", g_log);
	g_sigcall.rc = 0;
	/* the voice profile comes from the link slot's connected-profile word (+0x3E10) */
	btt_link_read(&o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_NONE, "no voice profile yet");
	put32(conn, 0x3E10, 0x10000u);
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_HSP, "the console's own headset link");
	put32(conn, 0x3E10, 0x10001u);
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_HFP, "the hands-free link");
	/* the other versions' callers see exactly what they saw: their sizes, their fields */
	struct { BttLinkInfo i; unsigned int extra[4]; } big;
	memset(&big, 0xEE, sizeof(big));
	big.i.size = 56;
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 56 && big.i.connected == 1 && big.i.link_quality == 0xEEEEEEEEu &&
	      big.i.rssi == (int)0xEEEEEEEEu && big.i.signal_level == 0xEEEEEEEEu, "a version 3 caller (56 bytes): its fields, nothing written past them");
	memset(&big, 0xEE, sizeof(big));
	big.i.size = 52;
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 52 && big.i.addr_lo == 0x11223344u && big.i.codec_mask == 0xEEEEEEEEu, "a version 2 caller (52 bytes): the address, no codec list");
	memset(&big, 0xEE, sizeof(big));
	big.i.size = 44;
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 44 && big.i.addr_lo == 0xEEEEEEEEu && big.i.latency == BTT_LAT_STANDARD, "a version 1 caller (44 bytes): as before");
	memset(&big, 0xEE, sizeof(big));
	big.i.size = sizeof(big);
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 72 && big.i.version == 4 && big.i.signal_level == BTT_LINK_SIGNAL_FAIR && big.i.link_quality >= 150 && big.i.rssi < 0 &&
	      big.extra[0] == 0xEEEEEEEEu, "a version 4 caller (bigger than ours): the three new fields, the rest as it had it");
	memset(&big, 0xEE, sizeof(big));
	big.i.size = 60;
	CHECK(bttGetLinkInfo(&big.i) == BTT_OK && big.i.size == 60 && big.i.link_quality >= 150 && big.i.rssi == (int)0xEEEEEEEEu, "a caller of 60 bytes: link quality, not the fields after it");
	/* the link goes and comes back: measuring again, the old average is not shown */
	*(uintptr_t *)(g_seg1 + 0x36528) = 0;
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(!o.connected && !o.signal_level && !o.link_quality && !o.rssi, "no link: no signal");
	*(uintptr_t *)(g_seg1 + 0x36528) = (uintptr_t)conn;
	g_sigcall.zero_first = 1;
	g_sigcall.lq = 255; g_sigcall.rssi = 0;
	g_us += 500000u;
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(o.connected && o.signal_level == BTT_LINK_SIGNAL_NONE, "the link again: measuring, not the old Fair");
	/* Bluetooth Turbo off, or the Bluetooth code not recognised: nothing is asked of it */
	calls = (unsigned)g_sigcall.calls;
	g_us += 5000000u;
	btt_link_pass(0, BTT_LAT_STANDARD);
	g_seg1_ok = 0;
	btt_link_pass(1, BTT_LAT_STANDARD);
	g_seg1_ok = 1;
	btt_link_read(&o);
	CHECK((unsigned)g_sigcall.calls == calls && !o.signal_level, "off, or the code not recognised: no call");
	/* the call not found: no reading, said once, the rest of the link information is as before */
	memset(&g_sigcall, 0, sizeof(g_sigcall));
	g_log[0] = 0;
	g_sig_tried = 0;
	btsig_reset(&g_sig);
	g_us += 5000000u;
	btt_link_pass(1, BTT_LAT_STANDARD);
	btt_link_read(&o);
	CHECK(o.connected && o.signal_level == BTT_LINK_SIGNAL_NONE && g_sigcall.calls == 0 && log_has("information call was not found"), "the call not found: connected, no signal, logged\n%s", g_log);
	g_sigcall.found = 1;
	g_sig_tried = 0;
	g_clock_hook = NULL;
	*(uintptr_t *)(g_seg1 + 0x36528) = 0;
	btt_link_pass(1, BTT_LAT_STANDARD);
}

/* The link information composes the readings only for a link that is up. */
static void t_signal_compose(void)
{
	BtLinkIn in;
	BttLinkInfo o;

	memset(&in, 0, sizeof(in));
	in.on = 1; in.recognised = 1; in.sig_lq = 200; in.sig_rssi = -7; in.sig_level = BTT_LINK_SIGNAL_GOOD;
	btlink_compose(&in, &o);
	CHECK(!o.link_quality && !o.rssi && !o.signal_level, "not connected: no signal fields");
	in.connected = 1;
	btlink_compose(&in, &o);
	CHECK(o.link_quality == 200 && o.rssi == -7 && o.signal_level == BTT_LINK_SIGNAL_GOOD, "connected: the readings and the word");
	in.on = 0;
	btlink_compose(&in, &o);
	CHECK(!o.link_quality && !o.rssi && !o.signal_level && !o.voice_profile, "off: nothing");
	/* the voice profile: from the link slot's connected-profile word; hands-free wins over the headset profile; only while connected */
	in.on = 1; in.connected = 1;
	in.conn_word = 0;
	btlink_compose(&in, &o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_NONE, "no voice profile");
	in.conn_word = 0x1u;
	btlink_compose(&in, &o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_HFP, "hands-free");
	in.conn_word = 0x10000u;
	btlink_compose(&in, &o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_HSP, "the console's own headset link only");
	in.conn_word = 0x10001u;
	btlink_compose(&in, &o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_HFP, "both: hands-free");
	in.conn_word = 0x30002u & ~0x10000u;   /* other profile bits are not voice */
	btlink_compose(&in, &o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_NONE, "other profile bits are no voice profile");
	in.connected = 0; in.conn_word = 0x1u;
	btlink_compose(&in, &o);
	CHECK(o.voice_profile == BTT_LINK_VOICE_NONE, "not connected: none");
}

/* ---- the access unit cap ------------------------------------------------------------------------------------------------ */

/* An access unit longer than one packet takes is cut into pieces, and a headset stuttered on it (9 units in 143 s at 256 kb/s, a 895-byte limit, units up to 989
 * bytes). The encoder is held to what one packet takes (btaac_enc_limit): first the arithmetic, then the real encoder through the engine at every bit rate and limit. */
static void t_cap_math(void)
{
	static uint8_t au[BTAAC_AU_MAX], el[BTAAC_LATM_MAX];

	memset(au, 0x5A, sizeof(au));
	for (uint32_t n = 1; n <= BTAAC_AU_MAX; n++)
		CHECK(btaac_latm_len(n) == btaac_latm_wrap(el, sizeof(el), au, n), "the element of %u bytes is %u, btaac_latm_wrap makes %u", (unsigned)n, (unsigned)btaac_latm_len(n),
		      (unsigned)btaac_latm_wrap(el, sizeof(el), au, n));
	/* the cap is the longest unit whose element and RTP header fit the limit, exactly, at every believable limit */
	for (uint32_t mtu = BTAAC_MTU_MIN; mtu <= BTAAC_MTU_MAX; mtu++) {
		uint32_t cap = btaac_au_cap(mtu);

		CHECK(cap > 0 && BTAAC_RTP_HDR + btaac_latm_len(cap) <= mtu, "limit %u: a unit of %u bytes does not fit", (unsigned)mtu, (unsigned)cap);
		CHECK(BTAAC_RTP_HDR + btaac_latm_len(cap + 1u) > mtu, "limit %u: %u bytes is not the longest that fits", (unsigned)mtu, (unsigned)cap);
		CHECK(btaac_latm_pieces(btaac_latm_len(cap), mtu) == 1, "limit %u: the capped unit goes in one packet", (unsigned)mtu);
	}
	CHECK(btaac_au_cap(895) == 873 && btaac_au_cap(672) == 651 && btaac_au_cap(1021) == 999 && btaac_au_cap(256) == 237 && btaac_au_cap(0) == 0 && btaac_au_cap(19) == 0,
	      "the caps of the reference headset (895), the default (672) and the ends: %u %u", (unsigned)btaac_au_cap(895), (unsigned)btaac_au_cap(672));
	/* the highest bit rate a cap carries: its frame, the margin and the smallest reservoir fit, and one more bit a second would not */
	for (uint32_t rate = 44100; rate <= 48000; rate += 3900)
		for (uint32_t cap = 1; cap <= BTAAC_AU_MAX; cap++) {
			uint32_t bps = btaac_cap_max_bps(cap, rate);

			if (!bps) {
				CHECK(cap <= BTAAC_CAP_MARGIN + BTAAC_RES_MIN + 1u || btaac_frame_bytes(4000, rate) + BTAAC_CAP_MARGIN + BTAAC_RES_MIN > cap, "cap %u at %u Hz: no bit rate, yet room", (unsigned)cap, (unsigned)rate);
				continue;
			}
			CHECK(btaac_frame_bytes(bps, rate) + BTAAC_CAP_MARGIN + BTAAC_RES_MIN <= cap, "cap %u at %u Hz: %u b/s does not fit", (unsigned)cap, (unsigned)rate, (unsigned)bps);
			CHECK(btaac_frame_bytes(bps + 1u, rate) + BTAAC_CAP_MARGIN + BTAAC_RES_MIN > cap, "cap %u at %u Hz: %u b/s is not the highest", (unsigned)cap, (unsigned)rate, (unsigned)bps);
			CHECK(btaac_cap_reservoir(cap, bps, rate) >= BTAAC_RES_MIN, "cap %u at %u Hz: the reservoir at %u b/s", (unsigned)cap, (unsigned)rate, (unsigned)bps);
		}
	CHECK(btaac_frame_bytes(256000, 48000) == 683 && btaac_frame_bytes(320000, 48000) == 854 && btaac_frame_bytes(128000, 48000) == 342, "the padded frame: 682.7, 853.3 and 341.3 bytes, up to a whole byte");
	CHECK(btaac_cap_max_bps(873, 48000) >= 320000 && btaac_cap_max_bps(873, 48000) < 330000, "the reference headset carries 320 kb/s: %u", (unsigned)btaac_cap_max_bps(873, 48000));
	CHECK(btaac_cap_max_bps(651, 48000) >= 128000 && btaac_cap_max_bps(651, 48000) < 256000, "the default limit does not carry 256 kb/s: %u", (unsigned)btaac_cap_max_bps(651, 48000));
	/* an unbelievable limit is the default one (the engine believes btaac_mtu_sane only) */
	CHECK(btaac_au_cap(btaac_mtu_sane(100)) == 651 && btaac_au_cap(btaac_mtu_sane(0xFFFFFFFFu)) == 651, "limits that are not believed are the default's cap");
}

/* The test signals (host only, floating point): noise (the probe's, the hardest signal), a music-like mix (a sweep from 50 Hz to 18 kHz with tones, noise and noise
 * bursts), and notes (harmonic notes with decays, a kick and a hat, a quiet second every six: the reservoir fills, then is spent). */
static const double PI_ = 3.14159265358979323846;
static void sig_mix(uint32_t n, int16_t *l, int16_t *r)
{
	double t = (double)n / 48000.0, phase = 2 * PI_ * 50.0 * 10.0 / __builtin_log(360.0) * (__builtin_pow(360.0, t / 10.0) - 1.0);
	double s = 9000 * __builtin_sin(phase) + 5000 * __builtin_sin(2 * PI_ * 220 * t) + 3000 * __builtin_sin(2 * PI_ * 3300 * t);
	double nl = (int16_t)(btaac_hash32(n * 2u) >> 16) / 32768.0 * 2500, nr = (int16_t)(btaac_hash32(n * 2u + 1u) >> 16) / 32768.0 * 2500;
	double burst = __builtin_fmod(t, 0.5) < 0.01 ? (int16_t)(btaac_hash32(n * 7u) >> 16) / 32768.0 * 14000 : 0;

	*l = (int16_t)(s * 0.8 + nl + burst);
	*r = (int16_t)(s * 0.6 + nr + burst * 0.5 + 2000 * __builtin_sin(2 * PI_ * 1500 * t));
}
static void sig_notes(uint32_t n, int16_t *l, int16_t *r)
{
	static const int scale[8] = { 0, 2, 4, 7, 9, 12, 7, 4 };
	double t = (double)n / 48000.0, tn = t - (int)(t / 0.25) * 0.25, v = 0, env = __builtin_exp(-tn * 6), kt = __builtin_fmod(t, 0.5);
	int note = (int)(t / 0.25);
	double f0 = 110 * __builtin_pow(2, (scale[note & 7] + ((note / 8) % 3) * 5) / 12.0);
	double hat = __builtin_fmod(t, 0.125) < 0.03 ? (int16_t)(btaac_hash32(n * 5u) >> 16) / 32768.0 * (__builtin_fmod(t, 0.25) < 0.125 ? 9000 : 5000) : 0;
	double kick = kt < 0.08 ? 14000 * __builtin_sin(2 * PI_ * (60 + 300 * __builtin_exp(-kt * 40)) * kt) : 0;
	double gap = __builtin_fmod(t, 6.0) > 5.0 ? 0.05 : 1.0;

	for (int k = 1; k < 24; k++)
		v += __builtin_sin(2 * PI_ * f0 * k * t + k) / k;
	*l = (int16_t)((v * 5500 * env + hat + kick) * gap);
	*r = (int16_t)((v * 4800 * env + hat * 0.8 + kick) * gap);
}
typedef void (*SigFn)(uint32_t, int16_t *, int16_t *);

typedef struct { uint32_t aus, bytes, max, over; double mean_bps; uint32_t bps, cap, res, cap_state; } CapRun;

/* 10 s of a signal through a session (470 units) at `kbps` with the packet limit `mtu` (0 = no cap); the units are also written as a LOAS stream to `path` (if not NULL). */
static CapRun cap_run(SigFn sig, uint32_t kbps, uint32_t mtu, const char *path)
{
	static int16_t blk[512 * 2];
	static uint8_t au[BTAAC_AU_MAX], el[BTAAC_LATM_MAX], loas[BTAAC_LATM_MAX + 3];
	const uint32_t frames = 470u * BTAAC_FRAME;
	CapRun r;
	uint32_t pushed = 0, seq;
	FILE *f = path ? fopen(path, "wb") : NULL;

	memset(&r, 0, sizeof(r));
	CHECK(btaac_start(kbps, 48000, 2, 0, 0, mtu) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	BtAacInfo i = info();

	r.bps = i.bps; r.cap = i.au_cap; r.res = i.reservoir; r.cap_state = i.cap_state;
	while (pushed < frames || r.aus < 470) {
		if (pushed < frames) {
			for (uint32_t k = 0; k < 512; k++)
				sig(pushed + k, &blk[2 * k], &blk[2 * k + 1]);
			if (btaac_push_pcm(blk, 512) == 512)
				pushed += 512;
		}
		uint32_t n = btaac_pop_au(au, sizeof(au), &seq), len;

		if (!n) {
			usleep(300);
			continue;
		}
		CHECK(seq == r.aus, "unit %u numbered %u", (unsigned)r.aus, (unsigned)seq);
		len = btaac_latm_wrap(el, sizeof(el), au, n);
		CHECK(len, "unit %u wraps", (unsigned)r.aus);
		if (mtu && BTAAC_RTP_HDR + len > btaac_mtu_sane(mtu))
			r.over++;
		if (f)
			fwrite(loas, 1, loas_wrap(loas, el, len), f);
		r.aus++;
		r.bytes += n;
		if (n > r.max)
			r.max = n;
	}
	r.mean_bps = (double)r.bytes * 8.0 / ((double)r.aus * 1024.0 / 48000.0);
	if (f)
		fclose(f);
	btaac_stop();
	wait_for(c_idle, "the stop");
	return r;
}

/* The decoder (ffmpeg) takes the stream, and says what the signal said: as many frames as units, and the level within 30% of the input's. */
static void cap_decode_check(SigFn sig, const char *path, uint32_t aus, const char *what)
{
	uint32_t frames = 0;
	int16_t *pcm = ffmpeg_decode(path, &frames);
	double e_in = 0, e_out = 0;

	CHECK(pcm, "%s: ffmpeg refused the stream", what);
	CHECK(frames >= (aus - 1) * 1024 && frames <= (aus + 1) * 1024, "%s: %u frames for %u units", what, (unsigned)frames, (unsigned)aus);
	for (uint32_t n = 16u * 1024u; n < frames - 2048u; n++) {
		int16_t l, r;

		sig(n, &l, &r);
		e_in += (double)l * l + (double)r * r;
		e_out += (double)pcm[2 * n] * pcm[2 * n] + (double)pcm[2 * n + 1] * pcm[2 * n + 1];
	}
	/* the decoder's output lags the input by its delay, which does not move the level of a long stretch much */
	CHECK(e_out > 0.5 * e_in && e_out < 1.5 * e_in, "%s: the decoded level is %.2f of the input's", what, e_out / (e_in > 0 ? e_in : 1));
	free(pcm);
	unlink(path);
}

static void t_cap_encode(void)
{
	static const struct { const char *name; SigFn fn; } sigs[] = { { "noise", btaac_sig_noise }, { "music mix", sig_mix }, { "notes", sig_notes } };
	static const uint32_t rates[] = { 128, 192, 256, 320 }, mtus[] = { 895, 672 };
	int ff = have_ffmpeg();
	char path[128];
	uint32_t worst = 0;

	engine_up();
	run_selftest();
	snprintf(path, sizeof(path), "build/test_cap_%d.loas", (int)getpid());
	printf("  access unit cap, 10 s of each signal (470 units): the longest unit, the mean bit rate, ffmpeg\n");
	for (unsigned m = 0; m < 2; m++)
		for (unsigned s = 0; s < 3; s++)
			for (unsigned k = 0; k < 4; k++) {
				SigFn fn = sigs[s].fn;
				uint32_t mtu = mtus[m], kbps = rates[k], cap = btaac_au_cap(mtu), want = kbps * 1000u, top = btaac_cap_max_bps(cap, 48000);
				char what[96];
				CapRun r;

				if (top && top < want)
					want = top;                                  /* the limit's own highest bit rate */
				snprintf(what, sizeof(what), "%s at %u kb/s, limit %u", sigs[s].name, (unsigned)kbps, (unsigned)mtu);
				r = cap_run(fn, kbps, mtu, ff ? path : NULL);
				if (ff)
					cap_decode_check(fn, path, r.aus, what);
				printf("    %-9s %3u kb/s limit %3u: cap %3u bytes, reservoir %3u, longest %3u, over %u, mean %.1f kb/s (target %u)%s\n", sigs[s].name, (unsigned)kbps, (unsigned)mtu,
				       (unsigned)r.cap, (unsigned)r.res, (unsigned)r.max, (unsigned)r.over, r.mean_bps / 1000.0, (unsigned)(r.bps / 1000u), ff ? ", decoded" : "");
				CHECK(r.aus == 470, "%s: %u units", what, (unsigned)r.aus);
				CHECK(r.cap == cap && r.cap_state == BTAAC_CAP_APPLIED && r.bps == want, "%s: cap %u (%u), state %u, bit rate %u (wanted %u)", what, (unsigned)r.cap, (unsigned)cap, (unsigned)r.cap_state,
				      (unsigned)r.bps, (unsigned)want);
				CHECK(r.res == btaac_cap_reservoir(cap, r.bps, 48000) && r.res >= BTAAC_RES_MIN, "%s: reservoir %u", what, (unsigned)r.res);
				CHECK(r.over == 0 && r.max <= cap, "%s: the longest unit %u bytes, %u over the cap of %u", what, (unsigned)r.max, (unsigned)r.over, (unsigned)cap);
				CHECK(r.max <= btaac_frame_bytes(r.bps, 48000) + r.res, "%s: the longest unit %u is over the frame and the reservoir (%u + %u)", what, (unsigned)r.max,
				      (unsigned)btaac_frame_bytes(r.bps, 48000), (unsigned)r.res);
				CHECK(r.mean_bps > 0.97 * r.bps && r.mean_bps < 1.03 * r.bps, "%s: the mean bit rate %.0f against %u", what, r.mean_bps, (unsigned)r.bps);
				if (mtu == 895)
					CHECK(r.bps == kbps * 1000u, "%s: the reference headset takes the whole bit rate (%u)", what, (unsigned)r.bps);
				if (r.max > worst)
					worst = r.max;
			}
	CHECK(worst > 0, "something was encoded");
	if (!ff)
		printf("  (ffmpeg is not on this machine: the units were checked against the cap, not decoded)\n");
	fflush(stdout);                                     /* the scenario ends with _exit */
	engine_down();
}

/* Without the cap the same signal does go over the reference headset's limit, and the encoder's own reservoir (853 bytes) is what lets it: the test can fail. */
static void t_cap_needed(void)
{
	engine_up();
	run_selftest();
	CapRun r = cap_run(sig_notes, 256, 0, NULL);

	CHECK(r.cap_state == BTAAC_CAP_NONE && r.cap == 0 && r.res == 0 && g_limit_calls == 0, "no limit given: no cap, the encoder untouched");
	CHECK(r.max > btaac_au_cap(895), "without a cap the notes reach %u bytes, over the cap of %u", (unsigned)r.max, (unsigned)btaac_au_cap(895));
	CapRun c = cap_run(sig_notes, 256, 895, NULL);

	CHECK(c.max <= btaac_au_cap(895) && c.over == 0 && c.bytes > 0, "with it they stay under: %u", (unsigned)c.max);
	/* the average does not change: the same stream length to the byte (what the reservoir does not take is padding) */
	CHECK(c.bytes >= r.bytes - r.bytes / 50 && c.bytes <= r.bytes + r.bytes / 50, "the stream is %u bytes capped, %u not", (unsigned)c.bytes, (unsigned)r.bytes);
	engine_down();
}

/* A cap that cannot be applied (the encoder's record is not what the limit was written for) does not stop the sound: the session runs, says so, and the sender's safety net cuts a long unit. */
static void t_cap_not_applied(void)
{
	engine_up();
	run_selftest();
	g_limit_refuse = 1;
	CapRun r = cap_run(sig_notes, 256, 895, NULL);

	CHECK(r.cap == 873 && r.cap_state == BTAAC_CAP_FAILED && g_limit_calls == 1 && r.aus == 470, "a refused limit: the session runs (cap %u, state %u)", (unsigned)r.cap, (unsigned)r.cap_state);
	CHECK(r.max > 873, "and the encoder was left as it was (longest %u)", (unsigned)r.max);
	CHECK(log_has("NOT APPLIED"), "the log says so");
	g_limit_refuse = 0;
	engine_down();
}

/* The safety net: a unit that does not fit the link's limit goes out in pieces, every piece in the same call of the send hook, never one block later. The session here is
 * made for no limit (mtu 0) and the link takes 300 bytes, so most units are cut. */
static void t_split_one_call(void)
{
	static int16_t blk[512 * 2];
	uint8_t *conn = slot(1);
	uint32_t calls_cut = 0, calls_whole = 0, pieces_total = 0, last_ts = 0;
	int have_ts = 0;

	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 264, 0, 0) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	media_world(300, BTAAC_N_CONFIGURED);
	for (uint32_t b = 0; b < 400; b++) {
		int before = g_npkts, calls_before = g_send_calls;

		tone_block(b * 512, blk);
		btaac_pcm_block(blk);
		int r = btaac_send_hook(conn, g_sbc, 600, 1);
		int added = g_npkts - before;

		usleep(900);
		if (!added)
			continue;
		CHECK(r >= 0 && g_send_calls - calls_before == added, "block %u: %d packets for %d calls of the Bluetooth code's own sender", (unsigned)b, added, g_send_calls - calls_before);
		/* the call's packets are whole units: the first starts a unit (the packet before ended one), the last ends it (the marker), none in between does */
		CHECK(before == 0 || (g_pkts[before - 1].data[1] & 0x80), "block %u: the call starts on a unit's first piece", (unsigned)b);
		for (int k = 0; k < added; k++)
			CHECK(((g_pkts[before + k].data[1] & 0x80) != 0) == (k == added - 1), "block %u: piece %d of %d: the marker on the last piece only, the whole unit in this call", (unsigned)b, k, added);
		uint32_t ts = (uint32_t)g_pkts[before].data[4] << 24 | (uint32_t)g_pkts[before].data[5] << 16 | (uint32_t)g_pkts[before].data[6] << 8 | g_pkts[before].data[7];

		for (int k = 1; k < added; k++)
			CHECK(!memcmp(g_pkts[before + k].data + 4, g_pkts[before].data + 4, 4), "block %u: every piece carries the unit's timestamp", (unsigned)b);
		CHECK(!have_ts || ts == last_ts + 1024u, "block %u: the next unit follows at +1024 (%u after %u)", (unsigned)b, (unsigned)ts, (unsigned)last_ts);
		last_ts = ts;
		have_ts = 1;
		/* the flag the code counts in flight with: on the last piece of the unit, once up and once down */
		for (int k = 0; k < added; k++)
			CHECK(g_pkts[before + k].flag == (k == added - 1 ? 1u : 0u), "block %u: the in-flight flag on the last piece only", (unsigned)b);
		if (added > 1) {
			calls_cut++;
			pieces_total += (uint32_t)added;
		} else {
			calls_whole++;
		}
	}
	CHECK(calls_cut > 20 && calls_whole + calls_cut == btaac_neg.n_sent, "%u cut units and %u whole ones, %u counted sent", (unsigned)calls_cut, (unsigned)calls_whole, (unsigned)btaac_neg.n_sent);
	CHECK(btaac_neg.n_over == calls_cut && btaac_neg.n_back == pieces_total - calls_cut && btaac_neg.n_pieces == (uint32_t)g_npkts,
	      "the readings: %u units over the limit (%u cut), %u pieces back to back (%u), %u packets (%d)", (unsigned)btaac_neg.n_over, (unsigned)calls_cut, (unsigned)btaac_neg.n_back,
	      (unsigned)(pieces_total - calls_cut), (unsigned)btaac_neg.n_pieces, g_npkts);
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

/* The same hook with the session held to the link's limit: no unit is cut, and the readings say 0. */
static void t_no_split_with_cap(void)
{
	engine_up();
	run_selftest();
	CHECK(btaac_start(256, 48000, 2, 264, 0, 895) == BTAAC_OK, "start");
	wait_for(c_running, "the session");
	media_world(895, BTAAC_N_CONFIGURED);
	run_sender(500, 895);
	CHECK(btaac_neg.n_sent > 100 && btaac_neg.n_pieces == btaac_neg.n_sent && btaac_neg.n_over == 0 && btaac_neg.n_back == 0 && btaac_neg.max_au_len <= btaac_au_cap(895),
	      "%u units in %u packets, %u over, %u back to back, the longest %u bytes", (unsigned)btaac_neg.n_sent, (unsigned)btaac_neg.n_pieces, (unsigned)btaac_neg.n_over, (unsigned)btaac_neg.n_back,
	      (unsigned)btaac_neg.max_au_len);
	btaac_stop();
	wait_for(c_idle, "the stop");
	engine_down();
}

/* ---- the media hook's pacing (kernel/btaac_pace.h; test/test_btaac_pace.c has the arithmetic): the real hook, the real rings, units put by hand ---------------------------------- */

static void put_unit(uint32_t n)
{
	uint8_t au[200];

	memset(au, (int)n, sizeof(au));
	CHECK(btaac_au_put(au, sizeof(au)) == 1, "the output ring has room for unit %u", (unsigned)n);
}

static void pacing_world(uint32_t preroll, uint32_t keep)
{
	media_world(1021, BTAAC_N_CONFIGURED);
	btaac_neg.preroll = preroll;
	btaac_neg.keep = keep;
	btaac_pace_reset((BtAacPace *)&btaac_neg.pace);
	btaac_ring_open(2);
}

static void t_media_pacing(void)
{
	uint8_t *conn = slot(1);
	uint32_t closest = 99, last = 0;

	pacing_world(2, 11);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_npkts == 0 && btaac_neg.n_waiting == 1, "nothing ready: nothing sent, the code is told nothing went");
	put_unit(0);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_npkts == 0 && btaac_neg.n_waiting == 2, "one ready, two wanted: still nothing (the start waits for the pre-roll)");
	put_unit(1);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) >= 0 && g_npkts == 1 && btaac_neg.n_sent == 1, "two ready: the first goes");
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0 && g_npkts == 1 && btaac_neg.n_spaced == 1, "the next call is within the spacing: nothing, though a unit waits");
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) >= 0 && g_npkts == 2, "two calls after the first: the slack goes");
	/* a steady stream: a unit every second call, however it is ready */
	g_npkts = 0;
	for (uint32_t c = 1; c <= 100; c++) {
		if (c % 2 == 1)
			put_unit(100 + c);
		if (btaac_send_hook(conn, g_sbc, 600, 1) >= 0) {
			if (last && c - last < closest)
				closest = c - last;
			last = c;
		}
	}
	CHECK(g_npkts >= 48 && closest == 2 && btaac_neg.pace.underruns == 0, "100 calls: %d units, never closer than %u calls, no underrun", g_npkts, (unsigned)closest);
	CHECK(btaac_send_in == 0, "nobody is left inside the hook");
	/* with no pre-roll the old behaviour: a unit on every call that has one */
	pacing_world(0, 11);
	put_unit(0); put_unit(1); put_unit(2);
	g_npkts = 0;
	for (int c = 0; c < 3; c++)
		CHECK(btaac_send_hook(conn, g_sbc, 600, 1) >= 0, "no pre-roll: call %d sends", c);
	CHECK(g_npkts == 3 && btaac_neg.n_spaced == 0 && btaac_neg.n_waiting == 0, "three units on three calls");
}

static void t_media_burst_after_pause(void)
{
	uint8_t *conn = slot(1);
	uint32_t before, last = 0, closest = 99, calls_with_send = 0;

	/* the sender's blocks go on while the engine is held off the CPU for 300 ms; then the engine makes the backlog at once */
	pacing_world(2, 11);
	put_unit(0); put_unit(1);
	for (uint32_t c = 1; c <= 40; c++) {
		if (c % 2 == 1 && c > 2)
			put_unit(c);
		btaac_send_hook(conn, g_sbc, 600, 1);
	}
	CHECK(btaac_neg.pace.underruns == 0 && g_npkts >= 18, "a steady start: %d units, no underrun", g_npkts);
	while (btaac_au_depth())
		btaac_send_hook(conn, g_sbc, 600, 1);
	before = (uint32_t)g_npkts;
	for (int c = 0; c < 28; c++)
		CHECK(btaac_send_hook(conn, g_sbc, 600, 1) < 0, "the stall: call %d sends nothing", c);
	CHECK((uint32_t)g_npkts == before && btaac_neg.pace.underruns == 1 && btaac_neg.pace.run == 0, "28 calls of a dry ring: nothing sent, one underrun, waiting for the pre-roll");
	for (uint32_t u = 0; u < 14; u++)
		put_unit(1000 + u);
	before = (uint32_t)g_npkts;
	for (uint32_t c = 1; c <= 30; c++) {
		if (btaac_send_hook(conn, g_sbc, 600, 1) >= 0) {
			if (last && c - last < closest)
				closest = c - last;
			last = c;
			calls_with_send++;
		}
	}
	CHECK(calls_with_send == (uint32_t)g_npkts - before && closest == 2, "the backlog goes out one unit per two calls, never closer: %u calls apart at the closest", (unsigned)closest);
	CHECK(calls_with_send == 11 && btaac_neg.pace.prerolls == 1 && btaac_neg.n_trim == 1 && btaac_neg.n_trimmed_units == 3,
	      "Standard: 14 made, 3 dropped to the limit of 11, the 11 sent in 30 calls (%u), one pre-roll after the dry ring", (unsigned)calls_with_send);
	/* the same backlog at Lowest (3 units): the oldest are dropped down to the limit, the sink's timeline stays whole, and the rest still goes one per two calls */
	pacing_world(2, 3);
	for (uint32_t u = 0; u < 14; u++)
		put_unit(u);
	g_npkts = 0;
	last = 0;
	closest = 99;
	for (uint32_t c = 1; c <= 12; c++)
		if (btaac_send_hook(conn, g_sbc, 600, 1) >= 0) {
			if (last && c - last < closest)
				closest = c - last;
			last = c;
		}
	CHECK(btaac_neg.n_trim >= 1 && btaac_neg.n_trimmed_units == 11 && closest == 2, "Lowest: 11 of 14 dropped, 3 kept, then one per two calls (closest %u)", (unsigned)closest);
	CHECK(g_npkts == 3, "3 units sent in 12 calls from 3 kept (%d)", g_npkts);
}

static void t_media_preroll_vs_trim(void)
{
	uint8_t *conn = slot(1);

	/* the trim never leaves fewer than the pre-roll asks: a pre-roll of 6 at Lowest (3) keeps 6 */
	pacing_world(6, 3);
	for (uint32_t u = 0; u < 9; u++)
		put_unit(u);
	CHECK(btaac_send_hook(conn, g_sbc, 600, 1) >= 0 && g_npkts == 1, "the pre-roll of 6 is met: sending");
	CHECK(btaac_neg.n_trimmed_units == 3 && btaac_au_depth() == 5, "9 waiting, 6 kept (not 3), one sent: dropped %u, left %u", (unsigned)btaac_neg.n_trimmed_units, (unsigned)btaac_au_depth());
	/* the worker publishes the setting every pass; the setter takes 0..6 only */
	CHECK(btt_aac_stream_set_preroll(6) == 1 && btaac_neg.preroll == 6 && btt_aac_stream_set_preroll(0) == 1 && btaac_neg.preroll == 0, "the setter takes 0 and 6");
	CHECK(btt_aac_stream_set_preroll(7) == 0 && btt_aac_stream_set_preroll(-1) == 0 && btaac_neg.preroll == 0, "and refuses 7 and -1");
}

/* ---- harness ---------------------------------------------------------------------------------------------------------- */

typedef struct { const char *name; void (*fn)(void); } Scn;
#include "test_btaptx_stream.h"
#include "test_btldac_stream.h"
static const BtLdacApi *fake_ld_get_api(void)
{
	static BtLdacApi bad;

	if (g_ld_find == 2) {
		bad = k_ld_api;
		bad.version = BTLDAC_API_VERSION + 1;
		return &bad;
	}
	return &k_ld_api;
}
static const BtAptxApi *fake_apt_get_api(void)
{
	static BtAptxApi bad;

	if (g_apt_find == 2) {
		bad = k_apt_api;
		bad.version = BTAPTX_API_VERSION + 1;
		return &bad;
	}
	return &k_apt_api;
}

static const Scn k_scn[] = {
	{ "endpoint choice from the capability bytes", t_sep_choice },
	{ "configuration bytes", t_config_bytes },
	{ "the Bluetooth code's bytes are checked", t_gate },
	{ "the AudioMuxElement and the RTP header", t_latm },
	{ "cutting an element to the link's limit", t_fragment },
	{ "real units decoded by ffmpeg (whole and from pieces)", t_ffmpeg_decode },
	{ "negotiation: the reference headset", t_negotiation },
	{ "negotiation: an SBC-only sink", t_negotiation_sbc_only },
	{ "codec lists: read from the replies, whatever is being sent", t_codec_lists },
	{ "negotiation: rules and refusals", t_negotiation_rules },
	{ "fallback when the sink rejects AAC", t_fallback },
	{ "a mark is for one codec; a local failure marks nothing", t_ban_per_codec },
	{ "fallback on a general reject", t_fallback_general_reject },
	{ "acceptor: the reference headset opens the stream, start to finish", t_acc_flow },
	{ "acceptor: every configuration refused here, and the code's refusals", t_acc_rejects },
	{ "acceptor: SBC sinks, AAC off, endpoint 3 remembered", t_acc_sbc },
	{ "acceptor: no sender, unreadable pages, other connections", t_acc_edges },
	{ "Music Source Only: DISCOVER without the sink, the sink's configuration rejected, stock when off", t_src_only },
	{ "media hook: passes SBC through, never sends SBC to AAC", t_media_passthrough },
	{ "media hook: a session at a limit of 672", t_media_672 },
	{ "media hook: a session at a limit of 1021", t_media_1021 },
	{ "media hook: a session at a limit of 300", t_media_300 },
	{ "media hook: a limit that is not believed", t_media_unbelievable_mtu },
	{ "media hook: a cut unit counts once in flight (the flag on the last piece)", t_split_count },
	{ "media hook: the engine's output is bounded by the latency level", t_ring_bound },
	{ "media hook: edges", t_media_edges },
	{ "media hook: pacing and the pre-roll", t_media_pacing },
	{ "media hook: a burst of units after a pause goes out one per two calls", t_media_burst_after_pause },
	{ "media hook: the trim never goes below the pre-roll", t_media_preroll_vs_trim },
	{ "worker: Music Source Only in force, logged, off with the master switch and at module stop", t_stream_src_only },
	{ "worker: the hooks go in only when AAC is wanted", t_stream_install },
	{ "worker: the reader's own hook is not refused by the gate when a codec is wanted", t_stream_reader_then_codec },
	{ "worker: a failure of the module's own marks no headphones", t_stream_local_failure },
	{ "worker: refusals are not retried until off and on", t_stream_refusals },
	{ "worker: the session follows the stream, gates first", t_stream_session },
	{ "worker: a link whose limit is not known: the default's cap and rate", t_stream_session_default_limit },
	{ "worker: standby ends the session; a cancelled attempt does not", t_stream_standby },
	{ "worker: an engine failure ends the sound and bans the device", t_stream_engine_failure },
	{ "worker: a refused session", t_stream_start_refused },
	{ "worker: module stop", t_stream_restore },
	{ "worker: what the hooks did, in the log", t_stream_log },
	{ "access unit cap: the arithmetic", t_cap_math },
	{ "access unit cap: every bit rate and limit, three signals, decoded", t_cap_encode },
	{ "access unit cap: without it the units do go over", t_cap_needed },
	{ "access unit cap: a limit the encoder refuses does not stop the sound", t_cap_not_applied },
	{ "safety net: a cut unit goes out whole in one call", t_split_one_call },
	{ "safety net: with the cap nothing is cut", t_no_split_with_cap },
	{ "START retry: a rejected START is asked for again after 0.5 s, and the stream starts", t_retry_after_reject },
	{ "START retry: the headphones start it themselves inside the grace: no call", t_retry_headset_starts_first },
	{ "START retry: three rejected retries (0.5, 1, 2 s) mark the device, no fourth call", t_retry_three_rejects },
	{ "START retry: SBC, the voice link, a started stream and every other state: nothing is touched", t_retry_never_touches },
	{ "START retry: no more calls once the console stops wanting the sound, or a call sent nothing", t_retry_stops_when_not_wanted },
	{ "START retry: the wait is forgotten with the link or a new negotiation", t_retry_resets_with_the_link },
	{ "watchdog: a configured stream that never starts is marked once, nothing written", t_watchdog },
	{ "codec fallback notice: the stream code reports a rejected configuration, a given-up START, a stream that never starts", t_failure_report },
	{ "START retry: the Bluetooth code's start call not found", t_retry_without_the_call },
	{ "link information: what is said", t_link_info },
	{ "link information: the pass and the call", t_link_pass },
	{ "signal: the word from link quality and signal strength, at every edge", t_signal_levels },
	{ "signal: the smoothing", t_signal_smoothing },
	{ "signal: all zeros is nothing yet", t_signal_zeros },
	{ "signal: when a reading is asked for", t_signal_schedule },
	{ "signal: the worker's pass, the link information of every version", t_signal_pass },
	{ "signal: what the link information composes", t_signal_compose },
	{ "aptX: the endpoint choice, the configuration bytes and every configuration refused", t_apt_sep },
	{ "aptX: the negotiation when the Vita opens the stream, the SBC choice kept, the fallback", t_apt_negotiation },
	{ "aptX: the headphones open the stream (endpoint 4), what is refused, the commands", t_apt_acceptor },
	{ "aptX: the media packet is the encoder's 512 bytes, no header; every edge", t_apt_media },
	{ "aptX: the worker's session, the gates, standby, a refused session, module stop", t_apt_worker },
	{ "aptX: an AAC connection followed by an aptX one", t_apt_worker_switch },
	{ "aptX: link information", t_apt_link_info },
	{ "aptX HD: the endpoint, the 17 configuration bytes, the RTP header and how a block is cut into packets", t_apt_hd_sep },
	{ "aptX HD: HD preferred when offered, the 17 bytes, the fallback turns HD off and not the device", t_apt_hd_negotiation },
	{ "aptX HD: the availability bits and when HD is wanted", t_apt_hd_avail },
	{ "aptX HD: the media packets of whole frames, the limit, the sequence and the timestamp", t_apt_hd_media },
	{ "aptX HD: the worker asks for an HD session; a refused one turns HD off", t_apt_hd_worker },
	{ "aptX HD: link information", t_apt_hd_link_info },
	{ "aptX: the module is found once; a table of another version is refused", t_apt_link_find },
	{ "LDAC: the endpoint choice, the 14 configuration bytes and every configuration refused", t_ld_sep },
	{ "LDAC: the negotiation when the Vita opens the stream, the SBC choice kept, the fallback", t_ld_negotiation },
	{ "LDAC: the headphones open the stream (endpoint 5), what is refused, the commands", t_ld_acceptor },
	{ "LDAC: the media packets (RTP, frame count, whole frames), one or two to a call, the flag on the last, the limit", t_ld_media },
	{ "LDAC: the sender's block goes to the add-on's ring", t_ld_pcm },
	{ "LDAC: the worker's session, the setting, the gates, standby, a refused session, module stop", t_ld_worker },
	{ "LDAC: a media channel limit under 679 ends the session and marks the device", t_ld_mtu_refused },
	{ "LDAC: a connection that is not LDAC ends the LDAC session", t_ld_worker_switch },
	{ "LDAC: link information", t_ld_link_info },
	{ "LDAC: the module is found once; a table of another version is refused", t_ld_link_find },
};

int main(void)
{
	int failed = 0;

	for (unsigned i = 0; i < sizeof(k_scn) / sizeof(k_scn[0]); i++) {
		pid_t pid;
		int st;

		fflush(stdout);
		pid = fork();
		if (pid == 0) {
			k_scn[i].fn();
			_exit(0);
		}
		waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st)) {
			printf("  FAIL  %s\n", k_scn[i].name);
			failed++;
		} else {
			printf("ok    %s\n", k_scn[i].name);
		}
	}
	printf("test_btaac_stream: %d scenarios, %d failed\n", (int)(sizeof(k_scn) / sizeof(k_scn[0])), failed);
	return failed ? 1 : 0;
}
