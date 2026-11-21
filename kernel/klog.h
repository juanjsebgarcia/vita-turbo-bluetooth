/*
 * klog.h - kernel logger for Bluetooth Turbo: one line per event, appended to
 * ur0:data/bluetoothturbo/bluetoothturbo_kernel.log (size-capped), best-effort.
 * Low-rate callers only (start-up, settings changes, re-applies).
 */
#ifndef BLUETOOTHTURBO_KLOG_H
#define BLUETOOTHTURBO_KLOG_H

#include <psp2kern/kernel/sysclib.h>   /* snprintf */

#define BTT_KLOG_MAX_BYTES  (64 * 1024)

void btt_klog_init(void);
void btt_klog_line(const char *line);

#define BTT_KLOG(...) do { char _l[192]; snprintf(_l, sizeof(_l), __VA_ARGS__); btt_klog_line(_l); } while (0)

#endif /* BLUETOOTHTURBO_KLOG_H */
