#!/usr/bin/env python3
"""Ensure generated/native and Python version metadata match VERSION.json."""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from ghv.version import CURRENT_GHAC, CURRENT_GHVC, PROJECT_STATUS, PROJECT_VERSION
from ghv._version_generated import PACKAGE_VERSION
from tools.generate_version_header import pep440_version


def main() -> None:
    data = json.loads((ROOT / "VERSION.json").read_text(encoding="utf-8"))
    assert PROJECT_VERSION == data["project_version"]
    assert PROJECT_STATUS == data["project_status"]
    assert CURRENT_GHVC == data["current_ghvc_encoder"]
    assert CURRENT_GHAC == data["current_ghac_encoder"]
    assert PACKAGE_VERSION == pep440_version(data["project_version"])
    before = (ROOT / "native" / "ghv_version.h").read_bytes()
    python_before = (ROOT / "ghv" / "_version_generated.py").read_bytes()
    subprocess.run([sys.executable, str(ROOT / "tools" / "generate_version_header.py")], check=True)
    after = (ROOT / "native" / "ghv_version.h").read_bytes()
    python_after = (ROOT / "ghv" / "_version_generated.py").read_bytes()
    assert before == after, "native/ghv_version.h was stale; regenerated it"
    assert python_before == python_after, "ghv/_version_generated.py was stale; regenerated it"
    print(f"[PASS] version source: {PROJECT_VERSION} {PROJECT_STATUS}, GHVC{CURRENT_GHVC}, GHAC{CURRENT_GHAC}")


if __name__ == "__main__":
    main()
