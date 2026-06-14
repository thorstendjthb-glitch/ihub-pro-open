#!/usr/bin/env python3
# Pre-Build: extrahiert das Dashboard-HTML aus main/webui_html.h, komprimiert es
# mit gzip und schreibt main/webui_html_gz.h (Byte-Array + Länge).
# Dadurch lädt das Dashboard auch über schwache WLAN-Strecken zuverlässig.
import gzip, re, os, sys

try:
    Import("env")                 # PlatformIO/SCons-Kontext
    here = env["PROJECT_DIR"]     # noqa: F821
except Exception:
    here = os.path.dirname(os.path.abspath(__file__))
src  = os.path.join(here, "main", "webui_html.h")
dst  = os.path.join(here, "main", "webui_html_gz.h")

with open(src, encoding="utf-8") as f:
    txt = f.read()

m = re.search(r'R"HTML\((.*)\)HTML"', txt, re.S)
if not m:
    sys.exit("gzip_dashboard: HTML-Block (R\"HTML(...)HTML\") nicht gefunden")

html = m.group(1).encode("utf-8")
gz = gzip.compress(html, 9)

with open(dst, "w", encoding="utf-8") as f:
    f.write("// AUTO-GENERIERT von gzip_dashboard.py — nicht von Hand editieren.\n")
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write("static const uint8_t DASHBOARD_GZ[] = {\n")
    for i, b in enumerate(gz):
        f.write("%d," % b)
        if (i & 31) == 31:
            f.write("\n")
    f.write("\n};\n")
    f.write("static const unsigned DASHBOARD_GZ_LEN = %d;\n" % len(gz))

print("gzip_dashboard: %d -> %d Bytes (%.0f%%)" % (len(html), len(gz), 100.0*len(gz)/len(html)))

# Als PlatformIO-Pre-Script: nichts weiter nötig (Datei ist geschrieben).
