"""Project version metadata loaded from the repository source of truth."""
from __future__ import annotations

import json
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_DATA = json.loads((_ROOT / "VERSION.json").read_text(encoding="utf-8"))

PROJECT_VERSION = str(_DATA["project_version"])
PROJECT_STATUS = str(_DATA["project_status"])
GHV_CONTAINER_VERSION = str(_DATA["ghv_container_version"])
GHA_CONTAINER_VERSION = str(_DATA["gha_container_version"])
CURRENT_GHVC = int(_DATA["current_ghvc_encoder"])
SUPPORTED_GHVC = tuple(int(v) for v in _DATA["supported_ghvc_decoders"])
CURRENT_GHAC = int(_DATA["current_ghac_encoder"])
SUPPORTED_GHAC = tuple(int(v) for v in _DATA["supported_ghac_decoders"])
GHV_STUDIO_VERSION = str(_DATA["ghv_studio_version"])
GHA_STUDIO_VERSION = str(_DATA["gha_studio_version"])
PLAYER_VERSION = str(_DATA["player_version"])
LIBGHV_VERSION = str(_DATA["libghv_version"])


def version_summary() -> str:
    return f"GHV {PROJECT_VERSION} ({PROJECT_STATUS}), GHVC{CURRENT_GHVC}, GHAC{CURRENT_GHAC}"
