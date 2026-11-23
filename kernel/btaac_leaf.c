/*
 * btaac_leaf.c - the AAC engine's two rings, and nothing else: this object file is checked whole by tools/check_bteq_leaf.sh. The
 * audio sender's thread (later stages) fills the PCM ring and empties the access unit ring from inside the Bluetooth code's own
 * hooks, so what it calls here is a leaf: no system call, no lock, no wait, no allocation, no floating point or vector
 * instruction, and no library call (the copies are plain loops: the build forbids the compiler to turn them into one).
 *
 * Two single-producer single-consumer rings, nothing shared but the indices:
 *   PCM ring:  producer = the sender (btaac_push_pcm), consumer = the engine's thread (btaac_pcm_take).
 *   AU ring:   producer = the engine's thread (btaac_au_put),   consumer = the sender (btaac_pop_au, and btaac_au_trim, which drops the oldest units).
 * The indices run freely (never reduced), so a full ring and an empty one differ by subtraction and a wrap of the 32 bits is
 * harmless. A producer writes the data, then publishes the index with a release store; a consumer reads the other side's index with
 * an acquire load, takes the data, then releases its own index. The engine's thread alone changes the session (btaac_ring_open and
 * _close); it never moves an index that the sender owns: the PCM ring is emptied by its consumer side, and the units a session
 * leaves behind are told apart by the session number they carry, which the sender's pop skips.
 */
#include "btaac.h"

/* The rings are sized for the engine being held off the CPU: the engine can stall for 0.1 to 0.6 s while the system plays a sound, and the old
 * 85 ms input ring then refused blocks (audio lost). The input ring now takes 341 ms (16 blocks of 1024 frames; 64 KB), the output ring 16 units (341 ms of sound; 24.8 KB), so
 * a stall of 300 ms loses nothing and the backlog the engine then makes goes through the latency trim (kernel/btaac_lat.h), not through a refusal. Both are module data. */
#define PCM_FRAMES    BTAAC_PCM_RING_FRAMES   /* a power of two: 32 blocks of the sender's 512 frames */
#define PCM_MASK      (PCM_FRAMES - 1u)
#define AU_SLOTS      BTAAC_AU_RING_SLOTS     /* a power of two */
#define AU_MASK       (AU_SLOTS - 1u)

typedef struct AuSlot {
	uint32_t len, seq, epoch;
	uint8_t  data[BTAAC_AU_MAX];
} AuSlot;

static int16_t   g_pcm[PCM_FRAMES * 2u];     /* interleaved, `g_channels` samples a frame */
static AuSlot    g_au[AU_SLOTS];

/* The indices, each written by one side only. */
static volatile uint32_t g_pcm_wr, g_pcm_rd, g_au_wr, g_au_rd;
/* The session, written by the engine's thread before it sets g_open. */
static volatile uint32_t g_open;             /* 1 while a session runs: the sender's pushes are accepted */
static volatile uint32_t g_channels;
static volatile uint32_t g_epoch;            /* the session's number: units of another session are dead */
static volatile uint32_t g_next_seq;         /* the next unit's number (engine thread only) */

static volatile uint32_t g_overruns, g_frames_in, g_popped, g_drops, g_out;

uint32_t btaac_push_pcm(const int16_t *pcm, uint32_t frames)
{
	uint32_t wr, rd, ch, f, c;

	if (!pcm || !frames || frames > PCM_FRAMES || !__atomic_load_n(&g_open, __ATOMIC_ACQUIRE))
		return 0;
	ch = g_channels;
	wr = g_pcm_wr;
	rd = __atomic_load_n(&g_pcm_rd, __ATOMIC_ACQUIRE);
	if (PCM_FRAMES - (wr - rd) < frames) {
		g_overruns++;
		return 0;
	}
	for (f = 0; f < frames; f++)
		for (c = 0; c < ch; c++)
			g_pcm[((wr + f) & PCM_MASK) * ch + c] = pcm[f * ch + c];
	__atomic_store_n(&g_pcm_wr, wr + frames, __ATOMIC_RELEASE);
	g_frames_in += frames;
	return frames;
}

uint32_t btaac_pop_au(uint8_t *out, uint32_t cap, uint32_t *seq)
{
	uint32_t rd = g_au_rd, wr, i, len;
	const AuSlot *s;

	if (!out)
		return 0;
	wr = __atomic_load_n(&g_au_wr, __ATOMIC_ACQUIRE);
	for (;;) {
		if (rd == wr) {
			__atomic_store_n(&g_au_rd, rd, __ATOMIC_RELEASE);
			return 0;
		}
		s = &g_au[rd & AU_MASK];
		if (s->epoch == g_epoch)
			break;
		rd++;                              /* a unit of an earlier session: skipped */
	}
	len = s->len;
	if (len > cap) {
		g_drops++;
		__atomic_store_n(&g_au_rd, rd + 1u, __ATOMIC_RELEASE);
		return 0;
	}
	for (i = 0; i < len; i++)
		out[i] = s->data[i];
	if (seq)
		*seq = s->seq;
	__atomic_store_n(&g_au_rd, rd + 1u, __ATOMIC_RELEASE);
	g_popped++;
	return len;
}

uint32_t btaac_au_depth(void)
{
	uint32_t rd = g_au_rd, wr = __atomic_load_n(&g_au_wr, __ATOMIC_ACQUIRE), ep = g_epoch, n = 0;

	for (; rd != wr; rd++)                     /* the units of this session: those of an earlier one are skipped by the pop and are not waiting */
		if (g_au[rd & AU_MASK].epoch == ep)
			n++;
	return n;
}

uint32_t btaac_au_trim(uint32_t keep)
{
	uint32_t rd = g_au_rd, wr = __atomic_load_n(&g_au_wr, __ATOMIC_ACQUIRE), n = 0;

	while (wr - rd > keep) {                   /* the oldest first: units of an earlier session count as waiting too, and go first */
		rd++;
		n++;
	}
	if (n)
		__atomic_store_n(&g_au_rd, rd, __ATOMIC_RELEASE);
	return n;
}

/* ---- the engine's thread ---- */

void btaac_ring_open(uint32_t channels)
{
	__atomic_store_n(&g_open, 0u, __ATOMIC_RELEASE);
	g_channels = channels;
	g_pcm_rd = g_pcm_wr;                     /* the consumer's side: whatever is left of an earlier session is gone */
	g_next_seq = 0;
	g_overruns = g_frames_in = g_popped = g_drops = g_out = 0;
	g_epoch++;                               /* every unit already in the AU ring is now of an earlier session */
	__atomic_store_n(&g_open, 1u, __ATOMIC_RELEASE);
}

void btaac_ring_close(void)
{
	__atomic_store_n(&g_open, 0u, __ATOMIC_RELEASE);
}

uint32_t btaac_pcm_avail(void)
{
	return __atomic_load_n(&g_pcm_wr, __ATOMIC_ACQUIRE) - g_pcm_rd;
}

void btaac_pcm_take(int16_t *dst, uint32_t frames)
{
	uint32_t rd = g_pcm_rd, ch = g_channels, f, c;

	for (f = 0; f < frames; f++)
		for (c = 0; c < ch; c++)
			dst[f * ch + c] = g_pcm[((rd + f) & PCM_MASK) * ch + c];
	__atomic_store_n(&g_pcm_rd, rd + frames, __ATOMIC_RELEASE);
}

int btaac_au_room(void)
{
	return AU_SLOTS - (g_au_wr - __atomic_load_n(&g_au_rd, __ATOMIC_ACQUIRE)) > 0u;
}

int btaac_au_put(const uint8_t *au, uint32_t len)
{
	uint32_t wr = g_au_wr, i;
	AuSlot *s;

	if (len > BTAAC_AU_MAX || AU_SLOTS - (wr - __atomic_load_n(&g_au_rd, __ATOMIC_ACQUIRE)) == 0u)
		return 0;
	s = &g_au[wr & AU_MASK];
	for (i = 0; i < len; i++)
		s->data[i] = au[i];
	s->len = len;
	s->seq = g_next_seq++;
	s->epoch = g_epoch;
	__atomic_store_n(&g_au_wr, wr + 1u, __ATOMIC_RELEASE);
	g_out++;
	return 1;
}

void btaac_ring_info(uint32_t *overruns, uint32_t *frames_in, uint32_t *popped, uint32_t *drops, uint32_t *out)
{
	*overruns = g_overruns;
	*frames_in = g_frames_in;
	*popped = g_popped;
	*drops = g_drops;
	*out = g_out;
}
