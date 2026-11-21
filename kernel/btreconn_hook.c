/*
 * btreconn_hook.c - the hook body of "Reconnect After Standby" and its standby handler, and nothing else: this
 * object file is checked whole by tools/check_bteq_leaf.sh. The hook runs in SceAVConfig's policy thread, a
 * few times per connection of headphones, so it is a leaf like the others: no logging, no locks, no waits,
 * no allocation, no floating point, and no call but the original routine's. It only notes which headphones
 * the console started sound on. The handler runs in the power code's context around standby: it only adds
 * to a counter and returns 0.
 */
#include "btreconn_hook.h"

#define REF_SPINS   200000u      /* loads, no system call: about a millisecond at most */
#define FLAG_A2DP   0x8u         /* the call starts A2DP (sound sent to headphones)    */
#define NOT_CONNECTED   0x802F0D01   /* the error SceBt itself returns when the device is not connected */

volatile tai_hook_ref_t btreconn_ref;
volatile uint32_t btreconn_in, btreconn_rec, btreconn_armed;
volatile uint32_t btreconn_mac0, btreconn_mac1, btreconn_recorded, btreconn_wakes;

int btreconn_hook(uint32_t mac0, uint32_t mac1, uint32_t flags, uint32_t d)
{
	tai_hook_ref_t ref;
	uint32_t spins = REF_SPINS;
	int ret;

	/* The hook goes live a moment before taiHEN stores the reference. */
	while ((ref = btreconn_ref) == 0 && --spins)
		;
	if (!ref)
		return (int)NOT_CONNECTED;   /* never set: no call through it, in the first millisecond after install only */

	btreconn_in = 1;
	ret = TAI_CONTINUE(int, ref, mac0, mac1, flags, d);
	if (btreconn_rec && ret >= 0 && (flags & FLAG_A2DP)) {
		btreconn_mac0 = mac0;
		btreconn_mac1 = mac1;
		btreconn_recorded++;
	}
	btreconn_in = 0;
	return ret;
}

int btreconn_sysevent(int resume, int eventid, void *args, void *opt)
{
	if (resume && btreconn_armed)
		btreconn_wakes++;
	return 0;
}
