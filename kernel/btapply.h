/*
 * btapply.h - settings that need a new connection apply at once (btapply.c; the decisions are in btapply_core.h): Bluetooth Codec, AAC Bit Rate,
 * Hardware Accelerated AAC, SBC Audio Quality and aptX HD When Available, changed while headphones are connected, disconnect the headphones and connect them again from the
 * console, once, a moment after the last change. The same reconnect, once, brings headphones that connected on SBC before the chosen codec was ready (a
 * headset that reconnects at power-on) onto the codec: btapply_core.h, "the upgrade of an early SBC connection".
 */
#ifndef BTT_BTAPPLY_H
#define BTT_BTAPPLY_H

/* The worker's pass, once the start-up period is over, after the audio link has been read (btt_link_pass): the five settings as they are now
 * (`codec` a BttCodec, `kbps` the AAC bit rate asked in kb/s, `accel` 0/1, `quality` a BttQuality, `aptx_hd` 0/1) and the master switch. Cheap when nothing changes. */
void btt_btapply_pass(int on, int codec, int kbps, int accel, int quality, int aptx_hd);

/* 1 while the console must not mute itself when the headphones go: the worker then puts btdisc.c's no-mute mode in (Play on Speaker's), whatever the
 * disconnect setting is. */
int btt_btapply_hold(void);

/* Refresh Codecs (BTT_KEY_REFRESH_CODECS), from the syscall: asks the next pass to disconnect the headphones on the link and connect them again so the console
 * reads their codec list (when the list is not known; the pass checks that and the other guards, and logs why it declines). BTT_OK once noted,
 * BTT_ERR_BUSY while the last request has not been taken. Only a flag is set: nothing is called here. */
int btt_btapply_request_refresh(void);

#endif
