#!/usr/bin/env python3
"""Refuse CMake that is not buildutil's declarations in their place unless a ticket exempts the call."""
import itertools
import pathlib
import re
import subprocess
import sys
import tomllib
from typing import NamedTuple

ROOT             = pathlib.Path(__file__).resolve().parents[2]
SCAFFOLD         = ('cmake_minimum_required(VERSION 3.25)', 'project({name} CXX)', 'include(buildutil)',
                    'add_subdirectory(sources)')
SOURCES_COMMANDS = frozenset({'Require', 'Scan_subdirectories'})
MODULE_COMMANDS  = frozenset({'Init_submodule', 'Link_dependencies', 'Add_generated_source',
                              'Resolve_generated_source'})
TOKEN            = re.compile(r'#\[(?P<level>=*)\[.*?\](?P=level)\]|#[^\n]*|"(?:\\.|[^"\\])*"'
                              r'|\[(?P<bracket>=*)\[.*?\](?P=bracket)\]|(?P<name>[A-Za-z_]\w*)\s*\(|[()]', re.S)
HEADING          = re.compile(r'#\s*buildutil\b')
TICKET           = re.compile(r'#\s*buildutil\s+#(?P<ticket>\d+):\s*\S')


class Call(NamedTuple):
    name:  str
    first: int
    last:  int
    text:  str


class Verdict(NamedTuple):
    path:    pathlib.Path
    line:    int
    name:    str
    problem: str
    ticket:  str | None


def line_of(text, offset):
    return text.count('\n', 0, offset) + 1


def starts_line(text, offset):
    return not text[text.rfind('\n', 0, offset) + 1:offset].strip()


def depth_change(token):
    lexeme = token.group()
    return 1 if token.group('name') or lexeme == '(' else -1 if lexeme == ')' else 0


def spelled(text, start, end):
    """The call as one line: comments dropped, whitespace collapsed."""
    body = re.sub(r'#[^\n]*', '', text[start:end])
    return re.sub(r'\s*([()])\s*', r'\1', ' '.join(body.split()))


def scan(text):
    """Top-level calls and the full-line comments of a CMake file."""
    calls, comments, depth, start = [], {}, 0, None
    for token in TOKEN.finditer(text):
        if depth == 0 and token.group().startswith('#') and starts_line(text, token.start()):
            comments[line_of(text, token.start())] = token.group()
        if depth == 0 and not token.group('name'):
            continue
        start = token if depth == 0 else start
        depth += depth_change(token)
        if depth == 0:
            calls.append(Call(start.group('name'), line_of(text, start.start()), line_of(text, token.end()),
                              spelled(text, start.start(), token.end())))
    return calls, comments


def exemption(call, comments):
    """The ticket on the line directly above: None when unheaded, '' when the heading names no ticket."""
    heading = comments.get(call.first - 1, '').strip()
    if not HEADING.match(heading):
        return None
    ticket = TICKET.match(heading)
    return ticket.group('ticket') if ticket else ''


def project_name(root):
    return tomllib.loads((root / 'buildutil.toml').read_text())['project']['name']


def scaffold_problems(root, calls):
    """Each root call that is not buildutil's scaffolded statement in its position."""
    expected = [statement.format(name=project_name(root)) for statement in SCAFFOLD]
    for call, statement in itertools.zip_longest(calls, expected):
        if call is None:
            yield Call(statement.partition('(')[0], 0, 0, ''), f'is missing; the scaffold has `{statement}`'
        elif statement is None:
            yield call, 'is not a buildutil declaration'
        elif call.text != statement:
            yield call, f'differs from the scaffolded `{statement}`'


def vocabulary(root, path):
    relative = path.relative_to(root)
    if relative == pathlib.Path('sources/CMakeLists.txt'):
        return SOURCES_COMMANDS
    if relative.parts[0] == 'sources' and relative.name == 'CMakeLists.txt':
        return MODULE_COMMANDS
    return frozenset()


def problems(root, path, calls):
    if path.relative_to(root) == pathlib.Path('CMakeLists.txt'):
        return list(scaffold_problems(root, calls))
    allowed = vocabulary(root, path)
    return [(call, 'is not a buildutil declaration here') for call in calls if call.name not in allowed]


def verdicts(root, path):
    """Every misplaced or foreign call, with the ticket that exempts it if any."""
    calls, comments = scan(path.read_text())
    return [Verdict(path.relative_to(root), call.first, call.name, problem, exemption(call, comments))
            for call, problem in problems(root, path, calls)]


def tracked(root):
    listing = subprocess.run(['git', 'ls-files', '--', 'CMakeLists.txt', '*/CMakeLists.txt', '*.cmake'],
                             cwd=root, capture_output=True, text=True, check=True).stdout.split()
    return [root / name for name in listing
            if not any(part.startswith('_') for part in pathlib.Path(name).parts)
            and not name.startswith('test_package/')]


def describe(verdict):
    location = f'{verdict.path}:{verdict.line}: {verdict.name}'
    if verdict.ticket is None:
        return f'{location} {verdict.problem}'
    if not verdict.ticket:
        return f'{location} is exempted without a ticket number'
    return f'{location} exempted by buildutil #{verdict.ticket}'


def lint(root):
    """Print every exemption and violation; true when the tree is clean."""
    found = [verdict for path in tracked(root) for verdict in verdicts(root, path)]
    for verdict in found:
        print(describe(verdict))
    tickets = sorted({verdict.ticket for verdict in found if verdict.ticket}, key=int)
    exempted = sum(bool(verdict.ticket) for verdict in found)
    print(f'{exempted} exempted lines under {len(tickets)} tickets: ' + ' '.join(f'#{t}' for t in tickets))
    return all(verdict.ticket for verdict in found)


def main():
    return int(not lint(ROOT))


if __name__ == '__main__':
    sys.exit(main())
