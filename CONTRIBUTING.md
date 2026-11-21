# Contributing

Thanks for helping make Bluetooth headphones on the PS Vita sound and behave better. Bug reports,
results with your own headphones, translations and code are all welcome.

## Ground rules

Bluetooth Turbo sits in the path of the sound that goes to Bluetooth headphones, so a few rules are
non-negotiable:

- **Touch only the Bluetooth code.** The module changes a few constants in the Bluetooth code and hooks
  the three calls that hand the sound to the headphones and pass their volume back and forth, and it opens the
  hands-free connection that headsets report their battery over. It never
  reaches the audio chip, the sound driver, the wired volume tables or another plugin's files and settings,
  so it works alone and beside the other Turbo plugins in either load order.
  `tools/check_coexistence.py` (part of `make test`) fails if a source names anything else, or uses an
  address that isn't on its reviewed list.
- **Keep the Home screen light.** `bluetoothturbo.suprx` (`user/`, listed under `*main`) is in the Home screen's
  memory, which is short: it only asks the kernel module for the headset's level and the Settings switch every
  30 seconds and sends a notification. Keep it that way: no log, no big buffers or stacks, no text beyond the
  one notification (`tools/gen_i18n.py` makes its table), every library a weak import checked before its first
  call, and nothing touched before the Home screen is up. It never depends on another plugin.
- **Check before you write.** A change to the Bluetooth code goes in only when the bytes are exactly the
  ones it was checked against, on every firmware 3.60 to 3.74. Don't add a write or an offset that skips
  that check, and refuse cleanly (change nothing, say why in the log) when the code isn't recognised.
- **Put everything back.** Switching the plugin off, and stopping the module, must restore the original
  behaviour, and every setting at its default must be exactly the console's own.
  `test/test_btfix.c`, `test/test_btvol.c`, `test/test_bteq.c` and `test/test_worker.c` check it.
- **Hands off during start-up.** The timing fix and the latency limit (two code constants, `btt_bt_sync`) go in
  once the Home screen is up and the console has been on for about 12 seconds; everything else, hooks and the
  controller response included, waits for about 45 seconds. `test/test_worker.c` checks every call against that.
- **Keep the hooks leaves.** `kernel/bteq_hook.c` and `kernel/bteq_run.c` run in the audio sender's thread,
  and `kernel/btvol_hook.c` and `kernel/btvol_map.c` in the sound policy thread: no logging, locks, waits,
  allocation, floating point or vector instructions, and no call but each other and the microsecond clock.
  The same goes for `kernel/btflush_hook.c` (Bluetooth Latency *Lowest*), which makes no named call at all.
  `tools/check_bteq_leaf.sh` reads the object files after every build.
- **Generated tables.** `kernel/bteq_table.h` and `kernel/btvol_table.h` come from `tools/gen_bteq_table.py`
  and `tools/gen_btvol_table.py`; `make test` fails if they are out of date.

## Building

```sh
make            # VitaSDK if $VITASDK is set, otherwise the pinned Docker image
make test       # builds, then the host tests and checks (a C compiler and Python 3)
make clean
```

Fix any new warnings in the code you touch. The build must stay clean and `make test` must pass.

## Testing on a Vita

You need HENkaku/Ensō on firmware 3.60–3.74 and
[vitacompanion](https://github.com/devnoname120/vitacompanion)
([my fork](https://github.com/juanjsebgarcia/vitacompanion)), which provides FTP on :1337 and commands
on :1338.

```sh
make deploy VITA_IP=<vita>     # upload to ur0:tai/ and reboot
make log    VITA_IP=<vita>     # fetch the kernel log
```

If a change misbehaves, **hold L while the Vita boots** to skip all plugins, then fix `ur0:tai/config.txt`
or the module over FTP.

When you report a result, include your console model, firmware, your headphones, and
`ur0:data/bluetoothturbo/bluetoothturbo_kernel.log`.

**Check the log before you post it.** It records the Bluetooth name you gave the console; replace it with a
placeholder such as `<name>` if it is personal. Keep everything else as it is.

## Translations

The Settings page is shown in all 20 Vita system languages. Only English is written by a person; the rest
are machine translations, and corrections from native speakers are very welcome. Every string is in
`tools/gen_i18n.py`, one entry per string with all 20 languages. Fix a line there, run
`python3 tools/gen_i18n.py settings_app user`, and send both the script and the generated headers. Keep
"Bluetooth Turbo" as is: it's the product name.

## Pull requests

- One logical change per PR, with a clear description of what changed and how you tested it.
- Commit messages in the existing style: `bluetoothturbo: <what changed>`, `docs: <what changed>`.
- By contributing you agree that your contribution is licensed under the MIT
  licence of this project.
