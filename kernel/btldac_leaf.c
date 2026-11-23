/*
 * btldac_leaf.c - the LDAC engine's two rings, and nothing else: this object file is checked whole by tools/check_bteq_leaf.sh. The audio sender's thread fills the PCM
 * ring and empties the packet ring from inside the Bluetooth code's own hooks, so what it calls here is a leaf: no system call, no lock, no wait, no allocation, no floating
 * point or vector instruction, and no library call (the copies are plain loops: the build forbids the compiler to turn them into one). The design is btaac_leaf.c's:
 *
 *   PCM ring:     producer = the sender (btldac_push_pcm), consumer = the engine's thread (btldac_pcm_take).
 *   packet ring:  producer = the engine's thread (btldac_pkt_put), consumer = the sender (btldac_pop_pkt, and btldac_pkt_trim, which drops the oldest packets).
 * The indices run freely (never reduced); a producer writes the data then publishes its index with a release store, a consumer reads the other side's index with an acquire
 * load. The engine's thread alone changes the session (btldac_ring_open, _close); the packets a session leaves behind are told apart by the session number they carry.
 */
#include "btldac.h"


#define PCM_FRAMES    BTLDAC_PCM_RING_FRAMES   /* a power of two */
#define PCM_MASK      (PCM_FRAMES - 1u)
#define PKT_SLOTS     BTLDAC_PKT_RING_SLOTS    /* a power of two */
#define PKT_MASK      (PKT_SLOTS - 1u)

typedef struct PktSlot {
	uint32_t len, nframes, seq, epoch;
	uint8_t  data[(BTLDAC_FRAMES_BYTES + 3u) & ~3u];
} PktSlot;

static int16_t  g_pcm[PCM_FRAMES * 2u];      /* interleaved stereo */
static PktSlot  g_pkt[PKT_SLOTS];

static volatile uint32_t g_pcm_wr, g_pcm_rd, g_pkt_wr, g_pkt_rd;
static volatile uint32_t g_open;             /* 1 while a session runs: the sender's pushes are accepted */
static volatile uint32_t g_epoch;            /* the session's number: packets of another session are dead */
static volatile uint32_t g_next_seq;         /* the next packet's number (engine thread only) */

static volatile uint32_t g_overruns, g_frames_in, g_popped, g_drops, g_out;

uint32_t btldac_push_pcm(const int16_t *pcm, uint32_t frames)
{
	uint32_t wr, rd, f;

	if (!pcm || !frames || frames > PCM_FRAMES || !__atomic_load_n(&g_open, __ATOMIC_ACQUIRE))
		return 0;
	wr = g_pcm_wr;
	rd = __atomic_load_n(&g_pcm_rd, __ATOMIC_ACQUIRE);
	if (PCM_FRAMES - (wr - rd) < frames) {
		g_overruns++;
		return 0;
	}
	for (f = 0; f < frames; f++) {
		g_pcm[((wr + f) & PCM_MASK) * 2u] = pcm[f * 2u];
		g_pcm[((wr + f) & PCM_MASK) * 2u + 1u] = pcm[f * 2u + 1u];
	}
	__atomic_store_n(&g_pcm_wr, wr + frames, __ATOMIC_RELEASE);
	g_frames_in += frames;
	return frames;
}

uint32_t btldac_pop_pkt(uint8_t *out, uint32_t cap, uint32_t *nframes, uint32_t *seq)
{
	uint32_t rd = g_pkt_rd, wr, i, len;
	const PktSlot *s;

	if (!out)
		return 0;
	wr = __atomic_load_n(&g_pkt_wr, __ATOMIC_ACQUIRE);
	for (;;) {
		if (rd == wr) {
			__atomic_store_n(&g_pkt_rd, rd, __ATOMIC_RELEASE);
			return 0;
		}
		s = &g_pkt[rd & PKT_MASK];
		if (s->epoch == g_epoch)
			break;
		rd++;                              /* a packet of an earlier session: skipped */
	}
	len = s->len;
	if (len > cap) {
		g_drops++;
		__atomic_store_n(&g_pkt_rd, rd + 1u, __ATOMIC_RELEASE);
		return 0;
	}
	for (i = 0; i < len; i++)
		out[i] = s->data[i];
	if (nframes)
		*nframes = s->nframes;
	if (seq)
		*seq = s->seq;
	__atomic_store_n(&g_pkt_rd, rd + 1u, __ATOMIC_RELEASE);
	g_popped++;
	return len;
}

uint32_t btldac_pkt_depth(void)
{
	uint32_t rd = g_pkt_rd, wr = __atomic_load_n(&g_pkt_wr, __ATOMIC_ACQUIRE), ep = g_epoch, n = 0;

	for (; rd != wr; rd++)
		if (g_pkt[rd & PKT_MASK].epoch == ep)
			n++;
	return n;
}

uint32_t btldac_pkt_trim(uint32_t keep)
{
	uint32_t rd = g_pkt_rd, wr = __atomic_load_n(&g_pkt_wr, __ATOMIC_ACQUIRE), n = 0;

	while (wr - rd > keep) {
		rd++;
		n++;
	}
	if (n)
		__atomic_store_n(&g_pkt_rd, rd, __ATOMIC_RELEASE);
	return n;
}

/* ---- the engine's thread ---- */

void btldac_ring_open(void)
{
	__atomic_store_n(&g_open, 0u, __ATOMIC_RELEASE);
	g_pcm_rd = g_pcm_wr;                     /* whatever is left of an earlier session is gone */
	g_next_seq = 0;
	g_overruns = g_frames_in = g_popped = g_drops = g_out = 0;
	g_epoch++;                               /* every packet already in the ring is now of an earlier session */
	__atomic_store_n(&g_open, 1u, __ATOMIC_RELEASE);
}

void btldac_ring_close(void)
{
	__atomic_store_n(&g_open, 0u, __ATOMIC_RELEASE);
}

uint32_t btldac_pcm_avail(void)
{
	return __atomic_load_n(&g_pcm_wr, __ATOMIC_ACQUIRE) - g_pcm_rd;
}

void btldac_pcm_take(int16_t *dst, uint32_t frames)
{
	uint32_t rd = g_pcm_rd, f;

	for (f = 0; f < frames; f++) {
		dst[f * 2u] = g_pcm[((rd + f) & PCM_MASK) * 2u];
		dst[f * 2u + 1u] = g_pcm[((rd + f) & PCM_MASK) * 2u + 1u];
	}
	__atomic_store_n(&g_pcm_rd, rd + frames, __ATOMIC_RELEASE);
}

int btldac_pkt_room(void)
{
	return PKT_SLOTS - (g_pkt_wr - __atomic_load_n(&g_pkt_rd, __ATOMIC_ACQUIRE)) > 0u;
}

int btldac_pkt_put(const uint8_t *frames, uint32_t len, uint32_t nframes)
{
	uint32_t wr = g_pkt_wr, i;
	PktSlot *s;

	if (len > BTLDAC_FRAMES_BYTES || PKT_SLOTS - (wr - __atomic_load_n(&g_pkt_rd, __ATOMIC_ACQUIRE)) == 0u)
		return 0;
	s = &g_pkt[wr & PKT_MASK];
	for (i = 0; i < len; i++)
		s->data[i] = frames[i];
	s->len = len;
	s->nframes = nframes;
	s->seq = g_next_seq++;
	s->epoch = g_epoch;
	__atomic_store_n(&g_pkt_wr, wr + 1u, __ATOMIC_RELEASE);
	g_out++;
	return 1;
}

void btldac_ring_info(uint32_t *overruns, uint32_t *frames_in, uint32_t *popped, uint32_t *drops, uint32_t *out)
{
	*overruns = g_overruns;
	*frames_in = g_frames_in;
	*popped = g_popped;
	*drops = g_drops;
	*out = g_out;
}
