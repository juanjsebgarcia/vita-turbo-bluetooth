/*
 * btevlog.h - the link log: one line in the kernel log for each Bluetooth link that comes up, fails to come up, or ends, saying who opened it, the profiles in the order
 * they opened and why it ended. The reason is the controller's own (a disconnect reason code, in plain words with its number). Meant for bug reports: "why does my headset
 * keep reconnecting". The logic is in btevlog_core.h, the one routine inside the Bluetooth code in btevlog_hook.c.
 *
 * It watches; it changes nothing. The Bluetooth code's event handler is watched through the hook that is shared with the Lowest guard (btflush.c); what it sees goes into a ring,
 * and this file, in the worker, turns it into lines: addresses are shown with the last three bytes hidden, a link that ends again and again the same way is one line and a count,
 * and the number of lines is capped per minute and per start.
 */
#ifndef BTT_BTEVLOG_H
#define BTT_BTEVLOG_H

/* Called from the worker on every pass once the start-up period is over, with 1 when Bluetooth Turbo is on. Puts the event hook in (the first time), takes what it noted,
 * writes the log lines, and notes the profiles each link has open (read from the Bluetooth code's own link slots, a few times a second for the first seconds of a link). Cheap
 * when nothing happened. With 0 nothing more is noted. */
void btt_btevlog_pass(int on);

/* 1 while a link is under 15 s old: the worker passes more often, so the order the profiles opened in is kept. */
int btt_btevlog_fast(void);

/* Module stop: stops noting and writes what is still waiting (folded repeats). */
void btt_btevlog_restore(void);

#endif
