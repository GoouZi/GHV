#!/usr/bin/env python3
"""Print the authoritative GHV project and codec versions."""
from __future__ import annotations

import json
from ghv.paths import PROJECT_ROOT


def main() -> None:
    path = PROJECT_ROOT / "VERSION.json"
    print(json.dumps(json.loads(path.read_text(encoding="utf-8")), indent=2))


if __name__ == "__main__":
    main()
