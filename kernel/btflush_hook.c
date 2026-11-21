/*
 * btflush_hook.c - the hook bodies of the flush timeout (Bluetooth latency Lowest) and of the
 * completed-packets guard, and nothing else: this object file is checked whole by tools/check_bteq_leaf.sh.
 * They run in the Bluetooth code's own threads, so they are leaves like the sound hook: no logging, no
 * locks, no waits, no allocation, no floating point, and no call but the original routine and the
 * Bluetooth code's own command sender (an indirect call).
 *
 * Stream start and stop: each runs the original first and then does its one send (the flush timeout
 * command, through the sender the original already uses). See btflush.c for the timing.
 *
 * The guard: the controller reports sent packets in Number_Of_Completed_Packets events, as a list of
 * links followed by a list of counts. The Bluetooth code reads one handle and one count with the
 * format "122": right for an event about one link, wrong for an event about two (a headphone link and a
 * controller): it reads the second handle as the first link's count, and never sees the second link's
 * count at all, so a media packet's completion can be lost and the count of packets in flight stays
 * high. The guard hands such an event to the original as one well-formed one-link event per link. An event
 * about one link, or one that is not a well-formed list, goes to the original untouched.
 */
#include "btflush_hook.h"

#define REF_SPINS    200000u    /* loads, no system call                                  */
#define MASK_A2DP    0x8u       /* the mask bit of the A2DP source stream                 */
#define OP_FLUSH     0x0C28u    /* Write_Automatic_Flush_Timeout: handle, timeout         */
#define EVT_NOCP     0x13u      /* Number_Of_Completed_Packets                            */

typedef int (*sender_fn)(int ctx, unsigned op, const char *fmt, ...);

static const char k_fmt[] = "22";           /* two 16-bit values, in a literal of this module */

volatile tai_hook_ref_t btflush_start_ref, btflush_stop_ref, btflush_evt_ref;
volatile uint32_t btflush_in_start, btflush_in_stop, btflush_in_evt;
volatile uint32_t btflush_slots, btflush_armed, btflush_guard;
void *volatile btflush_sender;

/* One command to the link of connection `conn`: its handle is the 16-bit word at +0x30 and the
 * sender's context is the connection's first word, as in the original routines. */
static void send_flush(int *conn, uint32_t slots)
{
	sender_fn send = (sender_fn)btflush_sender;

	if (!send || !conn || !*conn)
		return;
	send(*conn, OP_FLUSH, k_fmt, (int)(uint16_t)conn[0xC], (int)slots);
}

void btflush_start_hook(int *conn, int flag, uint32_t mask)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;

	while ((ref = btflush_start_ref) == 0 && --spins)
		;
	if (!ref)
		return;
	btflush_in_start = 1;
	TAI_CONTINUE(void, ref, conn, flag, mask);
	if (btflush_slots && !flag && (mask & MASK_A2DP) && conn) {
		send_flush(conn, btflush_slots);
		btflush_armed = (uint32_t)(uint16_t)conn[0xC] + 1;
	}
	btflush_in_start = 0;
}

void btflush_stop_hook(int *conn, int flag, uint32_t mask)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;

	while ((ref = btflush_stop_ref) == 0 && --spins)
		;
	if (!ref)
		return;
	btflush_in_stop = 1;
	TAI_CONTINUE(void, ref, conn, flag, mask);
	if (btflush_armed && (mask & MASK_A2DP) && conn && (uint32_t)(uint16_t)conn[0xC] + 1 == btflush_armed) {
		send_flush(conn, 0);
		btflush_armed = 0;
	}
	btflush_in_stop = 0;
}

void btflush_evt_hook(uint32_t *ctx, const uint8_t *ev, int len)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;

	while ((ref = btflush_evt_ref) == 0 && --spins)
		;
	if (!ref)
		return;   /* never set: nothing to pass on through; only in the moment the hook goes live */
	btflush_in_evt = 1;
	/* event code, parameter length, then Num_Handles, the handles (2 bytes each) and the counts (2 each) */
	if (len >= 3 && ev[0] == EVT_NOCP) {
		uint32_t n = ev[2];
		if (n > 1 && (uint32_t)len == 2u + ev[1] && ev[1] == 1u + 4u * n) {
			if (btflush_guard) {
				for (uint32_t i = 0; i < n; i++) {
					uint8_t one[7];

					one[0] = EVT_NOCP;
					one[1] = 5;
					one[2] = 1;
					one[3] = ev[3 + 2 * i];
					one[4] = ev[4 + 2 * i];
					one[5] = ev[3 + 2 * n + 2 * i];
					one[6] = ev[4 + 2 * n + 2 * i];
					TAI_CONTINUE(void, ref, ctx, one, 7);
				}
				btflush_in_evt = 0;
				return;
			}
		}
	}
	TAI_CONTINUE(void, ref, ctx, ev, len);
	btflush_in_evt = 0;
}
