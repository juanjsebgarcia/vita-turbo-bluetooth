#ifndef BTT_BTRECONN_H
#define BTT_BTRECONN_H

/* Reconnect After Standby: call from the worker on every pass once the start-up period is over, with 1 while
 * the setting is on and Bluetooth Turbo is on, else 0. While on, the first call puts in the hook that notes
 * the headphones the console last started sound on (and a standby handler that notes a wake-up); after a
 * wake-up the worker waits until Bluetooth is up again plus 3 s and, if those headphones have no link (and
 * no sound has started on any headphones meanwhile), asks the Bluetooth code to connect them: at most twice
 * (the second 5 s after the first) per wake-up. Off: nothing is noted, counted or called; the hook, if it
 * went in, stays in passing every call through until module stop. Cheap when nothing changes. */
void btt_btreconn_sync(int on);

/* 1 while a wake-up is in hand (waiting for Bluetooth, settling, between attempts): the connect call may be made at any moment. */
int btt_btreconn_busy(void);

/* Module stop: the hook and the handler out. */
void btt_btreconn_restore(void);

#endif
