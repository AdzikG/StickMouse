"""Builds the firmware and writes web/install.html, a self-contained installer page.

Usage (from the repo root):  python tools/build-installer.py [--no-build]

The page embeds the flash images and esptool-js, so it works opened from disk
(file://) in Chrome or Edge, without PlatformIO or Python on the target PC.
"""
import base64
import glob
import json
import os
import re
import subprocess
import sys
from datetime import date

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")
BUILD = os.path.join(FW, ".pio", "build", "sticks3")
BUNDLE = os.path.join(ROOT, "tools", "vendor", "esptool-js-0.7.0.bundle.js")
TEMPLATE = os.path.join(ROOT, "tools", "install.template.html")
OUT = os.path.join(ROOT, "web", "install.html")

# Same layout and flash parameters as `pio run -t upload` (see its write_flash line).
FLASH = {"flashMode": "dio", "flashFreq": "80m", "flashSize": "8MB"}
BOOT_APP0 = os.path.expanduser(
    "~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")
IMAGES = [
    (0x0000, os.path.join(BUILD, "bootloader.bin")),
    (0x8000, os.path.join(BUILD, "partitions.bin")),
    (0xE000, BOOT_APP0),
    (0x10000, os.path.join(BUILD, "firmware.bin")),
]


def find_pio():
    for name in ("pio", "platformio"):
        try:
            subprocess.run([name, "--version"], capture_output=True, check=True)
            return name
        except (OSError, subprocess.CalledProcessError):
            pass
    found = glob.glob(os.path.expandvars(r"%APPDATA%\Python\Python3*\Scripts\pio.exe"))
    if found:
        return found[0]
    sys.exit("PlatformIO (pio) not found; build manually and use --no-build.")


def git_version():
    try:
        out = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=ROOT,
                             capture_output=True, text=True, check=True)
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "?"


def bundle_as_object():
    """Replaces the bundle's trailing `export{a as B,...}` with `const ESPTOOL={B:a,...}`."""
    with open(BUNDLE, encoding="utf-8") as f:
        src = f.read()
    m = re.search(r"export\s*\{([^}]*)\};?\s*$", src)
    if not m:
        sys.exit("Unexpected esptool-js bundle format (no trailing export list).")
    pairs = []
    for item in m.group(1).split(","):
        local, _, name = item.strip().partition(" as ")
        pairs.append(f"{(name or local).strip()}:{local.strip()}")
    return src[:m.start()] + "const ESPTOOL={" + ",".join(pairs) + "};"


def main():
    if "--no-build" not in sys.argv:
        subprocess.run([find_pio(), "run"], cwd=FW, check=True)

    files = []
    for address, path in IMAGES:
        with open(path, "rb") as f:
            files.append({"address": address, "data": base64.b64encode(f.read()).decode()})
    version = f"{git_version()}, {date.today().isoformat()}"
    firmware = json.dumps({"version": version, "files": files, **FLASH})

    with open(TEMPLATE, encoding="utf-8") as f:
        page = f.read()
    page = (page.replace("__ESPTOOL_BUNDLE__", bundle_as_object())
                .replace("__FW_JSON__", firmware)
                .replace("__FW_VERSION__", version))
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print(f"Wrote {os.path.relpath(OUT, ROOT)} ({os.path.getsize(OUT) // 1024} KB), firmware {version}")


if __name__ == "__main__":
    main()
