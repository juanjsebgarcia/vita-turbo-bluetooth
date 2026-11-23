#!/bin/sh
# Fail if an object file holds a floating-point or vector instruction. Used on the C build of the encoder and on the module's own
# code, which are compiled -mgeneral-regs-only: kernel code the system runs in someone else's thread must not touch that state.
#   OBJDUMP=... check_novec.sh OBJ...
set -eu
OBJDUMP="${OBJDUMP:-arm-vita-eabi-objdump}"
VEC='[[:space:]](v[a-z0-9]+(\.[a-z0-9]+)*|f(ld|st)[a-z]*)[[:space:]]'
bad=0
for obj in "$@"; do
  hits="$("$OBJDUMP" -d "$obj" | grep -E "^[[:space:]]+[0-9a-f]+:[[:space:]]+[0-9a-f ]+$VEC" || true)"
  if [ -n "$hits" ]; then
    echo "check_novec: $obj holds floating-point/vector instructions:" >&2
    printf '%s\n' "$hits" | head -5 >&2
    bad=1
  fi
done
[ "$bad" = 0 ] || exit 1
echo "check_novec: no floating-point or vector instruction in $#" "object(s)"
