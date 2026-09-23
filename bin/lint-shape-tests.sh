#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
python=$1
if ! "$python" -c 'import pytest' >/dev/null 2>&1; then
    echo "shape-lint-tests skipped: pytest is not importable by $python"
    exit 77
fi
exec "$python" -B -m pytest -p no:cacheprovider -q bin/test_lint_shape.py
