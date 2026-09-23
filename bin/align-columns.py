#!/usr/bin/env python3
"""Align consecutive, single-declarator columns without padding punctuation."""
import argparse
import pathlib
import re

DECLARATION = re.compile(r'^( +)([\w:][\w:< >*&]*?) +([A-Za-z_]\w*)(?: *(=) *(.*)| *(\{.*\}))?;$')
LITERALS = re.compile(r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
                      r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
EXCLUDED = {'return', 'co_return', 'throw', 'delete', 'using', 'typedef', 'case'}


def columns(line):
    match = DECLARATION.fullmatch(line)
    if not match or ',' in line or match[2].split()[0] in EXCLUDED:
        return None
    indent, kind, name, equals, value, braces = match.groups()
    kind = re.sub(r' +', ' ', kind)
    shape = '=' if equals else '{}' if braces else ';'
    return indent, kind, name, shape, value if equals else braces or ''


def clean_padding(text):
    parts = []
    start = 0
    for match in LITERALS.finditer(text):
        code = text[start:match.start()]
        parts.append(re.sub(r' +;', ';', re.sub(r'\{ +\}', '{}', code)))
        parts.append(match[0])
        start = match.end()
    parts.append(re.sub(r' +;', ';', re.sub(r'\{ +\}', '{}', text[start:])))
    return ''.join(parts)


def align(text):
    lines = clean_padding(text).splitlines()
    cursor = 0
    while cursor < len(lines):
        first = columns(lines[cursor])
        if first is None:
            cursor += 1
            continue
        end = cursor + 1
        group = [first]
        while end < len(lines):
            item = columns(lines[end])
            if item is None or (item[0], item[3]) != (first[0], first[3]):
                break
            group.append(item)
            end += 1
        width = max(len(item[1]) for item in group)
        names = max(len(item[2]) for item in group)
        proposed = []
        for indent, kind, name, shape, value in group:
            declaration = indent + kind.ljust(width) + ' ' + name
            if shape == '=':
                declaration += ' ' * (names - len(name)) + ' = ' + value
            elif shape == '{}':
                declaration += ' ' * (names - len(name)) + value
            proposed.append(declaration + ';')
        if all(len(line) <= 120 for line in proposed):
            lines[cursor:end] = proposed
        cursor = end
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=pathlib.Path)
    args = parser.parse_args()
    paths = args.paths or sorted(pathlib.Path('sources').rglob('*'))
    for path in paths:
        if path.suffix not in {'.c', '.h', '.cpp', '.hpp'} or path.name == 'sdl-rdp-backend.h':
            continue
        path.write_text(align(path.read_text()))


if __name__ == '__main__':
    main()
