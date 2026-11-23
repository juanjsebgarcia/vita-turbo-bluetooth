#!/usr/bin/env python3
"""Check that Bluetooth Turbo stays apart from the other Turbo plugins.

It must work alone, and side by side with the others in either load order, so it may touch only
the Bluetooth code and the audio sender's Bluetooth hand-offs (the audio blocks and the volume
number) and may share nothing with another product. This reads the product's
own sources (kernel/, include/, settings_app/) and fails if any of them

  - names a system module that is not on a short list (kernel: the Bluetooth stack and the audio
    sender, where the two constants and the send hook are, and the start-up code's lookups of
    SceShell and the module manager; Home screen module: the notification block's size and a few numbers (below). Settings module: the Settings app's own modules), or names
    an audio, codec or headphone-jack module or function at all;
  - uses a hexadecimal number (a library or function number, an offset, a register) that is not
    on the reviewed list below, so nothing can reach another driver's code or data without the
    list, and this check, being changed in the same commit;
  - names another product: any "<word>turbo" identifier, file name, data folder or syscall
    library other than bluetoothturbo, or an identifier with another product's prefix.

It also runs on its own examples first, so a pattern that stopped matching is noticed.

    python3 tools/check_coexistence.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
DIRS = ["kernel", "include", "settings_app", "user"]
EXT = (".c", ".h")
SKIP = {"bteq_table.h"}   # generated numbers, all decimal tables

# Quoted module names each part may use.
ALLOWED_MODULES = {
    # "SceAVConfigDataSend" is a thread's name, not a module's: the AAC engine reads that thread's priority (the thread manager's list: read only) to run just below it
    "kernel": {"SceBt", "SceAVConfig", "SceAVConfigDataSend", "SceShell", "SceSysmem", "SceKernelModulemgr"},
    "include": set(),
    # "ScePaf": the Settings process's own widget library, asked for its allocator (the rows on a Bluetooth device's page store text in the page's
    # string objects, which free with it)
    "settings_app": {"SceSettings", "SceSystemSettingsCore", "ScePaf"},
    "user": set(),
}

# Hexadecimal numbers of three digits or more each part may use. Kernel: the send hook's library and
# function numbers, the two volume imports' function numbers (SceAVConfig's calls of SceBt's AVRCP
# volume send and read) and the error the volume hooks return before taiHEN has stored their
# reference (the one SceBt itself returns), the offsets of the byte-checked changes in SceBt's code
# (the timestamp step, the queue limit, the two controller sites), of the routine that applies an A2DP
# codec configuration (the connection quality hook, and the place in the connection where it keeps the headphones'
# bitpool range) and, in the flush timeout and guard, of its three hooked routines (stream
# start, stream stop, the event handler and the code after its entry) and the command sender, and that command's
# opcode, the start-up code's lookups
# (shell process, firmware version, SceShell's module number on each firmware), the settings-file
# magic, a stack size, the block length and the sender's error code. The two changes in the audio
# policy's code for the headphones' disconnect (SceAVConfig, the four bytes at two sites and the stock bytes
# around them are checked), the Bluetooth name text and the format texts around it (SceBt), the import number of
# the start of sound on a device and the three Bluetooth connect/state calls the reconnect looks up (the same library as
# the volume calls), the error SceBt returns for a device that is not connected, the name's file magic, the masks
# of a 16-bit part and of a reading's top bit. Home screen module: the notification block's size and a few numbers (below). Settings module: the Settings app's own import numbers, the
# registry key type, and the two string-key imports (the Bluetooth name) with the string key type.
ALLOWED_HEX = {
    "kernel": {
        0x800, 0x2000, 0x3FFFFFFF, 0x802F0D07, 0x785C, 0x791E, 0xD48CA62D, 0x47F19727,
        0x7689DA3D, 0xC9C70056, 0x802F1901, 0x132EC, 0x1341C, 0x732C, 0x7338, 0x3BB8, 0x3BBC, 0x6F50, 0x71C8, 0x10434, 0xC28, 0xC848, 0xC854,
        0x31545442, 0x314E5442, 0x32515442,
        0xF68, 0xF70, 0x12DC, 0x12E2,                      # SceAVConfig: the disconnect changes and their windows
        0x1EFA4, 0x1EFA8,                                  # SceBt: the name text and its window
        0x8D47CABD, 0x6059113A, 0xE9936446, 0x94C66A3B,    # SceBtForDriver: start sound, connect, state, configuration
        0x50710281,                                        # SceBtForDriver: disconnect (btcall.c: the settings that apply at once)
        0x2C9DD7BB,                                        # SceBtForDriver: the information call for a device, read only (btcall.c: link quality and signal strength, for the Signal row and bttGetLinkInfo)
        0x802F0204, 0x802F0203, 0x802F020E,                # SceBt's answers to a connect that means "not now": driver busy, a link exists or is being made (btapply_core.h asks again)
        0x802F0D01, 0xFFFF, 0x7FFFFFFF,
        0x05093E7B, 0x2ED7F97A, 0x67AAB627, 0xD4A60A52, 0x5182E212,
        0x0552F692, 0x532155E5, 0xBB4B0A3E, 0x5549BF1F, 0x34B4D82E, 0x12DAC0F3,
        0x0703C828, 0x2053B5A5, 0xF476E785, 0x939FFBE9, 0x734D476A, 0x51CB6207,
        # AAC streaming (btaac_gate_data.h, btaac_hook.*, btaac_stream.c, btlink.c): SceBt's signalling receive routine and its windows, the media packet
        # send routine, the frame sender, the SET_CONFIGURATION format and the call site of the code's own send of it (0xA988); where a link slot keeps the
        # endpoint list, its index, the source stream record, the media channel and the profile bits; the link table and the A2DP stream's words in the
        # data segment (the headset battery code reads the same table); the error the send routine returns when it cannot send;
        # the AudioSpecificConfig (AAC LC, 48 kHz, stereo) and the page mask of the translation check; the stream's RTP stream number base.
        0x9F4C, 0x9F58, 0x9F8E, 0xA2B0, 0xA912, 0xAD6C, 0xAD8C, 0x109EC, 0x109FA, 0x1EDD8, 0xA988,
        0x3B35, 0x3B46, 0x3B70, 0x3B8C, 0x3E1C, 0x104A8, 0x36528, 0x3652C, 0x37AF4,
        0x802F0508, 0x1190, 0xFFF, 0x56544100,
        # stage 2b: the windows the acceptor path and the in-flight count rely on, compared by a hash (btaac_gate_data.h)
        #: their offsets, lengths, the offsets of the loader's address loads inside them, their hashes, the two frame
        # formats the replies use; the power-down events the standby handler tells apart (cancelled, point of no return).
        0x3FE8, 0x7674, 0x107E0, 0x10A5A, 0xA356, 0xA40A, 0xA702, 0xA990, 0xAA8C, 0x1EDC0, 0x1EDCC, 0x1EDE0,
        0x32, 0x16, 0xC8, 0x68, 0x34, 0x244, 0x40, 0xCC, 0x9E, 0x30,
        0x28, 0x64, 0x6C, 0xB8, 0xC4, 0xCE, 0x172, 0x176, 0x180, 0x1D8, 0x1DE, 0x1E8, 0x1F6, 0x10, 0x1A, 0x2E, 0x26, 0x86, 0x8C,
        0xA1D1E9E9, 0x35B72C9B, 0x095BE6F4, 0xAE473105, 0xA01860EF, 0xA58D9024, 0xEC6C8BE4, 0x74C06E56, 0x374BDFD2, 0xA01D42CB,
        0x101, 0x102, 0x20F,
        # the START retry and the watchdog (kernel/btaac_stream.c, btaac_hook.h): the link slot's "starting" word (bit 3), the
        # parked request, the voice bit; SceBt's START reject handler, ksceBtStartAudio's body and the START and SUSPEND senders (hashed windows: offsets, lengths, the
        # offsets of the loader's address loads inside them, hashes); the retry's raised priority
        0x3E18, 0x3E24, 0x20000000, 0xA64E, 0x70D8, 0xAC0C, 0x3E, 0xF0, 0x160,
        0x2054CFD3, 0x6445E8D0, 0xA0C65D6D,
        0x74, 0x76, 0x7A, 0x7C, 0x90, 0x94, 0xA4, 0xA8, 0xAC, 0xAE, 0xB0, 0xB2, 0xD4, 0xD8, 0xE6, 0xEA, 0x13E, 0x146, 0x156, 0x15A,
    },
    "include": set(),
    "settings_app": {
        0x00040000, 0x00200001, 0x16DDF3DC, 0x19CEFDA7, 0x19FE55A8, 0x2DCC4AFA, 0x3D643CE8,
        0x4D9A9DD0, 0x58421DD1, 0x41D320C5, 0xC436F916, 0xCAE9ACE6, 0xD72EA399, 0xE188382F,
        # the settings core's own enable and grey-out calls for a row by id (the greying of the AAC rows)
        0xF832D8FB, 0x9D058B17,
        # and its call that removes or adds a row by id (the Headset Battery row is shown again when Read Headset Battery changes)
        0x84029629,
        # the rows on a Bluetooth device's page (settings_app/bluetoothturbo_devrows.h): the import SceSettings makes to set a plugin's functions (hooked to
        # wrap two of the Bluetooth plugin's), the widget library's allocator (library and function numbers), the range a user address must lie in to be
        # used as that function, and the mask of a device address's high 16 bits
        0xF5354FEF, 0xA7D28DAE, 0xFC5CD359, 0x80000000, 0xF0000000, 0xFFFF,
    },
}

# The Home screen module (user/): the size of the system notification's request block, the words of an unconnected weak import's stub
# (checked before every call), the system's notification library's module number and the default user thread priority.
ALLOWED_HEX["user"] = {0x410, 0x470, 0x468, 0x46C, 0x80, 0x2000, 0x33, 0xE24FC008, 0xE3A0F000, 0x10000100, 0x1F, 0x3F, 0x0F, 0x7F}

# Audio quality High and Highest: the packet-type constant and packet cap sites of SceBt, their stock and changed
# constants and the place in the connection where the channel mode is kept.
ALLOWED_HEX["kernel"] |= {0x7048, 0x7050, 0x7560, 0x7566, 0x3BC4, 0x3C18, 0x2C18, 0xC18, 0x15D, 0x295}

# The headset battery code (kernel/bthfp*.[ch]): offsets in SceBt's code and data, the sizes and the words of the service records
# (class numbers, handles, attribute ids), the instruction encodings of the table move, the link table's strides and fields, and the
# lock's, the connect's and the close's windows: every one is byte-checked against the code on all twelve builds before
# anything is written; none names another driver.
ALLOWED_HEX["kernel"] |= {
        0x0, 0x1, 0x4, 0x8, 0x9, 0x100, 0x200, 0x248, 0x25C, 0x260, 0x264, 0x268, 0x2B8, 0x301, 0x30C, 0x311, 0x318, 0xC20, 0x1000,
        0x1108, 0x1112, 0x111E, 0x111F, 0x1203, 0x14A4, 0x14AB, 0x14B0, 0x2478, 0x3B30, 0x3E10, 0x3E14, 0x3E28, 0x3E68, 0x41D0,
        0x64F8, 0xF100, 0xF240, 0xF2C0, 0x10000, 0x10003, 0x10005, 0x10006, 0x10007, 0x10008, 0x1000A, 0x1000B, 0x1000D,
        0x1000F, 0x10010, 0x10011, 0x104A8, 0x11544, 0x116E4, 0x1189C, 0x118E4, 0x118F0, 0x155DC, 0x155DD, 0x15608, 0x15609,
        0x15654, 0x15655, 0x15798, 0x15799, 0x15E1C, 0x1986C, 0x19870, 0x19874, 0x19914, 0x19918, 0x19C7C, 0x19E60, 0x19E66,
        0x19E6C, 0x19FD8, 0x19FDC, 0x19FEE, 0x19FF6, 0x1AC24, 0x1AC28, 0x1AC2C, 0x1AC34, 0x1B6B4, 0x1B7A0, 0x1F3E8, 0x1F868,
        0x1FB20, 0x2D960, 0x33C60, 0x473D0, 0x7FFFFFFD, 0x7FFFFFFE, 0x802F0701,
        # the microphone (kernel/bthfp_mic.h): the two recompute sites, the voice bit of the device record and of the link's words, the profile bits
        0x14676, 0x1A638, 0x20000000, 0x30003, 0x3E24,
        # the service table's hash check (kernel/bthfp_sdp.h): its FNV-1a 64-bit hash, and FNV's offset basis and prime
        0x86CA732FC05BFA38, 0xCBF29CE484222325, 0x100000001B3,
        # the same hash check for the lock, unlock, connect, headset-connect and close routines (kernel/bthfp_conn.h) and the reply routine
        # (kernel/bthfp_mod.c): each routine's first bytes, with the loader's address loads left out, hashed
        0x00E4F8CA71592B06, 0x2E264870FFCCB156, 0x25604197563D82FF, 0x325EBCF134FA557A, 0xCD799887026F71BE, 0x4B3DFFAF3C45CF83
}

# The AAC engine (kernel/btaac*.c, btaac_core.h): the system core's thread affinity mask (the engine's thread is the only one pinned there),
# its stack size (the encoder's deepest path is about 3 KB), and the two multipliers of the integer hash that makes the test signals'
# noise. None is an address, a library or function number or a register: the engine touches no other driver.
ALLOWED_HEX["kernel"] |= {0x80000, 0x4000, 0x7FEB352D, 0x846CA68B}
# The number of the AAC module's one export, btaacGetApi, which the main module looks up in our own optional module (kernel/btaac_api.h): the
# number the build gives that name, checked against the build by tools/check_aac_nid.py. It names nothing of any other driver.
ALLOWED_HEX["kernel"] |= {0x5CFD5B5F}
# The aptX module (kernel/btaptx*.[ch], btaptx_sep.h): the number of its one export, btaptxGetApi (kernel/btaptx_api.h, checked against the build by tools/check_aac_nid.py), the
# FNV-1a hash the self-test compares the encoder's bytes with (made on the host by an independent encoder: kernel/btaptx_core.h), and the codec's vendor id as the Bluetooth
# specification's capability element carries it (a protocol number: kernel/btaptx_sep.h). None is an address, a library or function number of another driver.
ALLOWED_HEX["kernel"] |= {0x144737D5, 0xDB97AFEC90714FAE, 0x4F, 0x48401DD4901DF60D, 0xD7, 0x24}   # aptX: the add-on's export, the self-tests' hashes, the vendor and codec ids (classic 0x4F, HD 0xD7 and 0x24)
ALLOWED_HEX["kernel"] |= {0x12D, 0xAA, 0x61469F2B, 0x47EE778677FA09DD, 0x779317A219BD9753}   # LDAC: the vendor and codec ids (0x0000012D, 0x00AA) of the media codec element, the add-on's export, the self-tests' hashes
# The thread manager's system-wide pid (0x10021): the engine lists the threads with it to read the audio sender's priority (read only; the kernel's own pid is refused by that call).
ALLOWED_HEX["kernel"] |= {0x10021}

# The link log (kernel/btevlog*.[ch]): the standard L2CAP protocol numbers (PSMs) it names profiles by: HID control and interrupt, AVCTP, AVDTP,
# AVCTP browsing (and RFCOMM, 3), and the 24-bit mask of an address half. They are protocol numbers and masks, not addresses; the log only reads the Bluetooth code's own link slots.
ALLOWED_HEX["kernel"] |= {0x3, 0x11, 0x13, 0x17, 0x19, 0x1B, 0xFFFFFF}

# The headphones' codec list (kernel/btcodecs.h, include/bluetoothturbo.h): the vendor and codec numbers of the media codec element by which each codec is told
# apart (A2DP codec tables: protocol numbers, not addresses; the aptX and LDAC ones are above), and the magic of the
# list's own file. The signalling hook reads the headphones' replies only.
ALLOWED_HEX["kernel"] |= {0xAD, 0xA, 0x53A, 0x4C4C, 0x4C32, 0x4C33, 0x4C35, 0x75, 0x8A9, 0xE0, 0x5F1, 0x1005, 0x102, 0x103, 0x484C, 0x25, 0x2, 0x4C435442}

# (label, regex) - anything found in a source is a failure.
FORBIDDEN = [
    ("an audio, codec or headphone-jack module", r"\bSce(Codec|Audio|Hpremote|Hprm)\w*"),
    ("an audio, codec or headphone-jack function", r"\bksce(Codec|Audio|Hprm)\w*"),
    ("an identifier with another plugin's prefix", r"\bAT_[A-Z0-9]|\bat_[a-z0-9]"),
]


def check_text(text, part="kernel"):
    """Problems in one source text of `part` (a folder name), as (label, matched text)."""
    out = []
    for label, rx in FORBIDDEN:
        for m in re.finditer(rx, text):
            out.append((label, m.group(0)))
    # a "<word>turbo" token (identifier, file name, data folder, syscall library) of another
    # product; the words "Turbo" and "Bluetooth Turbo" on their own are fine
    for m in re.finditer(r"(?i)\b\w*turbo\w*", text, re.ASCII):
        tok = m.group(0).lower()
        if "bluetoothturbo" in tok:
            continue
        if re.search(r"[a-z0-9_]turbo|^turbo[a-z]{3,}", tok):
            out.append(("another product's name or library", m.group(0)))
    for m in re.finditer(r'"(Sce[A-Za-z0-9]+)"', text):
        if m.group(1) not in ALLOWED_MODULES[part]:
            out.append(("a system module outside the allowed list", m.group(1)))
    for m in re.finditer(r"\b0[xX]([0-9A-Fa-f]{3,})[uUlL]*\b", text):
        if int(m.group(1), 16) not in ALLOWED_HEX[part]:
            out.append(("a hexadecimal number outside the reviewed list", m.group(0)))
    return out


# What the check must catch, and what it must let through.
MUST_FAIL = [
    'taiGetModuleInfoForKernel(KERNEL_PID, "SceCodec", &ti);',
    'module_get_export_func(KERNEL_PID, "SceHpremote", 0x12345678, 0x9ABCDEF0, &f);',
    "ksceCodecWriteRegister(reg, v);",
    "#define NID_SOMETHING 0x12345678",
    "#define VOLUME_TABLE_OFFSET 0x1050",
    '#define PATH "ur0:data/exampleturbo/settings.bin"',
    "int r = at_get(AT_KEY_ENABLE);",
    "SceExampleTurbo",
    '#define PATH "ur0:data/turboexample/x"',
    'const char *m = "SceAudioOut";',
    'taiHookFunctionImportForKernel(KERNEL_PID, ref, "SceAVConfig", 0xD48CA62D, 0x12345678, h);',
    'taiGetModuleInfoForKernel(KERNEL_PID, "SceSomethingElse", &ti);',
]
MUST_PASS = [
    'taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &ti);',
    'taiHookFunctionImportForKernel(KERNEL_PID, ref, "SceAVConfig", 0xD48CA62D, 0x47F19727, h);',
    'taiHookFunctionImportForKernel(KERNEL_PID, ref, "SceAVConfig", 0xD48CA62D, 0x7689DA3D, h);',
    'taiHookFunctionImportForKernel(KERNEL_PID, ref, "SceAVConfig", 0xD48CA62D, 0x8D47CABD, h);',
    'taiGetModuleInfoForKernel(KERNEL_PID, "SceAVConfig", &ti);',
    'module_get_export_func(KERNEL_PID, "SceBt", 0xD48CA62D, 0x6059113A, &f);',
    'module_get_export_func(KERNEL_PID, "SceSysmem", TAI_ANY_LIBRARY, 0x05093E7B, &f);',
    '#define BTT_DIR "ur0:data/bluetoothturbo"',
    "SceBluetoothTurbo, bttGet, bttSet, Bluetooth Turbo, BluetoothTurboLog, 0x2000, 0x20",
]


def self_test():
    for t in MUST_FAIL:
        if not check_text(t):
            print("FAIL the check does not catch: " + t)
            return 1
    for t in MUST_PASS:
        hits = check_text(t)
        if hits:
            print("FAIL the check rejects an allowed line: %s -> %s" % (t, hits))
            return 1
    return 0


def sources():
    for d in DIRS:
        top = os.path.join(ROOT, d)
        for dirpath, _, names in os.walk(top):
            for n in sorted(names):
                if n.endswith(EXT) and n not in SKIP:
                    yield d, os.path.join(dirpath, n)


def main():
    if self_test():
        return 1
    bad = []
    count = 0
    for part, path in sources():
        count += 1
        text = open(path, encoding="utf-8").read()
        for label, found in check_text(text, part):
            bad.append("%s: %s: %s" % (os.path.relpath(path, ROOT), label, found))
    if not count:
        print("FAIL no sources found")
        return 1
    if bad:
        print("FAIL the module reaches outside the Bluetooth code, or into another product:")
        print("\n".join("  " + b for b in bad))
        return 1
    print("  ok   %d sources touch only the Bluetooth code and name no other product "
          "(the check itself catches %d known bad lines)" % (count, len(MUST_FAIL)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
