#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
python="${1:-python3}"
"$python" bin/align-columns.py --self-check
exec "$python" -m pytest -q bin/test_align_columns.py
