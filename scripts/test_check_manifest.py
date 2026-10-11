import json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_manifest as cm

HDR = ("#define CROSSPAD_GUI_VERSION_MAJOR 1\n#define CROSSPAD_GUI_VERSION_MINOR 23\n"
       "#define CROSSPAD_GUI_VERSION_PATCH 0\n#define CROSSPAD_GUI_REQUIRES_CORE_MAJOR 1\n"
       "#define CROSSPAD_GUI_REQUIRES_CORE_MINOR 53\n#define CROSSPAD_GUI_REQUIRES_CORE_PATCH 0\n")


def test_consistent_manifest_passes():
    assert cm.problems(HDR, {"version": "1.23.0", "dependencies": {"crosspad-core": "^1.53.0"}}) == []


def test_version_and_range_must_follow_the_header():
    p = cm.problems(HDR, {"version": "1.22.0", "dependencies": {"crosspad-core": "*"}})
    assert p == ["library.json version 1.22.0, header 1.23.0",
                 "library.json dependencies.crosspad-core '*', header wants '^1.53.0'"]


def test_the_repository_itself():
    root = Path(__file__).resolve().parents[1]
    hdr = (root / "include" / "crosspad-gui" / "CrosspadGuiVersion.hpp").read_text()
    assert cm.problems(hdr, json.loads((root / "library.json").read_text())) == []
