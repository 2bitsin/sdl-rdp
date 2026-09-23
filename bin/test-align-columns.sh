#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
python="${1:-python3}"
if ! "$python" -c 'import pytest' >/dev/null 2>&1; then
    echo 'align-columns-test skipped: pytest is not importable'
    exit 77
fi
"$python" bin/align-columns.py --self-check
exec "$python" -m pytest -q bin/test_align_columns.py
