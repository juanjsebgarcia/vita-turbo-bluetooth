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
DEVROWS = os.path.join(HERE, "..", "settings_app", "bluetoothturbo_devrows.h")
# The page is built from these parts (build_group): the codec row is its open and close with an item for each of AAC, aptX and LDAC that can be used (an
# item's value is its place in the list: aptX is 2 behind AAC and 1 without it, LDAC 3, 2 or 1); the Codec Settings sub-page holds the AAC rows with the AAC
# add-on (the second only when the vector build passed its check), the LDAC Bit Rate row with the LDAC add-on, aptX HD When Available with the aptX add-on once its HD
# self-test passed (aptx = 2 below), and always SBC Audio Quality, last. Every
# combination the module can build is checked (keep this in step with build_group in settings_app/bluetoothturbo_settings.c): the AAC add-on present or
# not (AAC usable needs it), the accelerated build or not (needs it), aptX usable or not (and with its HD self-test passed or not), the LDAC add-on present or not (LDAC usable needs it).
SUB_OPEN, SUB_CLOSE = "BTT_CODEC_PAGE_OPEN", "BTT_CODEC_PAGE_CLOSE"


def variants():
    out = []
    for module in (0, 1):
        for aac in (0, 1):
            if aac and not module:
                continue
            for accel in (0, 1):
                if accel and not module:
                    continue
                for aptx in (0, 1, 2):   # 2: aptX usable and its HD self-test passed (aptX HD When Available is shown)
                    for ldac in (0, 1):
                        parts = ["BTT_HEAD", "BTT_ROWS_A", "BTT_CODEC_HELP", "BTT_CODEC_OPEN"]
                        parts += ["BTT_CODEC_ITEM_AAC"] * aac + ["BTT_CODEC_ITEM_APTX" if aac else "BTT_CODEC_ITEM_APTX_2ND"] * (aptx > 0)
                        parts += ["BTT_CODEC_ITEM_LDAC_%d" % (3 if aac + (aptx > 0) == 2 else 2 if aac + (aptx > 0) == 1 else 1)] * ldac + ["BTT_CODEC_CLOSE", SUB_OPEN]
                        parts += ["BTT_AAC_RATE"] * module + ["BTT_AAC_ACCEL"] * accel + ["BTT_LDAC_RATE"] * ldac + ["BTT_APTX_HD"] * (aptx == 2) + ["BTT_SBC_QUALITY", SUB_CLOSE]
                        out.append(parts + ["BTT_ROWS_A2", "BTT_CUSTOM", "BTT_ROWS_B", "BTT_TAIL"])
    return out


VARIANTS = variants()
# What the structure must be: the Bluetooth Codec Help sub-page directly before the codec row, holding six read-only lines (labels, each with a hint); the Codec Settings sub-page
# directly after the codec row, holding exactly the per-codec rows; Bluetooth Latency (shared by every codec) on the top level.
HELP_ID = "btt_codechelp"
HELP_LINES = 6
SUB_ID = "btt_codecset"
SUB_ROWS = ["btt_aacrate", "btt_aacaccel", "btt_ldacrate", "btt_aptxhd", "btt_quality"]
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


# The ids of Sony's page for one Bluetooth device (bluetooth_settings_menu.xml, the same on every firmware 3.60-3.74): the rows the
# product adds to it (settings_app/bluetoothturbo_devrows.h) must not use them.
DEVICE_PAGE_IDS = ["id_bluetooth_settings_menu", "id_bt_device_name", "id_bt_device_type", "id_bt_profile"]
DEVROWS_PREFIX = "btt_lk_"


def devrows_xml(macro):
    """The device page rows as C builds them: BTT_DEVROWS_XML (the list is known) or BTT_DEVROWS_XML_REFRESH (it is not: the Refresh Codecs button too)."""
    src = open(DEVROWS, encoding="utf-8").read()
    start = src.index("#define BTT_DEVROWS_HEAD")
    end = src.index("static const char btt_devrows_xml[]")
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "rows.c")
        exe = os.path.join(d, "rows")
        with open(c, "w", encoding="utf-8") as f:
            f.write("#include <stdio.h>\n" + src[start:end] + "\nint main(void){fputs(%s, stdout);return 0;}\n" % macro)
        subprocess.run(["cc", "-o", exe, c], check=True)
        return subprocess.run([exe], check=True, capture_output=True, text=True).stdout


def check_devrows(main_ids):
    """The rows on the device page, with and without the Refresh Codecs button: well-formed inside the page's own list, ids unique and prefixed, apart from
    Sony's ids on that page and from the product's own page, every label's title id one of the rows the text hook answers (the row names in devrows.h),
    and the button's title and help ids answered by the button text function (btt_devrows_button_text)."""
    hdr = open(DEVROWS, encoding="utf-8").read()
    names = re.findall(r'"([a-z]+)"', re.search(r"name\[BTT_DR_COUNT\] = \{([^}]*)\}", hdr).group(1))
    for macro, button in (("BTT_DEVROWS_XML", False), ("BTT_DEVROWS_XML_REFRESH", True)):
        rows = devrows_xml(macro)
        xml.dom.minidom.parseString('<setting_list id="id_bluetooth_settings_menu">' + rows + "</setting_list>")
        ids = ids_of(rows)
        if not ids:
            print("FAIL no ids in the device page rows (%s)" % macro)
            return 1
        if any(n > 1 for n in Counter(ids).values()):
            print("FAIL repeated ids in the device page rows (%s): " % macro + ", ".join(sorted(i for i, n in Counter(ids).items() if n > 1)))
            return 1
        stray = [i for i in ids if not i.startswith(DEVROWS_PREFIX)]
        if stray:
            print("FAIL device page row ids outside the prefix %s: %s" % (DEVROWS_PREFIX, ", ".join(stray)))
            return 1
        clash = sorted(set(ids) & set(DEVICE_PAGE_IDS + main_ids))
        if clash:
            print("FAIL device page row ids that clash with Sony's page or the product's: " + ", ".join(clash))
            return 1
        # the button is a <button> with a title and a help; every other row a <label> with a title
        buttons = re.findall(r"<button\b[^>]*>", rows)
        if len(buttons) != (1 if button else 0):
            print("FAIL the page %s has %d buttons, expected %d" % (macro, len(buttons), 1 if button else 0))
            return 1
        if button:
            b = buttons[0]
            bid = re.search(r'\bid="([^"]*)"', b).group(1)
            btitle = re.search(r'\btitle="([^"]*)"', b).group(1)
            bhint = re.search(r'\bhint="([^"]*)"', b)
            defined = dict(re.findall(r'#define BTT_DR_REFRESH_(ID|TITLE|HINT)\s+"([^"]*)"', hdr))
            if not bhint or (bid, btitle, bhint.group(1)) != (defined.get("ID"), defined.get("TITLE"), defined.get("HINT")):
                print("FAIL the Refresh Codecs button's ids on the page and the ones the press handler and the text function answer (BTT_DR_REFRESH_*) differ: %s / %s" % (b, defined))
                return 1
        label_ids = [i for i in ids if i != "btt_lk_refresh"]
        label_titles = re.findall(r'<label\b[^>]*\btitle="([^"]*)"', rows)
        bad = [t for t in label_titles if not (t.startswith("btt_lt_") and t[7:] in names)]
        if bad or len(label_titles) != len(label_ids) or sorted("btt_lk_" + n for n in names) != sorted(label_ids):
            print("FAIL device page rows and the row names in bluetoothturbo_devrows.h differ: ids %s, names %s, titles %s" % (label_ids, names, label_titles))
            return 1
        print("  ok   the device page rows %s: well-formed, %d ids unique, prefixed %s, none clash with Sony's page or the product's, every title is answered" % ("with the Refresh Codecs button" if button else "without it", len(ids), DEVROWS_PREFIX))
    return 0


def page_xml(parts):
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
            for p in parts:
                f.write('fputs(%s, stdout);\n' % p)
            f.write("return 0;}\n")
        subprocess.run(["cc", "-o", exe, c], check=True)
        return subprocess.run([exe], check=True, capture_output=True, text=True).stdout


def ids_of(text):
    return re.findall(r'\bid="([^"]*)"', text)


def main():
    for parts in VARIANTS:
        rc = check(page_xml(parts))
        if rc:
            return rc
    return check_devrows(ids_of(page_xml(VARIANTS[0])))


# SBC Audio Quality's list, low to high: the item at place i has the value i (the stored key's value: enum BttQuality in include/bluetoothturbo.h) and the name of that level.
QUALITY_ITEMS = [("btt_q_stable0", "btt_i_q_stable"), ("btt_q_bal1", "btt_i_q_bal"), ("btt_q_std2", "btt_i_standard"), ("btt_q_high3", "btt_i_q_high"), ("btt_q_highest4", "btt_i_q_highest")]
QUALITY_ENUM = ["BTT_Q_STABLE", "BTT_Q_BALANCED", "BTT_Q_STANDARD", "BTT_Q_HIGH", "BTT_Q_HIGHEST"]


def check_quality(doc):
    """SBC Audio Quality lists Stable, Balanced, Standard, High, Highest in that order, and a saved value shows as the item of that value: the item's value is its place and
    the enum's number for that level (so a file's stored 3 shows High, 4 Highest)."""
    q = [e for e in doc.getElementsByTagName("list") if e.getAttribute("id") == "btt_quality"]
    if len(q) != 1:
        print("FAIL no SBC Audio Quality list (btt_quality)")
        return 1
    items = [(e.getAttribute("id"), e.getAttribute("title"), e.getAttribute("value")) for e in q[0].childNodes if e.nodeType == e.ELEMENT_NODE]
    if items != [(i, t, str(n)) for n, (i, t) in enumerate(QUALITY_ITEMS)]:
        print("FAIL SBC Audio Quality items %s: expected %s in this order, each with its place as its value" % (items, QUALITY_ITEMS))
        return 1
    hdr = open(os.path.join(HERE, "..", "include", "bluetoothturbo.h"), encoding="utf-8").read()
    m = re.search(r"enum BttQuality \{([^}]*)\}", hdr)
    names = [x.split("=")[0].strip() for x in m.group(1).split(",") if x.strip()][:len(QUALITY_ENUM)]
    if names != QUALITY_ENUM or "= 0" not in m.group(1).split(",")[0]:
        print("FAIL enum BttQuality is %s; the list's places 0..4 stand for %s" % (names, QUALITY_ENUM))
        return 1
    return 0


def check_help(top, top_ids):
    """The Bluetooth Codec Help sub-page: a nested <setting_list> directly before the codec row, with six <label> lines, each with a title, a hint and an icon."""
    if HELP_ID not in top_ids or "btt_codec" not in top_ids or top_ids.index(HELP_ID) != top_ids.index("btt_codec") - 1:
        print("FAIL the Bluetooth Codec Help sub-page (%s) must be a direct child of the page, right before btt_codec: %s" % (HELP_ID, top_ids))
        return 1
    h = top[top_ids.index(HELP_ID)]
    if h.tagName != "setting_list" or not h.getAttribute("hint"):
        print("FAIL %s must be a nested <setting_list> with a hint" % HELP_ID)
        return 1
    lines = [e for e in h.childNodes if e.nodeType == e.ELEMENT_NODE]
    if len(lines) != HELP_LINES or any(e.tagName != "label" or not e.getAttribute("title") or not e.getAttribute("hint") or not e.getAttribute("icon") for e in lines):
        print("FAIL the codec help page must hold %d <label> lines, each with a title, a hint and an icon: %s" % (HELP_LINES, [e.toxml() for e in lines]))
        return 1
    return 0


LOWBATT_ORDER = ["btt_readbatt", "btt_headset", "btt_lowbatt", "btt_codec"]


def check_lowbatt_grey(top_ids):
    """Low Battery Notifications is greyed while Read Headset Battery is Off, from the get of the codec row (the settings page's get hook): that row must be a direct child of
    the same page and built after it, or the row would not exist yet when it is greyed. The Headset Battery row, shown again from the switch's set, is on the same page between them.
    The source must make both calls."""
    pos = [top_ids.index(i) if i in top_ids else -1 for i in LOWBATT_ORDER]
    if -1 in pos or pos != sorted(pos):
        print("FAIL the rows %s must be direct children of the page in this order (the greying trigger is the codec row, built after Low Battery Notifications): %s" % (LOWBATT_ORDER, top_ids))
        return 1
    src = open(SRC, encoding="utf-8").read()
    for needle in ("if (k == BTT_KEY_CODEC)\n\t\t\tgrey_lowbatt_row();", "if (k == BTT_KEY_READBATT) {\n\t\t\trefresh_headset_row();"):
        if needle not in src:
            print("FAIL the Settings page's hooks lack the Low Battery Notifications greying (%r not found in %s)" % (needle, os.path.basename(SRC)))
            return 1
    return 0


def check_structure(doc):
    """The Bluetooth Codec Help sub-page sits directly before the codec row; the Codec Settings sub-page sits directly after it, holds only per-codec rows (SBC Audio Quality
    always, in the last place), and Bluetooth Latency stays on the top level."""
    top = [e for e in doc.documentElement.childNodes if e.nodeType == e.ELEMENT_NODE]
    top_ids = [e.getAttribute("id") for e in top]
    if check_help(top, top_ids) or check_quality(doc):
        return 1
    if check_lowbatt_grey(top_ids):
        return 1
    if SUB_ID not in top_ids or "btt_codec" not in top_ids or top_ids.index(SUB_ID) != top_ids.index("btt_codec") + 1:
        print("FAIL the Codec Settings sub-page (%s) must be a direct child of the page, right after btt_codec: %s" % (SUB_ID, top_ids))
        return 1
    sub = top[top_ids.index(SUB_ID)]
    if sub.tagName != "setting_list":
        print("FAIL %s is a <%s>, not a nested <setting_list>" % (SUB_ID, sub.tagName))
        return 1
    rows = [e.getAttribute("id") for e in sub.childNodes if e.nodeType == e.ELEMENT_NODE]
    if not rows or rows[-1] != "btt_quality" or any(r not in SUB_ROWS for r in rows):
        print("FAIL the Codec Settings sub-page holds %s; expected some of %s, ending with btt_quality (the greying trigger)" % (rows, SUB_ROWS))
        return 1
    if "btt_latency" not in top_ids:
        print("FAIL Bluetooth Latency (btt_latency) must stay on the top level: %s" % top_ids)
        return 1
    stray = [r for r in SUB_ROWS if r in top_ids]
    if stray:
        print("FAIL per-codec rows left on the top level: " + ", ".join(stray))
        return 1
    return 0


def check(group):
    doc = xml.dom.minidom.parseString(group)
    if check_structure(doc):
        return 1
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
                      if t not in texts and not t.startswith(("btt_db_", "btt_bal_")) and t not in ("btt_i_namenow", "btt_i_headset", "btt_h_codec")})
    if missing:
        print("FAIL titles/hints without text in the g_text table: " + ", ".join(missing))
        return 1
    print("  ok   the Settings page XML is well-formed; the codec help page precedes the codec row with %d lines, the Codec Settings sub-page follows it and holds %d row(s), SBC Audio Quality lists low to high; %d ids, all unique, all prefixed %s, none "
          "clash with the page it is spliced into, every title and hint has text" % (HELP_LINES, len(re.findall(r'id="btt_(?:aacrate|aacaccel|ldacrate|aptxhd|quality)"', group)), len(ours), PREFIX))
    return 0


if __name__ == "__main__":
    sys.exit(main())
