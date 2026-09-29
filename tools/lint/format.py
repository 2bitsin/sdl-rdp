#!/usr/bin/env python3
"""Lay out C++ sources with clang-format 20, then set their columns; --check reports the diff instead."""
import argparse
import concurrent.futures
import difflib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import columns
import fan

ROOT    = pathlib.Path(__file__).resolve().parents[2]
VERSION = re.compile(r'version 20\.')
BATCH   = 16


def clang_format():
    """The first clang-format 20 found, or None."""
    names = (os.environ.get('CLANG_FORMAT'), shutil.which('clang-format-20'), shutil.which('clang-format'))
    versions = ((name, subprocess.run([name, '--version'], capture_output=True, text=True).stdout)
                for name in names if name)
    return next((name for name, version in versions if VERSION.search(version)), None)


def lay_out(executable, tree):
    paths = columns.source_files([tree])
    batches = [paths[start:start + BATCH] for start in range(0, len(paths), BATCH)]
    command = [executable, '-i', f'--style=file:{ROOT / ".clang-format"}']
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        list(pool.map(lambda batch: subprocess.run([*command, *batch], check=True), batches))


def align(tree):
    paths = columns.source_files([tree])
    for path, (original, result) in zip(paths, fan.out(columns.aligned_file, paths)):
        columns.write_if_changed(path, original, result)


def format_tree(executable, tree):
    lay_out(executable, tree)
    align(tree)


def file_diff(original, formatted):
    return difflib.unified_diff(original.read_text().splitlines(keepends=True),
                                formatted.read_text().splitlines(keepends=True), str(original), str(formatted))


def report_differences(tree, copy):
    diffs = [''.join(file_diff(path, copy / path.relative_to(tree))) for path in columns.source_files([tree])]
    sys.stdout.writelines(diff for diff in diffs if diff)
    return any(diffs)


def check(executable, tree):
    with tempfile.TemporaryDirectory() as scratch:
        copy = pathlib.Path(scratch) / tree.name
        shutil.copytree(tree, copy, symlinks=True)
        format_tree(executable, copy)
        return report_differences(tree, copy)


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('tree', nargs='?', type=pathlib.Path, default=ROOT / 'sources')
    return parser.parse_args(argv)


def main(argv=None):
    args = arguments(argv)
    executable = clang_format()
    if executable is None:
        print('format: clang-format 20 is not installed', file=sys.stderr)
        return 1
    if args.check:
        return int(check(executable, args.tree.resolve()))
    format_tree(executable, args.tree)
    return 0


if __name__ == '__main__':
    sys.exit(main())
