/*
 * btevlog_hook.c - the link log's hook-side routine, and nothing else: this object file is checked whole by tools/check_bteq_leaf.sh. It runs inside the Bluetooth
 * code's event handler (through the hook in btflush_hook.c), so it is a leaf: no logging, no locks, no waits, no allocation, no floating point, and no call but the
 * microsecond clock. It stores a few words in a ring; the worker (btevlog.c) does all the rest.
 *
 * Events (HCI, in the standard layout; the event code, the parameter length, the parameters):
 *   0x03 Connection Complete        status, handle (2), address (6), link type, encryption
 *   0x04 Connection Request         address (6), class (3), link type
 *   0x05 Disconnection Complete     status, handle (2), reason
 *   0x06 Authentication Complete    status, handle (2)
 *   0x08 Encryption Change          status, handle (2), enabled
 *   0x12 Role Change                status, address (6), role
 * Only the ones with a whole, correct length are noted. An address is kept as the Bluetooth code keeps it: the first four bytes in a word, the last two in another.
 */
#include "btevlog_hook.h"

extern uint32_t ksceKernelGetSystemTimeLow(void);

BtelRing btevlog_ring;
volatile uint32_t btevlog_on;

static inline uint32_t el_le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static inline uint32_t el_le32(const uint8_t *p) { return el_le16(p) | el_le16(p + 2) << 16; }

void btevlog_note_hci(const uint8_t *ev, int len)
{
	uint32_t plen;
	const uint8_t *p;

	if (!btevlog_on || !ev || len < 3)
		return;
	plen = ev[1];
	if ((uint32_t)len != 2u + plen)
		return;
	p = ev + 2;
	switch (ev[0]) {
	case 0x03:    /* Connection Complete: an ACL link only (type 1); the voice links are not logged */
		if (plen == 11 && p[9] == 1)
			btel_put(&btevlog_ring, ksceKernelGetSystemTimeLow(), BTEL_K_CONN_CMP, el_le32(p + 3), el_le16(p + 7), (uint32_t)p[0] | (el_le16(p + 1) & 0xFFFu) << 8 | 1u << 24);
		break;
	case 0x04:
		if (plen == 10)
			btel_put(&btevlog_ring, ksceKernelGetSystemTimeLow(), BTEL_K_CONN_REQ, el_le32(p), el_le16(p + 4), p[9]);
		break;
	case 0x05:
		if (plen == 4)
			btel_put(&btevlog_ring, ksceKernelGetSystemTimeLow(), BTEL_K_DISC, (el_le16(p + 1) & 0xFFFu) | (uint32_t)p[0] << 16, p[3], 0);
		break;
	case 0x06:
		if (plen == 3)
			btel_put(&btevlog_ring, ksceKernelGetSystemTimeLow(), BTEL_K_AUTH, (el_le16(p + 1) & 0xFFFu) | (uint32_t)p[0] << 16, 0, 0);
		break;
	case 0x08:
		if (plen == 4)
			btel_put(&btevlog_ring, ksceKernelGetSystemTimeLow(), BTEL_K_ENC, (el_le16(p + 1) & 0xFFFu) | (uint32_t)p[0] << 16, p[3], 0);
		break;
	case 0x12:
		if (plen == 8)
			btel_put(&btevlog_ring, ksceKernelGetSystemTimeLow(), BTEL_K_ROLE, el_le32(p + 1), el_le16(p + 5), (uint32_t)p[0] | (uint32_t)p[7] << 8);
		break;
	}
}
