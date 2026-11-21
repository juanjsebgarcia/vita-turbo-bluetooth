#ifndef BTT_BTNAME_H
#define BTT_BTNAME_H

/* The Bluetooth name: put the name in place or take it out, to match `name` (up to BTT_NAME_MAX printable
 * ASCII characters; "" or NULL = the console's own, which takes the change out). Called from the worker on
 * every pass once the start-up period is over, with "" when Bluetooth Turbo is off. The Bluetooth code keeps
 * its name in one 17-byte text, used for the local name sent to the radio and (its first 16 bytes) for the
 * extended inquiry response, so the change is one data inject over it, checked against the stock text and
 * the bytes around it. Other devices see the new name from the next time Bluetooth is switched on (a
 * change of the setting does not touch a Bluetooth that is already up). Cheap when nothing changes. */
void btt_btname_sync(const char *name);

/* Module stop: the original text back. */
void btt_btname_restore(void);

#endif
