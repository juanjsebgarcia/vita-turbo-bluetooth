/*
 * shell.h - has the Home screen (SceShell) started yet?
 */
#ifndef BLUETOOTHTURBO_SHELL_H
#define BLUETOOTHTURBO_SHELL_H

/* Call from the worker, about once a second. Returns 1 once SceShell exists (it is not running
 * yet when the module starts), and from then on always 1. Read only. If the lookup is not
 * available it returns 0 for good, and the boot guard falls back on the plain timer. */
int btt_shell_up(void);

#endif
