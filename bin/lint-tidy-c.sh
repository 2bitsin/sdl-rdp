#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
database=${1:-_build/x86_64-linux-gcc-debug}
if [ ! -x /usr/local/bin/clang-tidy ]; then
    echo 'tidy-c-lint skipped: /usr/local/bin/clang-tidy is absent'
    exit 77
fi
if [ ! -f "$database/compile_commands.json" ]; then
    echo "tidy-c-lint skipped: $database/compile_commands.json is absent"
    exit 77
fi
status=0
for source in sources/SDL3.so/rdp/*.c; do
    /usr/local/bin/clang-tidy -p "$database" "$source" || status=1
done
exit "$status"
