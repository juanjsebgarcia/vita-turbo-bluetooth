/*
 * btcall.h - the Bluetooth code's connect calls, as the worker uses them (btcall.c): is Bluetooth up, what state is a device's link in,
 * connect a device, disconnect a device. Shared by Reconnect After Standby (btreconn.c) and the settings that apply at once (btapply.c).
 */
#ifndef BTT_BTCALL_H
#define BTT_BTCALL_H

#include <stdint.h>

#define BTCALL_STATE_NO_LINK  1   /* btcall_state: the device has no link at all */

/* Looks the three calls up by number (once; the numbers are the same on every firmware 3.60-3.74). 0 when all are found, else -1 (and nothing is
 * set: a missing call disables the feature, with a log line from the caller). */
int btcall_find(void);

/* Looks the disconnect call up too (once). 0 when it and the three above are found, else -1. */
int btcall_find_disconnect(void);

/* The calls. Each takes the Bluetooth code's lock for a few microseconds, so the worker raises its priority around it and nothing can preempt it
 * holding the lock. Only after btcall_find (and, for btcall_disconnect, btcall_find_disconnect) returned 0.
 * btcall_up: 1 while the Bluetooth code says it is up (the state word's bits 0x38 == 8, as the code itself tests before a connect).
 * btcall_state: the device's state (BTCALL_STATE_NO_LINK = none; any other is connecting or connected), or a negative error.
 * btcall_connect / btcall_disconnect: the code's own answer (0 = accepted, negative = refused). */
int btcall_up(void);
int btcall_state(uint32_t mac0, uint32_t mac1);
int btcall_connect(uint32_t mac0, uint32_t mac1);
int btcall_disconnect(uint32_t mac0, uint32_t mac1);

/* The controller's own readings of a device's radio link, from the Bluetooth code's information call (the one the system keeps for its tests: it fills 0x50
 * bytes, the link quality at +0x42 and the signal strength at +0x43). The call itself asks the controller to refresh them at most once a second; what it returns
 * is what the earlier answers stored, so the first call after a connection returns zeros. Read only. Called from the worker, never from a hook (it takes the
 * Bluetooth code's lock and fails in interrupt context). btcall_find_signal: looks the call up (once); 0 when found, else -1.
 * btcall_signal: 0 and the two readings (*lq 0..255, *rssi signed), or the call's own negative error (Bluetooth not up, device not held). */
int btcall_find_signal(void);
int btcall_signal(uint32_t mac0, uint32_t mac1, unsigned *lq, int *rssi);

#endif
