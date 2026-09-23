#!/usr/bin/env python3
"""Align declaration and statement columns while preserving protected C and C++ text."""
import argparse
import pathlib
import re

PROTECTED = re.compile(r'R"(?P<d>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=d)"'
                       r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
                       r'|^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*', re.S | re.M)
DECL = r'(.+?[\s*&])([A-Za-z_]\w*(?:\s*\[[^\]]*\])*)\s*(?:([={])(.*))?;'
FORBIDDEN = {'return', 'co_return', 'throw', 'delete', 'using', 'typedef', 'case', 'goto', 'else', 'break'}
FROZEN = 'sources/sdl-rdp-backend.so/sdl-rdp-backend.h'


def mask(text):
    return PROTECTED.sub(lambda m: re.sub(r'[^\n]', '@', m[0]), text)


def pieces(text):
    hidden, stack, start, result = mask(text), [], 0, []
    for i, char in enumerate(hidden):
        if char in '([{':
            stack.append(char)
        elif char == '<' and re.search(r'[\w:>]$', hidden[:i]) and re.match(r'[\w:]', hidden[i + 1:]):
            stack.append(char)
        elif char in ')]}>' and stack and stack[-1] == dict(zip(')]}>', '([{<'))[char]:
            stack.pop()
        elif char == ',' and not stack:
            result.append(text[start:i].strip())
            start = i + 1
    return result + [text[start:].strip()]


def parse(line, hidden):
    indent = line[:len(line) - len(line.lstrip())]
    body, code = line[len(indent):], hidden[len(indent):]
    comment = ''
    for m in PROTECTED.finditer(body):
        if m[0].startswith('//'):
            comment, body, code = m[0], body[:m.start()].rstrip(), code[:m.start()].rstrip()
            break
    def match(pattern):
        m = re.fullmatch(pattern, code)
        return [body[a:b].strip() if a >= 0 else '' for a, b in (m.span(i) for i in range(1, len(m.groups()) + 1))] if m else None
    if found := match(r'(case\s+(?:[^:]|::)+:)\s*(.+;)'):
        return indent, 'case', found, comment
    if found := match(r'([:,])\s*(\w+)\s*\{(.*)\}\s*(,?)'):
        prefix, name, value, tail = found
        return indent, 'ctor', [prefix, name, '{', value, '}', tail], comment
    if found := match(DECL):
        typ, name, opener, value = found
        type_code = mask(typ)
        if not re.search(r'(?<!:):(?!:)', typ) and len(pieces(typ)) == 1 and typ.split()[0] not in FORBIDDEN and re.fullmatch(r'[\w:\s<>,*&\[\]]+', type_code):
            if opener == '{' and not value.endswith('}'):
                return None
            return indent, 'decl', [re.sub(r'\s+', ' ', typ), name, opener, value[:-1].strip() if opener == '{' else value, '}' if opener == '{' else '', ';'], comment
    if found := match(r'([\w:*&][\w:.>\-\[\]()@ *&]*?)\s*(<<=|>>=|[+*/%&|^\-]?=)\s*(?![=])(.*);'):
        lhs = mask(found[0])
        balanced = all(lhs.count(a) == lhs.count(b) for a, b in [('(', ')'), ('[', ']')])
        if balanced and lhs.split()[0] not in FORBIDDEN | {'if', 'while', 'for', 'switch'} and not lhs.endswith(('>', '<')):
            return indent, 'assign', found, comment
    if found := match(r'(\w+)\s*(?:(=)\s*(.*?))?,'):
        return indent, 'enum', found, comment
    return None


def render(item, widths):
    indent, kind, fields, comment = item
    def pad(index):
        return fields[index].ljust(widths[index])
    if kind == 'decl':
        typ, name, opener, value, closer, tail = fields
        body = pad(0) + ' ' + (pad(1) + ' ' + opener + ' ' + (pad(3) + (' }' if widths[3] else '}') if closer else value) if opener else name) + tail
    elif kind == 'ctor':
        body = fields[0] + ' ' + pad(1) + ' { ' + pad(3) + (' }' if widths[3] else '}') + fields[5]
    elif kind == 'case':
        body = pad(0) + ' ' + fields[1]
    else:
        body = pad(0) + ' ' + pad(1) + ' ' + fields[2] if fields[1] else fields[0]
        body += ';' if kind == 'assign' else ','
    return indent + body


def align(text, report=None):
    lines, hidden = text.splitlines(), mask(text).splitlines()
    expanded = []
    for line, code in zip(lines, hidden):
        comment = next((m[0] for m in PROTECTED.finditer(line) if m[0].startswith('//')), '')
        body = line[:line.rfind(comment)].rstrip() if comment else line.rstrip()
        if code[:len(body)].endswith(';'):
            parts = pieces(body[:-1])
            item = parse(parts[0] + ';', mask(parts[0] + ';'))
            if len(parts) > 1 and item and item[1] == 'decl':
                indent = line[:len(line) - len(line.lstrip())]
                base = re.sub(r'(?:[*&]\s*(?:(?:const|volatile)\s*)?)+$', '', item[2][0]).rstrip()
                for n, part in enumerate([parts[0]] + [base + ' ' + p for p in parts[1:]]):
                    new = indent + part + ';' + ('  ' + comment if comment and n == len(parts) - 1 else '')
                    expanded.append((new, mask(new)))
                continue
        expanded.append((line, code))
    output, group, exceptions, groups = [], [], [], 0
    def flush():
        nonlocal groups
        if not group:
            return
        groups += 1
        active = [i for i, (_, item) in enumerate(group) if item]
        while active:
            widths = [max(len(group[i][1][2][j]) for i in active) for j in range(len(group[active[0]][1][2]))]
            proposed = {i: render(group[i][1], widths) for i in active}
            comment_column = max(len(v) for v in proposed.values()) + 2
            proposed = {i: v.ljust(comment_column) + group[i][1][3] if group[i][1][3] else v for i, v in proposed.items()}
            long = [i for i, v in proposed.items() if len(v) > 120]
            if not long:
                break
            for i in [max(long, key=lambda i: len(render(group[i][1], list(map(len, group[i][1][2])))))]:
                exceptions.append((len(output) + i + 1, group[i][0]))
                active.remove(i)
        output.extend(proposed[i] if i in active else line for i, (line, _) in enumerate(group))
        group.clear()
    for line, code in expanded:
        item = parse(line, code)
        comment_only = line.strip() and not code.replace('@', '').strip() and line.lstrip().startswith(('//', '/*', '*'))
        if comment_only and group:
            group.append((line, None))
            continue
        first = next((entry for _, entry in group if entry), None)
        if not item or first and item[:2] != first[:2]:
            flush()
        if item:
            group.append((line, item))
        else:
            output.append(line)
    flush()
    if report is not None:
        report.update(groups=groups, exceptions=exceptions)
    return '\n'.join(output) + ('\n' if text.endswith('\n') else '')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--report', action='store_true')
    parser.add_argument('paths', nargs='*', type=pathlib.Path, default=[pathlib.Path('sources')])
    args = parser.parse_args()
    paths = sorted({p for root in args.paths for p in (root.rglob('*') if root.is_dir() else [root])})
    changed = False
    for path in paths:
        if path.suffix not in {'.c', '.h', '.cpp', '.hpp'} or path.as_posix().endswith(FROZEN):
            continue
        original, stats = path.read_text(), {}
        result = align(original, stats)
        if args.report:
            print(f'{path}: {stats["groups"]} groups; {len(stats["exceptions"])} exceptions')
            for number, line in stats['exceptions']:
                print(f'{path}:{number}: {line}')
        if result != original:
            changed = True
            if args.check:
                print(path)
            else:
                path.write_text(result)
    return int(args.check and changed)


if __name__ == '__main__':
    raise SystemExit(main())
