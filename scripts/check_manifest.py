#!/usr/bin/env python3
"""library.json follows CrosspadGuiVersion.hpp: its version is the header's, and its
dependencies.crosspad-core is ^CROSSPAD_GUI_REQUIRES_CORE_*. platform-idf's dependency
resolver reads that range (tools/deps_resolve.py); the header's exact pairing stays the
compile-time check. Run before every library release tag.

  python3 scripts/check_manifest.py
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _triple(text: str, prefix: str) -> str:
    parts = []
    for p in ("MAJOR", "MINOR", "PATCH"):
        m = re.search(rf"^[ \t]*#define[ \t]+{prefix}_{p}[ \t]+(0|[1-9][0-9]*)[ \t]*$", text, re.M | re.ASCII)
        if m is None:
            raise ValueError(f"{prefix}_{p} not found")
        parts.append(m.group(1))
    return ".".join(parts)


def problems(header: str, lib: dict) -> list[str]:
    out = []
    version = _triple(header, "CROSSPAD_GUI_VERSION")
    if lib.get("version") != version:
        out.append(f"library.json version {lib.get('version')}, header {version}")
    want = "^" + _triple(header, "CROSSPAD_GUI_REQUIRES_CORE")
    have = (lib.get("dependencies") or {}).get("crosspad-core")
    if have != want:
        out.append(f"library.json dependencies.crosspad-core {have!r}, header wants {want!r}")
    return out


def main() -> int:
    found = problems((ROOT / "include" / "crosspad-gui" / "CrosspadGuiVersion.hpp").read_text(),
                     json.loads((ROOT / "library.json").read_text()))
    for p in found:
        print(f"::error::{p}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
