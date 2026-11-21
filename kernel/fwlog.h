/*
 * fwlog.h - log which firmware this really is, once, next to the one the
 * system reports (a version spoof changes only the reported one).
 */
#ifndef BLUETOOTHTURBO_FWLOG_H
#define BLUETOOTHTURBO_FWLOG_H

/* Call from start-up and from the worker loop: the first call logs the
 * reported version and the kernel's own record; later calls look for SceShell
 * (it is not running yet at plug-in load) and log its build once, then do
 * nothing. Purely informational: nothing is decided from any of it. */
void btt_fwlog_poll(void);

#endif /* BLUETOOTHTURBO_FWLOG_H */
