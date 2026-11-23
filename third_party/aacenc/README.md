# vo-aacenc: an AAC-LC encoder

Vendored unmodified from the vo-aacenc repository:

- Repository: <https://github.com/mstorsjo/vo-aacenc>
- Commit: `a277487e051e92e99a532294eed3c673f4d879f2` (2017-06-28, version 0.1.3)
- Fetched with `git clone --depth 1` on 2026-11-22.

Only what the kernel module builds is kept. Every file keeps its own copyright and licence header.

| Files | What | Licence |
|---|---|---|
| `COPYING` | the Apache License 2.0, verbatim | - |
| `NOTICE` | the project's notice file (an Apache section 4(d) notice, the licence text, and a data-files licence inherited from the tree it came from), verbatim | - |
| `aacenc/NOTICE` | the notice of the framework the encoder was written for, verbatim | - |
| `aacenc/patent_disclaimer.txt` | the upstream's own statement on patents, verbatim | - |
| `aacenc/src/*.c`, `aacenc/inc/*.h` | the encoder | Apache-2.0 (each file's header) |
| `aacenc/basic_op/*` | its integer arithmetic helpers (`basicop2.c`, `oper_32b.c` are built) | Apache-2.0 (each file's header) |
| `aacenc/src/asm/ARMV5E/*.s`, `aacenc/src/asm/ARMV7/*.s` | the three energy routines and the three transform routines in ARM assembly with integer NEON instructions | Apache-2.0 (each file's header) |
| `common/include/*.h` | the six headers of its small interface | Apache-2.0 (each file's header) |

Not vendored, because nothing here builds them: the C++ glue for another system's media framework, the example command line
program and its WAV reader, the build scripts, `common/cmnMemory.c` (the product supplies its own memory operator), the
vendor's manual, and the ARMv5E transform routines (`PrePostMDCT_v5.s`, `R4R8First_v5.s`,
`Radix4FFT_v5.s`; the ARMv7 ones are used instead).

## How the product uses it

- `CMakeLists.txt` builds the files twice into one object each, then gives every symbol of each build its own prefix
  (`tools/prefix_symbols.sh`), so the plain C build and the NEON build live side by side in one module:
  - the C build: `-O2 -mgeneral-regs-only` (no floating-point or vector instruction at all; `tools/check_novec.sh` fails the
    build if one appears);
  - the NEON build: ARM mode (`-marm`, the inline assembly in `basic_op.h` does not build as Thumb) with the six assembly files.
- `kernel/shim/assert.h` stands in for the C library's `assert.h` (a failed check is counted and ends the session, it never
  panics); `kernel/btaac.c` supplies the memory operator (one kernel block, 8 allocations, 48,928 bytes in all) and an empty
  `__aeabi_unwind_cpp_pr1` for the assembly's unwind notes.
- `kernel/btaac_enclimit.c` is the product's own file (MIT), built with the encoder (both builds) because it reads the encoder's own bit reservoir record: it sets how large the reservoir may be, so no access unit is longer than one packet of the link takes. It checks the record is as the vendored version makes it and changes nothing if it is not. No vendored file is changed.
- Integer fixed-point throughout: no floating point, no writable globals, no allocation after start-up, about 3 KB of stack.
