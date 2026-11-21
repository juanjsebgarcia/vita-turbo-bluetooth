/*
 * bthfp_sdp.h - the service table with a hands-free gateway record. Included by bthfp_mod.c only, so it is part of that
 * file's translation unit; the host test compiles it the same way.
 *
 * The console's service records are one table of 0x2B8 bytes at SDP_STOCK_OFF in the Bluetooth code, which cannot grow in
 * place. This module keeps its own copy: the console's records, then a hands-free gateway record, and points the 14
 * instruction words that name the table's address and end at the copy. Nothing else is changed; the headset gateway record
 * stays where it is.
 *
 * The added record (HFP 1.7, table 5.3): handle 0x00010011; service classes 0x111F (hands-free gateway) and 0x1203 (generic
 * audio); L2CAP and RFCOMM channel 4 (the headset record's channel: the AT answers of bthfp.c tell the two profiles apart
 * by what the headset sends first); profile 0x111E version 1.7; the service name; Network 0x0301 = 0; SupportedFeatures
 * 0x0311 = 0, matching +BRSF 1024 in the answers. The service name makes the record 0x54 bytes, so the table (0x30C) fits a
 * Thumb 'add' immediate and the records stay on 4-byte multiples.
 *
 * Gate: the table moves only while all 14 words and the table are as expected for this build; the copy is checked as a
 * table (every record's length, the handles, the end) before any word is written; each word is written with one data
 * inject and read back, and a write that does not take is undone. It is done holding the Bluetooth code's lock
 * (bthfp_conn.h). If another plugin has changed any word or the table, nothing is moved.
 */

#define SDP_STOCK_OFF  0x1F868u               /* the table: text offset of its first record's length byte   */
#define SDP_STOCK_LEN  0x2B8u                 /* ... and its size                                           */
#define SDP_END_OFF    (SDP_STOCK_OFF + SDP_STOCK_LEN)   /* the stock end, 0x1FB20: the CRC table starts there */
#define SDP_REC_LEN    0x54u                  /* the hands-free gateway record                              */
#define SDP_TAB_LEN    (SDP_STOCK_LEN + SDP_REC_LEN)     /* 0x30C = 0xC3 << 2: a Thumb immediate holds it  */
#define SDP_HFP_HANDLE 0x00010011u

/* The console's table is checked, not shipped: the 0x2B8 bytes must have this FNV-1a 64-bit hash (bytes_fnv in bthfp_mod.c; every firmware build has the same table),
 * and are then copied from the console's own code into g_sdp_tab. */
#define SDP_STOCK_FNV  0x86CA732FC05BFA38ull

/* The hands-free gateway record (every attribute is [entry length][id, 2 bytes, big endian][data element], ascending ids). */
static const uint8_t k_sdp_hfp[SDP_REC_LEN] = {
	0x54,                                                                           /* the record: its own length byte and all below   */
	0x08, 0x00, 0x00, 0x0A, 0x00, 0x01, 0x00, 0x11,                                 /* 0x0000 ServiceRecordHandle 0x00010011           */
	0x0B, 0x00, 0x01, 0x35, 0x06, 0x19, 0x11, 0x1F, 0x19, 0x12, 0x03,              /* 0x0001 ServiceClassIDList 0x111F, 0x1203        */
	0x11, 0x00, 0x04, 0x35, 0x0C, 0x35, 0x03, 0x19, 0x01, 0x00,                    /* 0x0004 ProtocolDescriptorList: L2CAP,           */
	      0x35, 0x05, 0x19, 0x00, 0x03, 0x08, 0x04,                                 /*            RFCOMM, server channel 4             */
	0x0D, 0x00, 0x09, 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x1E, 0x09, 0x01, 0x07,  /* 0x0009 BluetoothProfileDescriptorList 0x111E 1.7 */
	0x17, 0x01, 0x00, 0x25, 0x12,                                                   /* 0x0100 ServiceName, 18 bytes of text:           */
	      'H', 'a', 'n', 'd', 's', '-', 'F', 'r', 'e', 'e', ' ', 'G', 'a', 't', 'e', 'w', 'a', 'y',
	0x05, 0x03, 0x01, 0x08, 0x00,                                                   /* 0x0301 Network = 0 (cannot reject a call)       */
	0x06, 0x03, 0x11, 0x09, 0x00, 0x00,                                             /* 0x0311 SupportedFeatures = 0                    */
};

_Static_assert(sizeof(k_sdp_hfp) == SDP_REC_LEN, "the record window");
_Static_assert(SDP_REC_LEN % 4 == 0 && SDP_TAB_LEN % 4 == 0, "records and table stay on 4-byte multiples");

static uint8_t g_sdp_tab[SDP_TAB_LEN] __attribute__((aligned(8)));   /* the copy the walkers read */

/* ---- the 14 instruction words ------------------------------------------------------------------ */

enum { SK_MOVW, SK_MOVT, SK_ADD };

/* One word: where (text offset), what it is, its register(s), and for a movw/movt half whether it names the table's start or
 * its end. An 'add' is `add.w rd, rn, #length`: it makes the end from the start, so only its immediate changes. */
typedef struct { uint32_t off; uint8_t kind, rd, rn, end; } HfpSite;

#define HFP_NSITES 14
static const HfpSite k_sites[HFP_NSITES] = {
	/* the service search: start and end */
	{ 0x19870u, SK_MOVW, 7, 0, 0 }, { 0x19874u, SK_MOVT, 7, 0, 0 }, { 0x19914u, SK_MOVW, 2, 0, 1 }, { 0x19918u, SK_MOVT, 2, 0, 1 },
	/* the request handler: the record by handle, then the attribute search */
	{ 0x19E60u, SK_MOVW, 4, 0, 0 }, { 0x19E66u, SK_MOVT, 4, 0, 0 }, { 0x19E6Cu, SK_ADD, 9, 4, 0 },
	{ 0x19FD8u, SK_MOVW, 6, 0, 1 }, { 0x19FDCu, SK_MOVT, 6, 0, 1 }, { 0x19FEEu, SK_MOVW, 5, 0, 0 }, { 0x19FF6u, SK_MOVT, 5, 0, 0 },
	/* the list of service classes for the extended inquiry response, made at Bluetooth power-up */
	{ 0x1AC28u, SK_MOVW, 6, 0, 0 }, { 0x1AC2Cu, SK_MOVT, 6, 0, 0 }, { 0x1AC34u, SK_ADD, 7, 6, 0 },
};

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* The 4 bytes of a Thumb-2 `movw`/`movt rd, #imm16` (T3): hw1 = 11110 i 10 0100/1100 imm4, hw2 = 0 imm3 rd imm8. */
static void enc_movwt(uint8_t *p, int movt, unsigned rd, uint32_t imm)
{
	le16(p, (movt ? 0xF2C0u : 0xF240u) | (((imm >> 11) & 1u) << 10) | (imm >> 12));
	le16(p + 2, (((imm >> 8) & 7u) << 12) | (rd << 8) | (imm & 0xFFu));
}

/* The 12-bit field of a Thumb-2 "modified immediate" holding `v` (an 8-bit value with its top bit set, rotated right by 8 to 31
 * bits), or -1 if there is none. Only the rotated form is needed: the lengths here are 0x200 and up. */
static int thumb_modimm(uint32_t v)
{
	for (unsigned rot = 8; rot < 32; rot++) {
		uint32_t c = (v << rot) | (v >> (32 - rot));     /* v rotated left by rot: back to the unrotated 8 bits */

		if (c >= 0x80 && c <= 0xFF)
			return (int)((rot << 7) | (c & 0x7F));
	}
	return -1;
}

/* The 4 bytes of `add.w rd, rn, #imm12` (T3): hw1 = 11110 i 0 1000 0 rn, hw2 = 0 imm3 rd imm8. */
static void enc_add(uint8_t *p, unsigned rd, unsigned rn, uint32_t imm12)
{
	le16(p, 0xF100u | (((imm12 >> 11) & 1u) << 10) | rn);
	le16(p + 2, (((imm12 >> 8) & 7u) << 12) | (rd << 8) | (imm12 & 0xFFu));
}

/* The bytes site `s` has when the table starts at `start` and ends at `end` and is `len` long. */
static void site_bytes(const HfpSite *s, uint32_t start, uint32_t end, uint32_t len, uint8_t out[4])
{
	uint32_t a = s->end ? end : start;

	if (s->kind == SK_ADD)
		enc_add(out, s->rd, s->rn, (uint32_t)thumb_modimm(len));
	else
		enc_movwt(out, s->kind == SK_MOVT, s->rd, s->kind == SK_MOVT ? a >> 16 : a & 0xFFFFu);
}

/* The table as seen by this module: where it starts (an address) and what its end and size are. */
static uint32_t sdp_stock_start(void) { return (uint32_t)(uintptr_t)g_at_base + SDP_STOCK_OFF; }
static uint32_t sdp_copy_start(void)  { return (uint32_t)(uintptr_t)g_sdp_tab; }

/* How many of the 14 words are exactly what they would be for a table at `start` of `len` bytes; -1 if the text is not readable. */
static int sdp_sites_match(uint32_t start, uint32_t len)
{
	int n = 0;

	if (!g_at_base)
		return -1;
	for (int i = 0; i < HFP_NSITES; i++) {
		uint8_t want[4];

		site_bytes(&k_sites[i], start, start + len, len, want);
		if (bytes_same((const volatile uint8_t *)g_at_base + k_sites[i].off, want, 0, 4))
			n++;
	}
	return n;
}

/* What the 14 words say now: 0 not readable, 1 all stock, 2 all ours, 3 anything else. *ours = how many point at the copy. */
static int sdp_tab_bytes(int *ours)
{
	int s = sdp_sites_match(sdp_stock_start(), SDP_STOCK_LEN), o = sdp_sites_match(sdp_copy_start(), SDP_TAB_LEN);

	if (ours)
		*ours = o < 0 ? 0 : o;
	if (s < 0)
		return 0;
	return s == HFP_NSITES ? 1 : o == HFP_NSITES ? 2 : 3;
}

/* 1 if the 0x2B8 bytes at the stock table are the console's own table (by their hash). */
static int sdp_stock_bytes_ok(void)
{
	return g_at_base && bytes_fnv((const volatile uint8_t *)g_at_base + SDP_STOCK_OFF, SDP_STOCK_LEN, 0, 0) == SDP_STOCK_FNV;
}

/* A table: records back to back, each at least 9 bytes (its length byte and the handle attribute) and inside the table, ending exactly at `len`, with distinct handles and
 * the handle attribute first (every walker finds a record by it). Returns 0 if it is, else a code for the log. */
static int sdp_tab_problem(const uint8_t *t, uint32_t len)
{
	uint32_t i = 0, n = 0, handles[16];

	while (i < len) {
		uint32_t r = t[i], h;

		if (r < 9 || i + r > len)
			return 1;
		if (t[i + 1] != 0x08 || t[i + 2] != 0 || t[i + 3] != 0 || t[i + 4] != 0x0A)
			return 2;                                      /* attribute 0x0000, a 32-bit unsigned: the handle */
		h = (uint32_t)t[i + 5] << 24 | (uint32_t)t[i + 6] << 16 | (uint32_t)t[i + 7] << 8 | t[i + 8];
		for (uint32_t k = 0; k < n; k++)
			if (handles[k] == h)
				return 3;
		if (n >= 16)
			return 4;
		handles[n++] = h;
		i += r;
	}
	return i == len ? 0 : 5;
}
