#!/bin/sh
# Merge the object files of one encoder build into a single relocatable object and give every symbol it defines a prefix, so
# two builds of the same encoder (plain C, and the one with the NEON assembly) can live in one module.
#   prefix_symbols.sh PREFIX OUT.o LD NM OBJCOPY OBJ...
# Only the symbols the objects define are renamed (inside the objects too: the calls between them follow); the symbols they import
# (the library routines) keep their names.
set -eu
prefix=$1; out=$2; ld=$3; nm=$4; objcopy=$5; shift 5
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$ld" -r -o "$tmp/all.o" "$@"
"$nm" --defined-only -g "$tmp/all.o" | awk -v p="$prefix" 'NF == 3 { print $3 " " p $3 }' > "$tmp/syms"
[ -s "$tmp/syms" ] || { echo "prefix_symbols: no defined symbols in $*" >&2; exit 1; }
"$objcopy" --redefine-syms="$tmp/syms" "$tmp/all.o" "$out"
