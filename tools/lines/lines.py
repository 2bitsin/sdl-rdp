"""Code, comment and blank lines of the tracked tree through cloc, by language, component and file."""
import argparse
import collections
import json
import os
import shutil
import subprocess
import sys

SECTIONS = ('language', 'component', 'file')


def tracked(root):
    return subprocess.run(['git', '-C', root, 'ls-files', '-z'], capture_output=True, text=True, check=True).stdout


def counted(root):
    if not shutil.which('cloc'):
        raise RuntimeError('cloc is not installed (the build image adds it)')
    result = subprocess.run(['cloc', '--list-file=-', '--by-file', '--json', '--quiet'], cwd=root,
                            input=tracked(root).replace('\0', '\n'), capture_output=True, text=True, check=True)
    table = json.loads(result.stdout or '{}')
    return {path: entry for path, entry in table.items() if path not in ('header', 'SUM')}


def component(path):
    parts = path.split('/')
    if parts[0] == 'sources' and parts[1] == 'sdl-rdp':
        return 'SDL3 driver' if parts[2] == 'SDL3' else parts[2] if len(parts) > 3 else 'sources (root)'
    if parts[0] == 'sources':
        return parts[1] if len(parts) > 2 else 'sources (root)'
    if parts[0] == 'tools':
        return f'tools/{parts[1]}' if len(parts) > 2 else 'tools'
    return parts[0] if len(parts) > 1 else '(root)'


def grouped(files, key):
    totals = collections.defaultdict(lambda: [0, 0, 0, 0])
    for path, entry in files.items():
        total = totals[key(path, entry)]
        total[0] += 1
        total[1] += entry['code']
        total[2] += entry['comment']
        total[3] += entry['blank']
    return sorted(totals.items(), key=lambda item: -item[1][1])


def print_group(title, groups, all_code, out):
    print(f'== {title} ==', file=out)
    print(f"{'':30} {'files':>5} {'code':>7} {'comment':>8} {'blank':>6} {'share':>6}", file=out)
    for name, (files, code, comment, blank) in groups:
        print(f'{name:30} {files:5} {code:7} {comment:8} {blank:6} {100 * code / all_code:5.1f}%', file=out)
    print(file=out)


def print_files(files, top, out):
    print('== By file (code, comment, blank) ==', file=out)
    for path, entry in sorted(files.items(), key=lambda item: -item[1]['code'])[:top]:
        print(f"{entry['code']:6} {entry['comment']:5} {entry['blank']:5}  {path}", file=out)


def report(root, sections, top, out=sys.stdout):
    files    = counted(root)
    all_code = sum(entry['code'] for entry in files.values())
    revision = subprocess.run(['git', '-C', root, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True)
    print(f'{os.path.basename(os.path.abspath(root))} {revision.stdout.strip()}: {len(files)} files, {all_code} code, '
          f"{sum(entry['comment'] for entry in files.values())} comment, "
          f"{sum(entry['blank'] for entry in files.values())} blank lines\n", file=out)
    if 'language' in sections:
        print_group('By language', grouped(files, lambda path, entry: entry['language']), all_code, out)
    if 'component' in sections:
        print_group('By component', grouped(files, lambda path, entry: component(path)), all_code, out)
    if 'file' in sections:
        print_files(files, top, out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    parser.add_argument('--by', action='append', choices=SECTIONS, help='a section to print; default all three')
    parser.add_argument('--top', type=int, default=None, help='largest files to list; default all')
    args = parser.parse_args(argv)
    report(args.root, args.by or SECTIONS, args.top)
    return 0


if __name__ == '__main__':
    sys.exit(main())
