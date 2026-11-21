/*
 * bthfp_mic.h - the headset's microphone over the hands-free connection. Included by bthfp_mod.c only, after bthfp_conn.h, so it is
 * part of that file's translation unit; the host test compiles it the same way.
 *
 * The console takes a Bluetooth headset's microphone only from a device record whose profile word (+0x20) has the voice bit
 * 0x20000000, and the Bluetooth code sets that bit from the headset profile (0x10000) alone: a headset that offers only the
 * hands-free profile (0x1) never has it. These parts make the bit follow "headset profile or hands-free profile". They are on while
 * the hands-free mode is 3 or 4 and the answers are in, and are all undone when the mode goes down, the master switch goes off or the
 * module stops.
 *
 *   1. Two patches of the Bluetooth code's text, one at each place it recomputes the bit (after a device's service records are read,
 *      and when the device table is loaded). Each is 12 bytes, three instructions in place of the console's four: the console sets bit
 *      29 of the record word from bit 16; the patch sets it from bit 16 or bit 0. So it is the console's rule with one more input: a
 *      record with neither profile still has the bit cleared. The code after each patch writes its scratch register and the flags
 *      before it reads them again, and the 12 bytes hold no address the loader writes.
 *      Applied only if all 24 bytes are the console's, under the Bluetooth lock, read back, undone if they did not take; nothing is
 *      written if any of them is not.
 *   2. A sweep of the device records already loaded (the patches only act when a record is next recomputed): a record with the
 *      hands-free bit, a hands-free channel and the audio sink bit gets the voice bit. The records it changed are remembered, and the bit
 *      is taken off again from exactly those (while the record still names the same address and has no headset profile), and from any
 *      record that has it with the hands-free bit and no headset profile, which is what the patched recompute leaves: the console sets
 *      the bit for the headset profile alone, so the bit on such a record can only be ours.
 *   3. The connect when the audio policy asks for the microphone (bthfp_conn.h).
 * The console reads the bit when a headset's link comes up, so a headset that is connected when this goes in has the microphone from
 * its next connection.
 */

#define OFF_MIC_A     0x1A638u    /* the recompute after a device's service records are read */
#define OFF_MIC_B     0x14676u    /* ... and when the device table is loaded                  */
#define MIC_LEN       12u
#define MIC_NSITES    2
_Static_assert(MIC_LEN == 12, "the patch is 12 bytes");

static const uint8_t k_mic_stock[12] = { 0xDA, 0x03, 0x4C, 0xBF, 0x43, 0xF0, 0x00, 0x53, 0x23, 0xF0, 0x00, 0x53 };
static const uint8_t k_mic_new[12]   = { 0x43, 0xEA, 0x03, 0x42, 0xC2, 0xF3, 0x00, 0x42, 0x62, 0xF3, 0x5D, 0x73 };
static const uint32_t k_mic_off[MIC_NSITES] = { OFF_MIC_A, OFF_MIC_B };

#define MIC_NDEV      32          /* the device table's entries                                                              */
#define MIC_HSP       0x10000u    /* the headset profile bit of the device record                                           */

static SceUID g_mic_uid[MIC_NSITES];
static int g_mic_in, g_mic_failed;
static int g_mic_dirty;           /* the patches have been in, or the sweep changed a record: the records are to be looked at when it ends */
static struct { uint32_t lo, hi; uint8_t on; } g_mic_rec[MIC_NDEV];    /* the records the sweep changed, by their place in the table */
static int g_mic_nrec;            /* how many are `on` */
static uint32_t g_mic_swept;      /* records the sweep changed since the module started */

/* ---- the patches ----------------------------------------------------------------------------------- */

/* What the 24 bytes say: 0 not readable, 1 all the console's, 2 all ours, 3 anything else. */
static int mic_bytes(void)
{
	int stock = 0, ours = 0;

	if (!g_at_base)
		return 0;
	for (int i = 0; i < MIC_NSITES; i++) {
		const volatile uint8_t *p = (const volatile uint8_t *)g_at_base + k_mic_off[i];

		stock += bytes_same(p, k_mic_stock, 0, MIC_LEN);
		ours += bytes_same(p, k_mic_new, 0, MIC_LEN);
	}
	return stock == MIC_NSITES ? 1 : ours == MIC_NSITES ? 2 : 3;
}

static int hfp_mic_release_locked(void)
{
	int n = 0;

	for (int i = 0; i < MIC_NSITES; i++) {
		if (g_mic_uid[i] > 0) {
			taiInjectReleaseForKernel(g_mic_uid[i]);
			n++;
		}
		g_mic_uid[i] = 0;
	}
	g_mic_in = 0;
	return n;
}

/* The patches out, with the Bluetooth lock (or, at module stop (`stop`) without it when it cannot be had: code left patched in a module
 * that is gone is the worse of the two). Returns 1 when none is in (any more). */
__attribute__((noinline)) static int hfp_mic_patch_release(int stop)
{
	int locked, any = 0;

	for (int i = 0; i < MIC_NSITES; i++)
		any |= g_mic_uid[i] > 0;
	if (!any)
		return 1;
	locked = hfp_lock(!stop) == 0;
	if (!locked && !stop)
		return 0;                       /* the next pass tries again */
	hfp_mic_release_locked();
	if (locked)
		hfp_unlock(!stop);
	else
		ksceKernelDelayThread(50000);
	BTT_KLOG("bluetooth hfp: microphone patches out%s", mic_bytes() == 1 ? "" : " - NOT all the console's bytes");
	return 1;
}

static void hfp_mic_fail(const char *why, int state)
{
	g_mic_failed = 1;
	BTT_KLOG("bluetooth hfp: microphone patches refused: %s (%d)", why, state);
}

__attribute__((noinline)) static void hfp_mic_patch_apply(void)
{
	int b, bad = 0;

	if (g_mic_in || g_mic_failed || g_at_modid < 0 || !g_at_base)
		return;
	if (hfp_routines_check() != 1) {
		hfp_mic_fail("lock not recognised", g_rt_state);
		return;
	}
	b = mic_bytes();
	if (b != 1) {
		hfp_mic_fail(b == 3 ? "other bytes" : "unreadable", b);
		return;
	}
	if (hfp_lock(1) != 0) {
		hfp_mic_fail("no lock", 0);
		return;
	}
	if (mic_bytes() == 1) {
		for (int i = 0; i < MIC_NSITES; i++) {
			SceUID uid = taiInjectDataForKernel(KERNEL_PID, g_at_modid, 0, k_mic_off[i], k_mic_new, MIC_LEN);

			if (uid <= 0) {
				bad = i + 1;
				break;
			}
			g_mic_uid[i] = uid;
		}
		if (!bad && mic_bytes() != 2)
			bad = 1000 + mic_bytes();
		if (bad)
			hfp_mic_release_locked();
	} else {
		bad = 200;                      /* changed between the first look and the lock */
	}
	hfp_unlock(1);
	if (bad) {
		hfp_mic_fail(bad == 200 ? "changed under the lock" : bad >= 1000 ? "not taken, undone" : "write refused, undone", bad);
		return;
	}
	g_mic_in = 1;
	g_mic_dirty = 1;
	BTT_KLOG("bluetooth hfp: microphone patches in");
}

/* ---- the sweep --------------------------------------------------------------------------------------- */

static uintptr_t hfp_mic_dev(int i)
{
	uintptr_t seg1 = btt_bt_seg1();

	return seg1 && i >= 0 && i < MIC_NDEV ? seg1 + HC_DEV0 + (uintptr_t)i * HC_DEV_STRIDE : 0;
}

/* The sweep's rule: hands-free profile, a hands-free channel and the audio sink, and no voice bit yet. */
static int hfp_mic_rule(uint32_t bits, uint8_t hf)
{
	return (bits & 0x1u) && hf && (bits & HC_DEV_SINK) && !(bits & HC_VOICE_MARK);
}

/* A record that names an address (an entry no one has written names none). */
static int hfp_mic_named(uintptr_t d)
{
	return hc_rd32(d + HC_D_MAC_LO) || (hc_rd32(d + HC_D_MAC_HI) & 0xFFFF);
}

/* The records the rule picks. With `apply` (the lock held) each gets the voice bit and is remembered. Returns how many. */
static int hfp_mic_pick(int apply)
{
	int n = 0;

	for (int i = 0; i < MIC_NDEV; i++) {
		uintptr_t d = hfp_mic_dev(i);
		uint32_t bits;

		if (!d || !hfp_mic_named(d))
			continue;
		bits = hc_rd32(d + HC_D_BITS);
		if (!hfp_mic_rule(bits, hc_rd8(d + HC_D_HF)))
			continue;
		n++;
		if (!apply)
			continue;
		*(volatile uint32_t *)(d + HC_D_BITS) = bits | HC_VOICE_MARK;
		g_mic_nrec += !g_mic_rec[i].on;     /* the console may have taken the bit off and this put it back: one entry */
		g_mic_rec[i].on = 1;
		g_mic_rec[i].lo = hc_rd32(d + HC_D_MAC_LO);
		g_mic_rec[i].hi = hc_rd32(d + HC_D_MAC_HI) & 0xFFFF;
	}
	return n;
}

/* One pass of the sweep: the records the rule picks get the bit, with the lock, each read again under it. Nothing is taken when there is
 * nothing to change. */
__attribute__((noinline)) static void hfp_mic_sweep(void)
{
	int n;

	if (!hfp_mic_pick(0) || hfp_lock(1) != 0)
		return;                         /* nothing to do, or the next pass */
	n = hfp_mic_pick(1);
	hfp_unlock(1);
	if (n) {
		g_mic_dirty = 1;
		g_mic_swept += (uint32_t)n;
		BTT_KLOG("bluetooth hfp: microphone: %d record(s) marked, %d held", n, g_mic_nrec);
	}
}

/* The bit off a record that has no headset profile (the console sets it for that profile alone). */
static int hfp_mic_unmark(uintptr_t d, int need_hf)
{
	uint32_t bits = hc_rd32(d + HC_D_BITS);

	if (!(bits & HC_VOICE_MARK) || (bits & MIC_HSP) || (need_hf && !(bits & 0x1u)))
		return 0;
	*(volatile uint32_t *)(d + HC_D_BITS) = bits & ~HC_VOICE_MARK;
	return 1;
}

/* The bit off again: from the records the sweep changed (while they still name the same address) and, with `all`, from every record that
 * has it with the hands-free bit and no headset profile, which is what the patched recompute leaves. With the lock, or at module stop
 * without it when it cannot be had. Returns 1 when done (0: no lock, the next pass tries again). */
__attribute__((noinline)) static int hfp_mic_records_restore(int stop, int all)
{
	int locked, listed = 0, others = 0;

	if (!g_mic_nrec && !(all && g_mic_dirty))
		return 1;
	locked = hfp_lock(!stop) == 0;
	if (!locked && !stop)
		return 0;
	for (int i = 0; i < MIC_NDEV; i++) {
		uintptr_t d = hfp_mic_dev(i);

		if (!d)
			continue;
		if (g_mic_rec[i].on && hc_rd32(d + HC_D_MAC_LO) == g_mic_rec[i].lo && (hc_rd32(d + HC_D_MAC_HI) & 0xFFFF) == g_mic_rec[i].hi)
			listed += hfp_mic_unmark(d, 0);
		else if (all)
			others += hfp_mic_unmark(d, 1);
	}
	if (all)
		g_mic_dirty = 0;
	memset(g_mic_rec, 0, sizeof(g_mic_rec));
	g_mic_nrec = 0;
	if (locked)
		hfp_unlock(!stop);
	if (listed || others)
		BTT_KLOG("bluetooth hfp: microphone: %d+%d record(s) unmarked", listed, others);
	return 1;
}

/* Everything of the microphone parts out (the patches, then the records). 1 when nothing is left in. */
__attribute__((noinline)) static int hfp_mic_release(int stop)
{
	int a = hfp_mic_patch_release(stop), b = hfp_mic_records_restore(stop, 1);

	return a && b;
}

/* Once per pass in modes 3 and 4 with the hook answering (`on`), else the parts are taken out. */
static void hfp_mic_sync(int on)
{
	int patches = on, sweep = on;
	if (!on) {
		hfp_mic_release(0);
		return;
	}
	if (patches)
		hfp_mic_patch_apply();
	else
		hfp_mic_patch_release(0);
	if (sweep)
		hfp_mic_sweep();
	else
		hfp_mic_records_restore(0, !patches);
}
