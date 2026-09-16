"""Paths to repository-owned native tools and metadata.

GHV's Python package is currently developed and installed from a source
checkout.  Keeping repository discovery here prevents individual commands from
depending on their own file location and makes editable installs portable.
"""
from __future__ import annotations

import os
from pathlib import Path


def project_root() -> Path:
    override = os.environ.get("GHV_PROJECT_ROOT")
    if override:
        root = Path(override).expanduser().resolve()
        if (root / "VERSION.json").is_file():
            return root
        raise RuntimeError(f"GHV_PROJECT_ROOT does not contain VERSION.json: {root}")

    here = Path(__file__).resolve()
    for parent in here.parents:
        if (parent / "VERSION.json").is_file() and (parent / "native").is_dir():
            return parent

    cwd = Path.cwd().resolve()
    if (cwd / "VERSION.json").is_file() and (cwd / "native").is_dir():
        return cwd
    raise RuntimeError(
        "GHV source checkout not found; set GHV_PROJECT_ROOT to the repository root"
    )


PROJECT_ROOT = project_root()


def native_binary(name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    return PROJECT_ROOT / "native" / "bin" / f"{name}{suffix}"
