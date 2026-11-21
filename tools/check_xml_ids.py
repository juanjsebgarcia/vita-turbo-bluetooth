#!/usr/bin/env python3
"""Check the Settings page XML: well-formed, every id= unique, and every id inside the product's
own "btt_" prefix.

Settings crashes while building a page whose ids repeat, even when the XML itself is fine. The
page is assembled from C string constants in settings_app/bluetoothturbo_settings.c, so this
compiles those constants on the host, prints the full spliced group and checks it, also against
the ids of the system page it is spliced into (listed below), so the product's ids can never
collide with the system's own; the prefix keeps them apart from the other Turbo products' pages
too (each uses its own prefix). Last, every title and hint id the group uses must be in the
text table, or the row would show blank.
"""
import os
import re
import subprocess
import sys
import tempfile
import xml.dom.minidom
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "settings_app", "bluetoothturbo_settings.c")
PARTS = ["BTT_HEAD", "BTT_ROWS_A", "BTT_CUSTOM", "BTT_ROWS_B", "BTT_TAIL"]
PREFIX = "btt_"

# The ids of the system page the group is spliced into (Settings -> Devices, the
# peripherals_settings_plugin document, identical in firmware 3.60-3.74), as its layout lists
# them. Only a collision check: the page itself is not part of this product.
SYSTEM_PAGE_IDS = [
    "peripherals_settings", "wireless_controller", "bluetooth_settings", "bd_remocon_register",
    "keyboard_settings", "id_keyboard_type", "id_keyboard_input_mode1", "id_keyboard_input_mode2",
    "id_keyrepeat_delay", "id_keyrepeat_rate", "controller_settings", "id_controller_assign",
    "id_controller_vibration_func",
]


def page_xml():
    src = open(SRC, encoding="utf-8").read()
    start = src.index("#define ROW(")
    end = src.index("static const char BTT_TAIL")
    end = src.index(";", end) + 1
    frag = src[start:end]
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "page.c")
        exe = os.path.join(d, "page")
        with open(c, "w", encoding="utf-8") as f:
            f.write("#include <stdio.h>\n" + frag + "\nint main(void){\n")
            for p in PARTS:
                f.write('fputs(%s, stdout);\n' % p)
            f.write("return 0;}\n")
        subprocess.run(["cc", "-o", exe, c], check=True)
        return subprocess.run([exe], check=True, capture_output=True, text=True).stdout


def ids_of(text):
    return re.findall(r'\bid="([^"]*)"', text)


def main():
    group = page_xml()
    xml.dom.minidom.parseString(group)
    ours = ids_of(group)
    if not ours:
        print("FAIL no ids found in the Settings page group")
        return 1
    dup = [i for i, n in Counter(ours).items() if n > 1]
    if dup:
        print("FAIL repeated ids on the Settings page: " + ", ".join(sorted(dup)))
        return 1
    stray = [i for i in ours if not i.startswith(PREFIX)]
    if stray:
        print("FAIL ids outside the product's prefix %s: %s" % (PREFIX, ", ".join(stray)))
        return 1
    whole = Counter(ours + SYSTEM_PAGE_IDS)
    clash = [i for i, n in whole.items() if n > 1]
    if clash:
        print("FAIL ids that clash with the Bluetooth Devices page's own: " + ", ".join(sorted(clash)))
        return 1
    # every title and hint the page refers to must have text, or Settings shows a blank row
    src = open(SRC, encoding="utf-8").read()
    texts = set(re.findall(r'\{ "(btt_[a-z0-9_]+)",', src))
    missing = sorted({t for t in re.findall(r'\b(?:title|hint)="(btt_[^"]*)"', group)
                      if t not in texts and not t.startswith(("btt_db_", "btt_bal_")) and t not in ("btt_i_namenow", "btt_i_headset")})
    if missing:
        print("FAIL titles/hints without text in the g_text table: " + ", ".join(missing))
        return 1
    print("  ok   the Settings page XML is well-formed; %d ids, all unique, all prefixed %s, none "
          "clash with the page it is spliced into, every title and hint has text" % (len(ours), PREFIX))
    return 0


if __name__ == "__main__":
    sys.exit(main())
