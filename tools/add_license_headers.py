"""Adds the license header (SPDX + "Created by KosaWLeb", required by NOTICE) to every
MacWin source file that lacks it. Safe to run again: files that have it are skipped.

    python tools/add_license_headers.py
"""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
SPDX = "SPDX-License-Identifier: GPL-3.0-or-later"
CREDIT = "MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin"
PORTED = ('Portions ported from the Windhawk mod "MacOS Minimize Animation" (MIT, Abdullah Masood',
          "and Potassiumuncher); see THIRD_PARTY_NOTICES.md.")
# Files with logic ported from the Windhawk mod (see THIRD_PARTY_NOTICES.md).
PORTED_FILES = {
    "src/hook/effects/genie_hooks.cpp", "src/hook/hooks.cpp", "src/host/render/genie.hlsl",
    "src/host/render/genie_effect.h", "src/host/render/genie_effect.cpp", "src/host/taskbar_locator.cpp",
    "src/host/taskbar_locator.h", "settings-app/Services/GenieMath.cs",
}
C_LIKE = {".cpp", ".h", ".c", ".hlsl", ".cs", ".in"}
HASH = {".cmake", ".txt", ".ps1", ".py"}


def header_lines(rel, prefix, suffix=""):
    lines = [SPDX, CREDIT] + (list(PORTED) if rel in PORTED_FILES else [])
    return [f"{prefix}{line}{suffix}" for line in lines]


def main():
    files = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    files += subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=ROOT, capture_output=True,
                            text=True, check=True).stdout.split()
    changed = 0
    for rel in sorted(set(files)):
        if rel.startswith(("third_party/", "i18n/", "installer/languages/")):
            continue
        path = ROOT / rel
        suffix = path.suffix.lower()
        if suffix in C_LIKE:
            head = header_lines(rel, "// ")
        elif suffix == ".xaml":
            head = ["<!-- " + SPDX + " -->", "<!-- " + CREDIT + " -->"]
        elif suffix in HASH and (rel == "CMakeLists.txt" or suffix != ".txt"):
            head = header_lines(rel, "# ")
        elif suffix == ".iss":
            head = header_lines(rel, "; ")
        else:
            continue
        raw = path.read_bytes()
        bom = raw.startswith(b"\xef\xbb\xbf")
        text = raw[3:].decode("utf-8") if bom else raw.decode("utf-8")
        if SPDX in text[:400]:
            continue
        newline = "\r\n" if "\r\n" in text else "\n"
        lines = text.split(newline)
        keep_first = lines and lines[0].startswith("#!")
        body = lines[1:] if keep_first else lines
        new = ([lines[0]] if keep_first else []) + head + ([""] if body and body[0].strip() else []) + body
        out = newline.join(new).encode("utf-8")
        path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + out)
        changed += 1
        print("header:", rel)
    print(f"{changed} file(s) updated")


if __name__ == "__main__":
    main()
