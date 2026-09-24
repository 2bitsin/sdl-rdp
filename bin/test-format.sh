#!/bin/sh
# The fixture is a fixed point of bin/format.sh, its unaligned copy formats back to it, and a second pass is idle.
set -euo pipefail
case "${1:-}" in
-*) echo 'usage: bin/test-format.sh [PYTHON]' >&2; exit 2 ;;
esac
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
python="${1:-python3}"
fixture="$root/bin/format-fixture.cpp"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
mkdir "$scratch/formatted" "$scratch/unaligned"

format_and_compare() {
    "$root/bin/format.sh" --python "$python" "$scratch/$1"
    diff -u "$fixture" "$scratch/$1/fixture.cpp"
}

fixture_is_a_fixed_point() {
    cp "$fixture" "$scratch/formatted/fixture.cpp"
    format_and_compare formatted
}

unaligned_copy_formats_back() {
    sed -E 's/([^ ])  +/\1 /g' "$fixture" >"$scratch/unaligned/fixture.cpp"
    if cmp -s "$fixture" "$scratch/unaligned/fixture.cpp"; then
        echo 'format-test: the fixture has no aligned columns to remove' >&2
        exit 1
    fi
    format_and_compare unaligned
}

second_pass_changes_nothing() {
    format_and_compare unaligned
}

fixture_is_a_fixed_point
unaligned_copy_formats_back
second_pass_changes_nothing
