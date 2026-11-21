#!/bin/sh
# The Bluetooth sound filter's hook (kernel/bteq_hook.c) and the filter it calls (kernel/bteq_run.c)
# run inside the audio sender's thread, once per 10.67 ms block; the volume hooks (kernel/btvol_hook.c)
# and the mapping they call (kernel/btvol_map.c) run inside the sound policy thread. The object files must
#   - define the hooks and what they call (so an empty or misnamed file cannot pass),
#   - contain no floating-point or vector instruction anywhere, and no system call (svc),
#   - call nothing but each other and the microsecond clock: every relocation of a call or a
#     jump names an allowed function. The call to the original routine is indirect, so it
#     carries no relocation; a library routine (memcpy, memset, a division, an out-of-line
#     atomic), a lock, a wait or the logger would.
# The encoder hook (kernel/btqual_hook.c), the reconnect hook and standby handler (kernel/btreconn_hook.c) and the flush timeout's hooks
# (kernel/btflush_hook.c) are checked the same way: the first two with the default lists above, the last with
#   BTEQ_REQUIRED="btflush_start_hook btflush_stop_hook btflush_evt_hook" BTEQ_ALLOWED_CALLS=none
# Run by the build after the link:
#   check_bteq_leaf.sh bteq_run.c.obj bteq_hook.c.obj btvol_map.c.obj btvol_hook.c.obj btqual_hook.c.obj btreconn_hook.c.obj
set -eu
OBJDUMP="${OBJDUMP:-arm-vita-eabi-objdump}"
[ -n "$OBJDUMP" ] || OBJDUMP=arm-vita-eabi-objdump
ALLOWED="${BTEQ_ALLOWED_CALLS:-bq_process ksceKernelGetSystemTimeLow bv_send_map bv_read_map}"
REQUIRED="${BTEQ_REQUIRED:-bteq_hook bq_process btvol_send_hook btvol_read_hook bv_send_map bv_read_map btqual_hook btreconn_hook btreconn_sysevent}"
VEC='[[:space:]]v(ld|st|mov|add|sub|mul|cvt|cmp|dup|and|orr|eor|ext|zip|swp|rev|shl|shr|push|pop|mrs|msr|abs|neg|sqrt|div|fma|nmul|nmla|nmls|mla|mls|rec|rsq|sel|max|min|bic|bsl|bif|bit|tbl|trn|uzp|qadd|qsub|qmov)[a-z0-9.]*[[:space:]]'
dump=""
for obj in "$@"; do
  dump="$dump
$("$OBJDUMP" -dr "$obj")"
done
for sym in $REQUIRED; do
  case "$dump" in *"<$sym>:"*) ;; *) echo "check_bteq_leaf: no $sym in $*" >&2; exit 1 ;; esac
done
bad="$(printf '%s\n' "$dump" | grep -E "^[[:space:]]+[0-9a-f]+:[[:space:]]+[0-9a-f ]+$VEC" || true)"
if [ -n "$bad" ]; then
  echo "check_bteq_leaf: floating-point/vector instruction (the hook may use general registers only):" >&2
  printf '%s\n' "$bad" >&2
  exit 1
fi
svc="$(printf '%s\n' "$dump" | grep -E "^[[:space:]]+[0-9a-f]+:[[:space:]]+[0-9a-f ]+[[:space:]]svc[[:space:]]" || true)"
if [ -n "$svc" ]; then
  echo "check_bteq_leaf: system call instruction in the hook objects:" >&2
  printf '%s\n' "$svc" >&2
  exit 1
fi
# Every call or jump relocation, as "function target".
calls="$(printf '%s\n' "$dump" | awk '
  /^[0-9a-f]+ <[^>]+>:/ { fn = $2; gsub(/[<>:]/, "", fn); sub(/\..*/, "", fn); next }
  /R_ARM_[A-Z0-9_]*(CALL|JUMP|PLT)[A-Z0-9_]*[ \t]/ { print fn, $3 }')"
echo "$calls" | while read -r fn target; do
  [ -n "$fn" ] || continue
  ok=0
  for a in $ALLOWED; do
    [ "$target" = "$a" ] && ok=1
  done
  if [ "$ok" = 0 ]; then
    echo "check_bteq_leaf: $fn calls $target (allowed: $ALLOWED)" >&2
    exit 1
  fi
done || exit 1
echo "check_bteq_leaf: no floating-point/vector/svc instructions; calls only: $(printf '%s\n' "$calls" | awk 'NF { print $1 "->" $2 }' | sort -u | tr '\n' ' ')"
