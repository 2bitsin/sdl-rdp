#!/usr/bin/env python3
"""Run the SDL driver TUs, or one with --unit, under the project checks; compiler errors also fail."""
import argparse
import concurrent.futures
import json
import pathlib
import re
import shlex
import subprocess
import sys

DROPPED_PREFIXES = ('-fconstexpr-ops-limit=', '--embed-dir=')


def compile_flags(entry):
    arguments = entry.get('arguments') or shlex.split(entry['command'])
    flags = []
    skip = False
    for argument in arguments[1:]:
        if skip:
            skip = False
        elif argument in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
        elif argument not in ('-c', entry['file']) and not argument.startswith(DROPPED_PREFIXES):
            flags.append(argument)
    return flags


def check(entry, root):
    flags = compile_flags(entry)
    command = ['clang-tidy', '--quiet', f'--config-file={root / ".clang-tidy"}',
               '--header-filter=sources/SDL3\\.so/rdp/.*\\.(hpp|cpp)$',
               entry['file'], '--', *flags]
    result = subprocess.run(command, cwd=entry['directory'], capture_output=True, text=True)
    output = result.stdout + result.stderr
    failed = result.returncode != 0 or re.search(r'\b(warning|error):', output)
    return bool(failed), output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database', type=pathlib.Path)
    parser.add_argument('--unit', type=pathlib.Path, help='check one driver TU, so ctest runs one entry per TU')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    units = [args.unit] if args.unit else (root / 'sources/SDL3.so/rdp').glob('*.cpp')
    expected = {path.resolve() for path in units}
    entries = {pathlib.Path(entry['file']).resolve(): entry for entry in json.loads(args.database.read_text())
               if pathlib.Path(entry['file']).resolve() in expected}
    if entries.keys() != expected:
        sys.exit(f'driver TUs missing from compile database: {expected - entries.keys()}')
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        results = list(pool.map(lambda entry: check(entry, root), entries.values()))
    for _, output in results:
        print(output, end='')
    failures = sum(failed for failed, _ in results)
    print(f'tidy-driver: {len(entries)} translation units, {failures} failures')
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
