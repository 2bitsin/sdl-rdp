#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
if ! command -v npx >/dev/null 2>&1; then
    echo 'clone-lint skipped: npx is not installed'
    exit 77
fi
includes='#include\s+[<"][^>"]*[>"]'
npx --yes jscpd --min-tokens 40 --min-lines 5 --format cpp,c --ignore-pattern "$includes" --exit-code 1 sources
exec npx --yes jscpd --min-tokens 40 --min-lines 5 --format cpp --formats-exts 'cpp:cpp,hpp' \
    --ignore-pattern "$includes" --exit-code 1 sources/SDL3.so/rdp
