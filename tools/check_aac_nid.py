#!/usr/bin/env python3
"""Check that the number the main module looks the AAC module's export up by (BTAAC_NID_GET_API in kernel/btaac_api.h) is the one the build
gave the export's name (the stubs file the build writes beside the AAC module).

    python3 tools/check_aac_nid.py build/bluetoothturbo_aac-stubs.yml kernel/btaac_api.h
    python3 tools/check_aac_nid.py build/bluetoothturbo_aptx-stubs.yml kernel/btaptx_api.h btaptxGetApi BTAPTX_NID_GET_API
"""
import re
import sys


def nid_in_stubs(text, name="btaacGetApi"):
    m = re.search(r"^\s*" + name + r":\s*0x([0-9a-fA-F]+)\s*$", text, re.M)
    return int(m.group(1), 16) if m else None


def nid_in_header(text, macro="BTAAC_NID_GET_API"):
    m = re.search(r"#define\s+" + macro + r"\s+0x([0-9a-fA-F]+)u?\b", text)
    return int(m.group(1), 16) if m else None


def main(argv):
    if len(argv) not in (3, 5):
        print(__doc__)
        return 2
    name, macro = (argv[3], argv[4]) if len(argv) == 5 else ("btaacGetApi", "BTAAC_NID_GET_API")
    built = nid_in_stubs(open(argv[1]).read(), name)
    wanted = nid_in_header(open(argv[2]).read(), macro)
    if built is None or wanted is None:
        print("FAIL: could not read the number (stubs: %s, header: %s)" % (built, wanted))
        return 1
    if built != wanted:
        print("FAIL: %s says %s 0x%08X, the build gave %s 0x%08X" % (argv[2], macro, wanted, name, built))
        return 1
    print("  ok   %s is 0x%08X in the build and in %s" % (name, built, argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
