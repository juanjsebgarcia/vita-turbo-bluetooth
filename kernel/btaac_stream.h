/*
 * btaac_stream.h - AAC streaming, the worker's side: the hooks that make a stream AAC (btaac_hook.h), the engine's session that feeds
 * it, and what ends the session. The hooks go in only when AAC is wanted: the AAC module is loaded and has passed its self-test, the
 * Bluetooth Codec key is AAC and Bluetooth Turbo is on. With the AAC module absent, or the key SBC, none of it exists and the Bluetooth
 * code runs as it always did.
 *
 * What happens to a connection: the headphones connect, the code asks every endpoint they list what it offers, and the first free sink endpoint
 * offering AAC LC at 48 kHz in stereo is kept (H1). When the code has narrowed its own choice it is replaced by the AAC configuration (H2). If the
 * sink accepts, the stream is AAC: this module starts the engine's session when the stream starts, the audio sender's blocks go to the engine,
 * and the engine's access units go out as the stream's packets (H3). If the sink rejects it, the SBC configuration is sent again, the headphones
 * are not offered AAC again until the next boot, and the stream is SBC as it always was. A change of the Bluetooth Codec key, or of the master
 * switch, applies from the next connection of the headphones (the stream has no way to change codec); a connection that is AAC keeps its
 * session until it ends: the headphones disconnect, the stream stops for more than a few seconds, the console goes to standby or shuts down,
 * the module stops, or the engine fails (then there is no sound until the headphones reconnect, and they are not offered AAC again).
 */
#ifndef BTAAC_STREAM_H
#define BTAAC_STREAM_H

#include <stdint.h>

/* The worker's pass, once the start-up period is over and the AAC module has been looked for. `on` is the master switch, `codec` the Bluetooth
 * Codec key, `accel` the hardware accelerated AAC key. Installs the hooks the first time AAC is wanted (all or none: a refusal is logged and not
 * retried until AAC has been switched off and on), publishes whether new connections are offered AAC, and runs the session by what the hooks saw. */
void btt_aac_stream_pass(int on, int codec, int accel);

/* 1 while the worker should look more often than twice a second: an AAC stream is configured and its stream record is in use (configured, opening, streaming, suspended or
 * closing), so the session may have to start or end quickly (every 0.1 s: a resume after a pause, or after a microphone has had the link, should not lose more than
 * that). 0 as soon as the link is gone or the record is idle. */
int btt_aac_stream_fast(void);

/* 1 while the connection's stream is configured as AAC (the send queue's limit is then the AAC one, btaac_lat.h). */
int btt_aac_stream_is_aac(void);

/* The Bluetooth Latency setting (a BttLatency) for the engine's output bound on an AAC stream (btaac_lat.h). Cheap; the worker calls it every pass. */
void btt_aac_stream_set_latency(int level);

/* The Music Source Only setting (default on): the Vita offers sources only, so the list the signalling hook answers a DISCOVER with has no sink and a configuration of the sink is
 * rejected (btaac_hook.c). Effective with the master switch on and the signalling hook in; the worker logs what it did. Cheap; the worker calls it every pass. */
void btt_aac_stream_set_source_only(int on);

/* The AAC bit rate asked of the headphones, kb/s (the Settings row AAC Bit Rate): taken by the next negotiation, which brings it under the headphones'
 * highest (btaac_sep_bps). Cheap; the worker calls it every pass. A value outside the encoder's own range is clamped there, not here. */
void btt_aac_stream_set_kbps(unsigned kbps);

/* The LDAC Bit Rate setting (a BTLDAC_MODE_*: 330, 660, adaptive): taken by the next LDAC session, and passed to a running one at once (the encoder's quality index is not in the
 * configuration the headphones were given). Cheap; the worker calls it every pass. */
void btt_aac_stream_set_ldac_mode(unsigned mode);

/* The aptX HD When Available setting (0/1, default 1): with the aptX codec, aptX HD (experimental) is offered to headphones that list it only while this is 1; 0 keeps standard aptX.
 * Taken by the next negotiation. Cheap; the worker calls it every pass. */
void btt_aac_stream_set_aptx_hd(int on);

/* The pre-roll (units that must be ready before the stream starts and again after a dry ring: kernel/btaac_pace.h): its default is BTAAC_PREROLL_DEFAULT; the development port sets it
 * (0..BTAAC_PREROLL_MAX: returns 1 when taken, 0 when out of range). The worker publishes it to the hook every pass. */
int btt_aac_stream_set_preroll(int units);

/* The last connection on which the codec the console offered failed for the headphones' part and SBC is used instead (they rejected the configuration, the START was
 * rejected until the retries gave up, or the stream never started): taken once. Returns a BTFALL_C_* (kernel/btfall_core.h; 0 when there is none), with the
 * headphones' address (low word, high 16 bits). aptX HD is named as such, but a refused HD stream that does not end in SBC (the connection is silent until they reconnect,
 * and the next one is plain aptX) is not reported. The worker calls it every pass (btfall.c). */
int btt_aac_stream_take_failure(uint32_t *lo, uint32_t *hi);

/* 1 once the hooks are in: the audio sender's hook (bteq.c) must be installed to hand the engine its blocks. */
int btt_aac_stream_wants_pcm_hook(void);

/* Module stop: the session ended, the hooks out, the standby handler removed. */
void btt_aac_stream_restore(void);

#endif /* BTAAC_STREAM_H */
