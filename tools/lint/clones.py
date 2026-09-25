#!/usr/bin/env python3
"""Fail on duplicated C and C++ code under sources/, headers included."""
import pathlib
import shutil
import subprocess
import sys

ROOT     = pathlib.Path(__file__).resolve().parents[2]
JSCPD    = 'jscpd@4.0.5'
INCLUDES = r'#include\s+[<"][^>"]*[>"]'
IMPORTS  = r'using\s+[\w:]+(?:operator""\w+)?\s*;'


def command():
    return ['npx', '--yes', JSCPD, '--min-tokens', '40', '--min-lines', '5', '--format', 'cpp',
            '--formats-exts', 'cpp:cpp,hpp,c,h', '--ignore-pattern', f'{INCLUDES},{IMPORTS}', '--noSymlinks',
            '--exitCode', '1', 'sources']


def main():
    if shutil.which('npx') is None:
        print('clones: npx is not installed', file=sys.stderr)
        return 1
    return subprocess.run(command(), cwd=ROOT).returncode


if __name__ == '__main__':
    sys.exit(main())
