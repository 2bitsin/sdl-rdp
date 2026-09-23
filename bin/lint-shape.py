#!/usr/bin/env python3
"""Check source shape and reject stale entries in the structural debt ratchet."""
import argparse
import pathlib
import re
import sys

# Limits: Step 0 quality gate brief, 2026-09-23.
FILE_LINES = 400
CLASS_LINES = 150
DATA_MEMBERS = 10
MEMBER_FUNCTIONS = 15
COMMENT_PERCENT = 15
EXTENSIONS = frozenset(('.c', '.h', '.cpp', '.hpp'))
LEXEMES = re.compile(
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    r'|//[^\n]*|/\*.*?\*/|^[ \t]*\#[^\n]*(?:\\\n[^\n]*)*'
    r'|[A-Za-z_]\w*|\d+(?:\.\d+)?|::|&&|->|[^\s]', re.S | re.M)


def scan(source):
    tokens = []
    code_lines = set()
    comment_lines = set()
    line = 1
    previous = 0
    for match in LEXEMES.finditer(source):
        line += source[previous:match.start()].count('\n')
        value = match.group()
        lines = set(range(line, line + value.count('\n') + 1))
        if value.startswith(('//', '/*')):
            comment_lines.update(lines)
        else:
            code_lines.update(lines)
            if not value.lstrip().startswith('#'):
                tokens.append((value, line))
        line += value.count('\n')
        previous = match.end()
    nonblank = {index for index, text in enumerate(source.splitlines(), 1) if text.strip()}
    return tokens, (comment_lines - code_lines) & nonblank


def matching(tokens):
    stack = []
    pairs = {}
    closing = {')': '(', ']': '[', '}': '{'}
    for index, (value, _) in enumerate(tokens):
        if value in closing.values():
            stack.append((value, index))
        elif value in closing and stack and stack[-1][0] == closing[value]:
            _, start = stack.pop()
            pairs[start] = index
    return pairs


def classes(tokens, pairs):
    for index, (kind, line) in enumerate(tokens):
        if kind not in ('class', 'struct') or index + 1 >= len(tokens):
            continue
        if index and tokens[index - 1][0] == 'enum':
            continue
        name = tokens[index + 1][0]
        cursor = index + 2
        inherited = False
        while cursor < len(tokens):
            word = tokens[cursor][0]
            if word in ('{', ';') or (word in ('>', ',') and not inherited):
                break
            inherited |= word == ':'
            cursor += 1
        if cursor in pairs and tokens[cursor][0] == '{':
            yield kind, name, line, cursor, pairs[cursor]


def members(tokens, pairs, start, end):
    cursor = start + 1
    first = cursor
    while cursor < end:
        word = tokens[cursor][0]
        if word in ('public', 'protected', 'private') and tokens[cursor + 1][0] == ':':
            yield 'access', tokens[cursor:cursor + 1]
            cursor += 2
            first = cursor
            continue
        if word == ';':
            yield 'member', tokens[first:cursor]
            first = cursor + 1
        elif cursor in pairs:
            closing = pairs[cursor]
            prefix = [value for value, _ in tokens[first:cursor]]
            following = tokens[closing + 1][0] if closing + 1 < end else ''
            if word == '{' and function_name(prefix, '') is not None and following not in (',', '{'):
                yield 'member', tokens[first:closing + 1]
                first = closing + 1
            cursor = closing
        cursor += 1


def function_name(words, name):
    if not words or words[0] in ('using', 'typedef', 'enum', 'class', 'struct', 'friend', 'static_assert'):
        return None
    depth = 0
    for opening, word in enumerate(words):
        if word == 'operator' and depth == 0:
            return 'operator=' if words[opening + 1:opening + 2] == ['='] else 'operator'
        if word in ('=', '{') and depth == 0 and (not opening or words[opening - 1] != 'operator'):
            return None
        if word == '(' and depth == 0 and opening:
            candidate = words[opening - 1]
            if candidate not in ('decltype', 'alignas', 'sizeof'):
                if words[opening + 1] in ('*', '&'):
                    return None
                if candidate == name and opening > 1 and words[opening - 2] == '~':
                    return '~' + name
                return 'operator=' if candidate == '=' else candidate
        if word in ('(', '<', '['):
            depth += 1
        elif word in (')', '>', ']'):
            depth -= 1
    return None


def data_count(words):
    if not words or words[0] in ('using', 'typedef', 'enum', 'class', 'struct', 'friend', 'static_assert', 'template'):
        return 0
    depth = 0
    count = 1
    for word in words:
        if word in ('(', '[', '{', '<'):
            depth += 1
        elif word in (')', ']', '}', '>'):
            depth -= 1
        elif word == ',' and depth == 0:
            count += 1
    return count


def class_measurements(tokens, pairs, item, public_data=False, fixture=False):
    kind, name, line, start, end = item
    access = 'private' if kind == 'class' else 'public'
    highest = -1
    ranks = {'public': 0, 'protected': 1, 'private': 2}
    functions = []
    data = 0
    layout = 0
    exposed_data = 0
    section_data = dict.fromkeys(ranks, False)
    for category, member in members(tokens, pairs, start, end):
        words = [word for word, _ in member]
        if category == 'access':
            access = words[0]
            layout += ranks[access] < highest
            highest = max(highest, ranks[access])
        elif (function := function_name(words, name)) is not None:
            highest = max(highest, ranks[access])
            layout += function == name and any(value not in (name, '~' + name, 'operator=') for value in functions)
            functions.append(function)
            layout += section_data[access]
        elif (count := data_count(words)):
            highest = max(highest, ranks[access])
            data += count
            exposed_data += access != 'private' and not (fixture and access == 'protected')
            section_data[access] = True
    active = any(function != name for function in functions)
    if active and not public_data:
        layout += exposed_data
    size = tokens[end][1] - line + 1
    return name, line, size, data, len(functions), layout, active


def check_file(path, fixtures=frozenset()):
    source = path.read_text()
    tokens, comments = scan(source)
    nonblank = sum(bool(line.strip()) for line in source.splitlines())
    findings = []
    if nonblank > FILE_LINES:
        findings.append((1, 'file lines', nonblank, FILE_LINES))
    percent = 100 * len(comments) / nonblank if nonblank else 0
    if percent > COMMENT_PERCENT:
        findings.append((1, 'comment percent', percent, COMMENT_PERCENT))
    pairs = matching(tokens)
    for item in classes(tokens, pairs):
        public_data = path.as_posix() == 'sources/sdl-rdp-backend.so/_detail/state.hpp' and item[1] in ('Peer', 'State')
        name, line, size, data, functions, layout, active = class_measurements(tokens, pairs, item, public_data, item[1] in fixtures)
        if not active:
            continue
        for label, value, limit in (('lines', size, CLASS_LINES),
                                    ('data members', data, DATA_MEMBERS),
                                    ('member functions', functions, MEMBER_FUNCTIONS),
                                    ('layout', layout, 0)):
            if value > limit:
                findings.append((line, f'{name} {label}', value, limit))
    return [(f'{path}: {label} {value} > {limit}', line)
            for line, label, value, limit in findings]


def check_allow(findings, allow):
    allowed = set()
    if allow:
        allowed = {line.strip() for line in allow.read_text().splitlines() if line.strip()}
    current = {text for text, _ in findings}
    failed = False
    for text, line in findings:
        if text not in allowed:
            path, detail = text.split(': ', 1)
            print(f'{path}:{line}: {detail}')
            failed = True
    for entry in sorted(allowed - current):
        print(f'{allow}:1: stale allow entry: {entry}')
        failed = True
    return int(failed)


def fixture_classes(paths):
    bases = {}
    for path in paths:
        tokens, _ = scan(path.read_text())
        for _, name, line, start, _ in classes(tokens, matching(tokens)):
            prefix = []
            cursor = start - 1
            while cursor >= 0 and tokens[cursor][0] not in ('class', 'struct'):
                prefix.append(tokens[cursor][0])
                cursor -= 1
            if ':' in prefix:
                bases.setdefault(name, set()).update(prefix[:prefix.index(':')])
    fixtures = {'Test', 'TestWithParam'}
    while additional := {name for name, parents in bases.items() if parents & fixtures} - fixtures:
        fixtures.update(additional)
    return fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow', type=pathlib.Path)
    args = parser.parse_args()
    findings = []
    paths = [path for path in sorted(pathlib.Path('sources').rglob('*'))
             if path.suffix in EXTENSIONS and path.is_file()]
    fixtures = fixture_classes(paths)
    for path in paths:
        findings.extend(check_file(path, fixtures))
    return check_allow(findings, args.allow)


if __name__ == '__main__':
    sys.exit(main())
