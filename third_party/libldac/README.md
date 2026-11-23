# libldac: the LDAC encoder

Vendored from the Android Open Source Project's copy of Sony's LDAC encoder (and its adaptive bit rate code). It is built into the optional add-on module
`bluetoothturbo_ldac.skprx`, never into the main module.

- Repository: <https://android.googlesource.com/platform/external/libldac>
- Commit: `eeee1a3f5f8df1282e3a6d297085885fd886737b` (the head of its main branch on 2026-11-23; committed 2025-01-10, "Modify apex_available to Bluetooth apex"); library version 2.0.2.
- Fetched on 2026-11-23 with `git clone --depth 1`; none of its scripts or build files was run.
- Licence: **Apache License 2.0** (`LICENSE`, verbatim). Every source and header file carries "Copyright (C) 2013 - 2016 Sony Corporation" and the same Apache header. `NOTICE` is
  Sony's notice file, verbatim.
- LDAC is a trademark of Sony Group Corporation. The name is used only to say which codec the add-on is compatible with; this project is not affiliated with or endorsed by Sony.

| Folder | What |
|---|---|
| `LICENSE`, `NOTICE` | the licence and Sony's notice, verbatim |
| `inc/ldacBT.h` | the public header |
| `src/` | the encoder: the two files the library's own build compiles (`ldacBT.c`, `ldaclib.c`, each including the others) and the files they include, in the integer (fixed point) form |
| `abr/` | the adaptive bit rate code (`ldacBT_abr.c`, `ldacBT_abr.h`) |

Not copied: `Android.bp`, `METADATA`, `OWNERS`, `MODULE_LICENSE_APACHE2`, the `fuzzer/` folder, and the floating point variants of the signal processing files (`tables_sigproc_ldac.c`,
`setpcm_ldac.c`, `mdct_ldac.c`, `sigana_ldac.c`, `quant_ldac.c`), which the integer build never includes. The product's own `CMakeLists.txt` compiles `src/ldacBT.c`, `src/ldaclib.c` and
`abr/src/ldacBT_abr.c` with `-D_32BIT_FIXED_POINT` (the library's documented setting for processors without a floating point unit), `-O2 -fno-tree-vectorize`, and its allocation calls
renamed to the add-on's static block (`-Dmalloc=btldac_malloc -Dcalloc=btldac_calloc -Dfree=btldac_free`, `kernel/btldac_mem.c`).

## Modifications (Apache section 4(b))

**None.** Every vendored file is byte for byte the file at the commit above. What the product changes is done by compile options and by its own files around the library
(`kernel/btldac_enc.c`, `btldac_mem.c`).
