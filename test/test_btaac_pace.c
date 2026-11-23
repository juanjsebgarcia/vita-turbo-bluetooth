/*
 * test_btaac_pace.c - host test of what keeps AAC from stuttering when the system is busy: the pacing of the media hook (kernel/btaac_pace.h: pre-roll, spacing, the refill after a
 * dry ring, the underrun and cadence accounting, the trim never below the pre-roll), the engine thread's priority choice (kernel/btaac_core.h: computed from the audio sender's, set
 * by hand, the fallback, and the reading of the thread list), and the rings (kernel/btaac_leaf.c: the input ring takes a 300 ms stall, the output ring's depth counts only this
 * session's units), compiled as they are.
 *
 *   make test      (cc -Itest/host test/test_btaac_pace.c, then run)
 *
 * The sender's side is simulated one call a block (10.67 ms of sound): the call trims, reads the depth, asks the pacing and pops, as the media hook does (kernel/btaac_hook.c send_aac;
 * test/test_btaac_stream.c runs the real hook through the same cases). The engine's side puts a unit in the ring every second block, or nothing during a stall and the whole backlog at
 * once after it.
 */
#include <btt_host.h>
#include <stdlib.h>

#include "../kernel/btaac_leaf.c"
#include "../kernel/btaac_pace.h"

#define CHECK(c, ...) do { if (!(c)) { \
	printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); \
	exit(1); } } while (0)

/* ---- the simulation -------------------------------------------------------------------------------------------------------------------- */

typedef struct Sim {
	BtAacPace p;
	uint32_t preroll, keep;
	uint32_t unit_no;          /* the next unit the engine makes */
	uint32_t call;             /* blocks so far */
	uint32_t last_send;        /* the call that last sent */
	uint32_t sends, closest;   /* units sent; the fewest calls between two sends (0 = none yet) */
	uint32_t trimmed, max_depth_after_trim;
	uint32_t sent_calls[512];
} Sim;

static void sim_init(Sim *s, uint32_t preroll, uint32_t keep)
{
	memset(s, 0, sizeof(*s));
	btaac_pace_reset(&s->p);
	s->preroll = preroll;
	s->keep = keep;
	s->closest = 0xFFFFu;
	btaac_ring_open(2);
	{   /* the dead units of the last scenario's session are taken out by the first pop, as in a real session */
		uint8_t got[BTAAC_AU_MAX];

		while (btaac_pop_au(got, sizeof(got), NULL))
			;
	}
}

static void engine_makes(Sim *s, uint32_t n)
{
	uint8_t au[100];

	for (uint32_t i = 0; i < n; i++) {
		memset(au, (int)s->unit_no, sizeof(au));
		CHECK(btaac_au_put(au, sizeof(au)), "the output ring has room for unit %u", s->unit_no);
		s->unit_no++;
	}
}

/* One call of the media hook: what send_aac does, step by step. Returns the pacing's decision. */
static int hook_call(Sim *s)
{
	uint8_t got[BTAAC_AU_MAX];
	uint32_t seq, keep, dropped = 0, depth;
	int dec;

	s->call++;
	keep = btaac_pace_keep(s->keep, s->preroll);
	if (keep)
		dropped = btaac_au_trim(keep);
	s->trimmed += dropped;
	depth = btaac_au_depth();
	if (keep && depth > s->max_depth_after_trim)
		s->max_depth_after_trim = depth;
	if (keep)
		CHECK(depth <= keep, "after the trim at most %u wait (%u)", keep, depth);
	dec = btaac_pace_step(&s->p, s->preroll, depth);
	if (dec == BTAAC_PACE_SEND) {
		CHECK(btaac_pop_au(got, sizeof(got), &seq) > 0, "a unit was counted, so one pops");
		btaac_pace_sent(&s->p);
		if (s->sends && s->call - s->last_send < s->closest)
			s->closest = s->call - s->last_send;
		if (s->sends < 512)
			s->sent_calls[s->sends] = s->call;
		s->last_send = s->call;
		s->sends++;
	}
	return dec;
}

/* A steady stream: the engine makes a unit on every second block (before the call, as the sender's PCM push comes first). */
static void run_steady(Sim *s, uint32_t blocks)
{
	for (uint32_t i = 0; i < blocks; i++) {
		if (s->call % 2 == 1)
			engine_makes(s, 1);
		hook_call(s);
	}
}

/* ---- the scenarios --------------------------------------------------------------------------------------------------------------------- */

static void t_pace_math(void)
{
	/* the trim never goes below the pre-roll; not bounded stays not bounded */
	CHECK(btaac_pace_keep(11, 2) == 11 && btaac_pace_keep(5, 2) == 5 && btaac_pace_keep(3, 2) == 3, "the latency levels stay as they are at a pre-roll of 2");
	CHECK(btaac_pace_keep(3, 4) == 4 && btaac_pace_keep(3, 6) == 6 && btaac_pace_keep(5, 6) == 6, "a pre-roll above the limit raises the limit to it");
	CHECK(btaac_pace_keep(0, 6) == 0 && btaac_pace_keep(0, 0) == 0, "no limit stays none");
	CHECK(btaac_pace_keep(3, 0) == 3, "no pre-roll: the limit as it was");
	/* the cadence's expected count: 0 before a send, then one for the first call and one more for every two calls after */
	BtAacPace p;
	btaac_pace_reset(&p);
	CHECK(btaac_pace_expected(&p) == 0, "nothing sent: nothing expected");
	p.calls = 10; p.first_call = 4; p.sent = 1;
	CHECK(btaac_pace_expected(&p) == 4, "call 10, first send at call 4: units at 4, 6, 8, 10");
	p.calls = 11;
	CHECK(btaac_pace_expected(&p) == 4, "the odd call in between adds none");
}

static void t_preroll_start(void)
{
	Sim s;

	/* the first unit goes only when the pre-roll is met */
	sim_init(&s, 2, 11);
	CHECK(hook_call(&s) == BTAAC_PACE_WAIT_PREROLL && s.sends == 0, "nothing ready");
	engine_makes(&s, 1);
	CHECK(hook_call(&s) == BTAAC_PACE_WAIT_PREROLL && s.sends == 0, "one unit ready, two wanted");
	engine_makes(&s, 1);
	CHECK(hook_call(&s) == BTAAC_PACE_SEND && s.sends == 1, "two ready: the first goes");
	CHECK(btaac_au_depth() == 1, "one left behind as slack");
	CHECK(hook_call(&s) == BTAAC_PACE_WAIT_SPACING, "the next call is within the spacing");
	CHECK(hook_call(&s) == BTAAC_PACE_SEND && s.sends == 2, "the second goes two calls after the first, from the slack");
	CHECK(s.p.underruns == 0 && s.p.miss_calls == 0, "the wait for the pre-roll at the start is not an underrun");
	CHECK(s.p.prerolls == 0, "and the first pre-roll is not counted as a refill");
	/* the pre-roll of 1 sends as soon as a unit is ready, like the old behaviour at the start */
	sim_init(&s, 1, 11);
	CHECK(hook_call(&s) == BTAAC_PACE_WAIT_PREROLL, "nothing yet");
	engine_makes(&s, 1);
	CHECK(hook_call(&s) == BTAAC_PACE_SEND, "one is enough for a pre-roll of 1");
	/* the largest pre-roll */
	sim_init(&s, BTAAC_PREROLL_MAX, 11);
	for (uint32_t i = 1; i < BTAAC_PREROLL_MAX; i++) {
		engine_makes(&s, 1);
		CHECK(hook_call(&s) == BTAAC_PACE_WAIT_PREROLL, "%u of %u", i, (unsigned)BTAAC_PREROLL_MAX);
	}
	engine_makes(&s, 1);
	CHECK(hook_call(&s) == BTAAC_PACE_SEND, "the sixth makes it");
}

static void t_steady(void)
{
	Sim s;

	for (uint32_t pr = 0; pr <= BTAAC_PREROLL_MAX; pr++) {
		sim_init(&s, pr, 11);
		engine_makes(&s, pr > 1 ? pr - 1 : 0);        /* the engine is ahead by the pre-roll (the first units come before the first call) */
		run_steady(&s, 200);
		CHECK(s.sends >= 95, "pre-roll %u: %u units in 200 calls", pr, s.sends);
		CHECK(s.p.underruns == 0 && s.p.miss_calls == 0 && s.p.prerolls == 0, "pre-roll %u: no underrun on a steady stream", pr);
		if (pr)
			CHECK(s.closest == 2u, "pre-roll %u: units go exactly two calls apart at the closest (%u)", pr, s.closest);
		CHECK(btaac_pace_expected(&s.p) >= s.sends - 1u && btaac_pace_expected(&s.p) <= s.sends + 1u, "pre-roll %u: the cadence expects %u, %u went", pr, btaac_pace_expected(&s.p), s.sends);
	}
}

static void t_dry_and_refill(void)
{
	Sim s;
	uint32_t sends_before, calls_dry = 0;

	sim_init(&s, 2, 11);
	engine_makes(&s, 1);
	run_steady(&s, 40);
	CHECK(s.p.underruns == 0 && s.sends > 15, "a steady start: %u sent", s.sends);
	/* the engine stops for 10 blocks: the ring runs dry, once */
	while (btaac_au_depth() > 0)
		hook_call(&s);                              /* the slack goes out */
	sends_before = s.sends;
	for (int i = 0; i < 10; i++) {
		int d = hook_call(&s);

		CHECK(d != BTAAC_PACE_SEND, "dry: nothing is sent");
		if (d == BTAAC_PACE_DRY)
			calls_dry++;
	}
	CHECK(s.sends == sends_before, "nothing sent while dry");
	CHECK(s.p.underruns == 1, "one dry episode, however long: %u", s.p.underruns);
	CHECK(s.p.miss_calls >= 4, "its calls are counted: %u", s.p.miss_calls);
	CHECK(s.p.run == 0 && calls_dry >= 1, "waiting for the pre-roll again");
	/* the engine makes one unit: still waiting (one of two); a second: it goes, and the one after follows two calls later, nothing in a burst */
	engine_makes(&s, 1);
	CHECK(hook_call(&s) == BTAAC_PACE_WAIT_PREROLL, "one unit back: not yet");
	engine_makes(&s, 1);
	CHECK(hook_call(&s) == BTAAC_PACE_SEND, "two units back: sending again");
	CHECK(s.p.prerolls == 1 && s.p.dry == 0, "one refill counted, the dry state cleared");
	CHECK(hook_call(&s) == BTAAC_PACE_WAIT_SPACING && hook_call(&s) == BTAAC_PACE_SEND, "the second goes two calls after the first");
	CHECK(s.closest == 2u, "no two units ever went closer than two calls: %u", s.closest);
	/* a second dry episode counts again */
	while (btaac_au_depth() > 0)
		hook_call(&s);
	for (int i = 0; i < 4; i++)
		hook_call(&s);
	CHECK(s.p.underruns == 2, "a second episode: %u", s.p.underruns);
}

static void t_burst_after_pause(void)
{
	/* The sender's blocks go on for 300 ms (28 blocks) while the engine is held off the CPU; then it makes every unit of the backlog at once. What goes out must be one unit per two calls,
	 * and what waits must be bounded by the latency limit and the pre-roll. */
	static const uint32_t keeps[] = { 11, 5, 3 };

	for (unsigned k = 0; k < 3; k++) {
		for (uint32_t pr = 0; pr <= BTAAC_PREROLL_MAX; pr += (pr == 2 ? 4 : 2)) {
			Sim s;
			uint32_t backlog = 14, sent_before, closest_after;
			uint32_t keep_eff = btaac_pace_keep(keeps[k], pr);

			sim_init(&s, pr, keeps[k]);
			engine_makes(&s, pr > 1 ? pr - 1 : 0);
			run_steady(&s, 60);
			sent_before = s.sends;
			s.closest = 0xFFFFu;
			for (int i = 0; i < 28; i++)              /* the stall: the calls find the ring empty after the slack is gone */
				hook_call(&s);
			/* the engine is back: 14 units (the 28 blocks' worth) in one go, the ring takes them all */
			engine_makes(&s, backlog);
			CHECK(btaac_au_depth() == backlog, "the backlog is in the ring");
			sent_before = s.sends;
			s.closest = 0xFFFFu;
			for (int i = 0; i < 60; i++) {
				if (s.call % 2 == 1)
					engine_makes(&s, 1);
				if (btaac_au_room() == 0)
					break;
				hook_call(&s);
			}
			closest_after = s.closest;
			CHECK(s.sends > sent_before, "pre-roll %u, limit %u: sending resumed", pr, keeps[k]);
			if (pr)
				CHECK(closest_after >= 2u, "pre-roll %u, limit %u: no burst: two units went %u call(s) apart", pr, keeps[k], closest_after);
			if (pr)
				CHECK(s.max_depth_after_trim <= keep_eff, "pre-roll %u, limit %u: at most %u wait (%u)", pr, keeps[k], keep_eff, s.max_depth_after_trim);
			(void)closest_after;
		}
	}
	/* the old behaviour (no pacing) does burst: what the stall used to do on the air */
	{
		Sim s;
		uint32_t before;

		sim_init(&s, 0, 11);
		run_steady(&s, 20);
		for (int i = 0; i < 28; i++)
			hook_call(&s);
		engine_makes(&s, 10);
		before = s.sends;
		s.closest = 0xFFFFu;
		for (int i = 0; i < 6; i++)
			hook_call(&s);
		CHECK(s.sends - before == 6 && s.closest == 1u, "without pacing the backlog goes out on every call (the burst)");
	}
}

static void t_trim_vs_preroll(void)
{
	Sim s;

	/* the latency level Lowest (3) with a pre-roll of 6: 6 units are kept ready, not 3; the stream starts */
	sim_init(&s, 6, 3);
	engine_makes(&s, 8);
	CHECK(hook_call(&s) == BTAAC_PACE_SEND, "6 ready is a pre-roll of 6, though the limit asks 3");
	CHECK(s.trimmed == 2 && btaac_au_depth() == 5, "the trim kept 6 (dropped the two oldest of 8), one was sent: %u waiting", btaac_au_depth());
	/* a pre-roll of 2 under the limit of 3: the limit is the limit */
	sim_init(&s, 2, 3);
	engine_makes(&s, 7);
	hook_call(&s);
	CHECK(s.trimmed == 4, "7 waiting, limit 3: four dropped (%u)", s.trimmed);
}

static void t_depth_of_this_session(void)
{
	uint8_t au[50];

	btaac_ring_open(2);
	memset(au, 1, sizeof(au));
	for (int i = 0; i < 5; i++)
		btaac_au_put(au, sizeof(au));
	CHECK(btaac_au_depth() == 5, "five wait");
	btaac_ring_open(2);                              /* a new session: the old units are dead */
	CHECK(btaac_au_depth() == 0, "none of this session's");
	btaac_au_put(au, sizeof(au));
	CHECK(btaac_au_depth() == 1, "one of this session's, the dead ones not counted");
	btaac_ring_close();
}

static void t_ring_absorbs_a_stall(void)
{
	static int16_t blk[512 * 2];
	uint32_t ov, fin, pop, dr, out;

	/* a stall of 300 ms is 28.1 blocks of 512 frames: the input ring takes them all; it used to refuse from the ninth */
	btaac_ring_open(2);
	for (int i = 0; i < 29; i++)
		CHECK(btaac_push_pcm(blk, 512) == 512, "block %d of the stall is kept", i);
	btaac_ring_info(&ov, &fin, &pop, &dr, &out);
	CHECK(ov == 0 && fin == 29u * 512u, "nothing refused: %u overruns, %u frames", ov, fin);
	CHECK(BTAAC_PCM_RING_FRAMES >= 300u * 48u, "the ring is at least 300 ms of frames");
	CHECK(BTAAC_AU_RING_SLOTS >= 14u, "and the output ring holds the units a 300 ms backlog makes (14)");
	/* a 0.6 s stall is more than the ring: refused, counted (the longest the ring can ride out is 341 ms) */
	for (int i = 29; i < 32; i++)
		CHECK(btaac_push_pcm(blk, 512) == 512, "up to the ring's end: block %d", i);
	CHECK(btaac_push_pcm(blk, 512) == 0, "past 341 ms: refused");
	btaac_ring_close();
}

static void t_prio_choice(void)
{
	uint32_t src;

	CHECK(btaac_prio_choose(0, 0x12, &src) == 0x13 && src == BTAAC_PRIO_SRC_COMPUTED, "one below the sender's 0x12");
	CHECK(btaac_prio_choose(0, 0x20, &src) == 0x21 && src == BTAAC_PRIO_SRC_COMPUTED, "one below another sender priority");
	CHECK(btaac_prio_choose(0x40, 0x12, &src) == 0x40 && src == BTAAC_PRIO_SRC_OVERRIDE, "set by hand wins");
	CHECK(btaac_prio_choose(0x70, 0x12, &src) == 0x70 && src == BTAAC_PRIO_SRC_OVERRIDE, "0x70, the old priority, by hand");
	CHECK(btaac_prio_choose(0x10, -1, &src) == 0x10 && src == BTAAC_PRIO_SRC_OVERRIDE && btaac_prio_choose(0xBF, -1, &src) == 0xBF && src == BTAAC_PRIO_SRC_OVERRIDE, "the override's bounds");
	CHECK(btaac_prio_choose(0x0F, 0x12, &src) == 0x13 && src == BTAAC_PRIO_SRC_COMPUTED && btaac_prio_choose(0xC0, 0x12, &src) == 0x13, "an override out of range is not used");
	CHECK(btaac_prio_choose(0, -1, &src) == 0x70 && src == BTAAC_PRIO_SRC_FALLBACK, "the sender's not found: the module's own");
	CHECK(btaac_prio_choose(0, 0, &src) == 0x70 && src == BTAAC_PRIO_SRC_FALLBACK, "a sender priority of 0 is not believed");
	CHECK(btaac_prio_choose(0, 0x0F, &src) == 0x70 && src == BTAAC_PRIO_SRC_FALLBACK, "nor one above the plausible band");
	CHECK(btaac_prio_choose(0, 0x70, &src) == 0x70 && src == BTAAC_PRIO_SRC_FALLBACK && btaac_prio_choose(0, 0xFE, &src) == 0x70, "nor one at or below the fallback: never lower than it");
	CHECK(btaac_prio_choose(0, 0x6F, &src) == 0x70 && src == BTAAC_PRIO_SRC_COMPUTED, "the last believable one (0x6F): one below is 0x70");
}

static void t_prio_scan(void)
{
	int sp = -2, bp = -2;

	btaac_prio_note("SceBtIntr", 0x12, &sp, &bp);
	CHECK(sp == -2 && bp == 0x12, "a Bluetooth thread: the sender is not it");
	btaac_prio_note("SceBtWorker", 0x14, &sp, &bp);
	btaac_prio_note("SceBtLow", 0x10, &sp, &bp);
	CHECK(bp == 0x14, "the lowest priority (highest number) of the Bluetooth threads: 0x%x", bp);
	btaac_prio_note("SceAVConfigDataRcev", 0x13, &sp, &bp);
	btaac_prio_note("SceAVConfigDataSend2", 0x20, &sp, &bp);
	btaac_prio_note("SceAVConfigData", 0x21, &sp, &bp);
	btaac_prio_note("SceShellMain", 0x40, &sp, &bp);
	CHECK(sp == -2 && bp == 0x14, "the sender's name is matched whole, nothing else counts");
	btaac_prio_note("SceAVConfigDataSend", 0x12, &sp, &bp);
	CHECK(sp == 0x12 && bp == 0x14, "the sender found");
	/* a whole list, as the thread manager gave it, through the engine's reader */
	{
		int s2, b2;
		uint32_t src;

		s2 = b2 = -1;
		for (int i = 0; i < 4; i++)
			btaac_prio_note(i == 0 ? "SceBtWorker" : i == 1 ? "SceAVConfigDataSend" : i == 2 ? "SceBtIntr" : "SceShellMain", i == 3 ? 0x40 : 0x12, &s2, &b2);
		CHECK(s2 == 0x12 && b2 == 0x12 && btaac_prio_choose(0, s2, &src) == 0x13, "the console's own: the sender and the Bluetooth threads at 0x12, the engine at 0x13");
	}
}

/* ---- harness ---------------------------------------------------------------------------------------------------------------------------- */

#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { const char *name; void (*fn)(void); } Scn;
static const Scn k_scn[] = {
	{ "pacing: the arithmetic (the trim's floor, the cadence)", t_pace_math },
	{ "pacing: the pre-roll at the start", t_preroll_start },
	{ "pacing: a steady stream at every pre-roll", t_steady },
	{ "pacing: a dry ring waits for the pre-roll again, no burst", t_dry_and_refill },
	{ "pacing: a burst of units after a pause is one per two calls", t_burst_after_pause },
	{ "pacing: the latency trim never goes below the pre-roll", t_trim_vs_preroll },
	{ "rings: the depth counts this session's units", t_depth_of_this_session },
	{ "rings: the input ring rides out a 300 ms stall", t_ring_absorbs_a_stall },
	{ "priority: computed, by hand, the fallback", t_prio_choice },
	{ "priority: reading the thread list", t_prio_scan },
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
			printf("  ok    %s\n", k_scn[i].name);
		}
	}
	printf("test_btaac_pace: %d scenarios, %d failed\n", (int)(sizeof(k_scn) / sizeof(k_scn[0])), failed);
	return failed ? 1 : 0;
}
