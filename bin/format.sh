#!/bin/sh
# clang-format lays out the lines, then align-columns sets the columns; --check fails with the diff instead.
set -euo pipefail
usage='usage: bin/format.sh [--check] [--python PYTHON] [DIRECTORY]'
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
check=false
python=python3
while [ $# -gt 0 ]; do
    case "$1" in
    --check)  check=true; shift ;;
    --python) python=$2; shift 2 ;;
    -*)       echo "$usage" >&2; exit 2 ;;
    *)        break ;;
    esac
done
tree="${1:-$root/sources}"
clang_format="${CLANG_FORMAT:-$(command -v clang-format-20 || command -v clang-format || true)}"
if [ -z "$clang_format" ] || ! "$clang_format" --version | grep -q 'version 20\.'; then
    echo 'format: clang-format 20 is not installed' >&2
    exit 1
fi

format_tree() {
    find "$1" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 |
        xargs -0 -r -n 16 -P "$(nproc)" "$clang_format" -i --style="file:$root/.clang-format"
    "$python" "$root/bin/align-columns.py" "$1"
}

if ! $check; then
    format_tree "$tree"
    exit 0
fi
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
copy="$scratch/$(basename "$tree")"
cp -R "$tree" "$copy"
format_tree "$copy"
diff -ru "$tree" "$copy"
