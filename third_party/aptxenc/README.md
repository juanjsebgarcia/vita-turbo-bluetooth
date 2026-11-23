# aptxenc: the aptX and aptX HD encoders

Vendored from the Bluetooth module of the Android Open Source Project, where they are two small integer-only encoders. They are built into the optional add-on
module `bluetoothturbo_aptx.skprx`, never into the main module.

- Repository: <https://android.googlesource.com/platform/packages/modules/Bluetooth>
- Commit: `745ee92b87ed62b5a8a1bac47d5df8cad623bf59` (the head of its main branch on 2026-11-23; committed 2025-03-17)
- Folders: `system/embdrv/encoder_for_aptx/` (copied to `classic/`) and `system/embdrv/encoder_for_aptxhd/` (copied to `hd/`)
- Fetched on 2026-11-23 as the source archives of those two folders at that commit.
- Licence: **Apache License 2.0** (`LICENSE`). All 34 source and header files carry the same header, "Copyright (C) 2022 The Android Open Source Project".
- aptX is a trademark of its owner. The name is used only to say which codec the add-on is compatible with; this project is not affiliated with or endorsed by it.

| Folder | What |
|---|---|
| `LICENSE` | the Apache License 2.0, a copy of `../aacenc/COPYING` (the upstream folders carry no licence file of their own) |
| `classic/include/aptXbtenc.h`, `classic/src/*` | the aptX encoder: 16 source and header files, and the public header |
| `hd/include/aptXHDbtenc.h`, `hd/src/*` | the aptX HD encoder, the same layout |

Neither upstream folder has a `NOTICE` file.

Not copied: each folder's `Android.bp` (the build file of the system it comes from) and its one-line `README.md`. Nothing here is built with those: the product's own
`CMakeLists.txt` compiles the `src/*.c` files (targets `aptx_lib_classic` and `aptx_lib_hd`).

## The one modification (Apache section 4(b))

`classic/src/AptxParameters.h` and `hd/src/AptxParameters.h`, in each, one line:

```
#elif defined __GNUC__
-#define XBT_INLINE_ inline
+#define XBT_INLINE_ static inline
```

The file picks `static inline` for one compiler family and plain `inline` for another. With plain `inline` under the C99 rules, gcc (the compiler of the console's
toolchain) leaves the encoder's helper functions as undefined external symbols and the link fails; `static inline` is what the other branch already uses. A comment
at the place says the file was modified, by whom and when. No other file or line is changed (the two `AptxParameters.h` files differ from the archive by exactly that
line and the comment above it).

## How the product uses them

- The caller supplies the encoder's memory: `SizeofAptxbtenc()` is 4,588 bytes and `SizeofAptxhdbtenc()` 4,840 on the console (5,008 and 5,256 on a 64-bit host).
  `kernel/btaptx.c` keeps two static buffers of 5,376 bytes, and the host test checks both records fit.
- No allocation, no C library call, no floating point or vector instruction, no writable global (the tables are read-only); about 280 bytes of stack in the
  deepest call. `tools/check_bteq_leaf.sh` checks the compiled objects of both encoders for that after every link (`CMakeLists.txt`).
- One call encodes four stereo frames: the left and the right samples in two arrays of four 32-bit integers (16-bit values for aptX; the same values shifted up by 8
  for aptX HD), and two codewords come back. The encoders are initialised with the byte order argument 0 and `kernel/btaptx_leaf.c` writes the bytes most significant
  first itself: the encoders' own byte swap gives a wrong high byte for a negative codeword.
- Output is bit for bit the stream of a widely used open-source reference encoder (run as a command-line tool, a black box) for signals below about 0.7 of full scale, including the self-test's signal (its hashes are in
  `kernel/btaptx_core.h`). Only for a near-full-scale input, where the filter output overshoots and is clipped, do the two differ: these encoders saturate at the
  points their authors chose. `test/test_btaptx.c` keeps its bit-exact comparison with that tool below that level.
