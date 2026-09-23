#!/usr/bin/env python3
"""Align declaration and statement columns while preserving protected C and C++ text."""
import argparse
import pathlib
import re
from typing import NamedTuple

PROTECTED    = re.compile(r'R"(?P<d>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=d)"'
                         r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
                         r'|^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*', re.S | re.M)
DECL         = r'(.+?[\s*&])([A-Za-z_]\w*(?:\s*\[[^\]]*\])*)\s*(?:([={])(.*))?;'
FORBIDDEN    = {'return', 'co_return', 'throw', 'delete', 'using', 'typedef', 'case', 'goto', 'else', 'break'}
FROZEN       = 'sources/sdl-rdp-backend.so/sdl-rdp-backend.h'
COLUMN_LIMIT = 120
COMMENT_GAP  = 2
CLOSER_OF    = dict(zip(')]}>', '([{<'))


class Item(NamedTuple):
    indent: str
    kind: str
    fields: list[str]
    comment: str


class Entry(NamedTuple):
    line: str
    item: Item | None


def mask(text):
    return PROTECTED.sub(lambda m: re.sub(r'[^\n]', '@', m[0]), text)


def split_top_level_commas(text):
    hidden, stack, start, result = mask(text), [], 0, []
    for i, char in enumerate(hidden):
        if char in '([{':
            stack.append(char)
        elif char == '<' and re.search(r'[\w:>]$', hidden[:i]) and re.match(r'[\w:]', hidden[i + 1:]):
            stack.append(char)
        elif char in ')]}>' and stack and stack[-1] == CLOSER_OF[char]:
            stack.pop()
        elif char == ',' and not stack:
            result.append(text[start:i].strip())
            start = i + 1
    return result + [text[start:].strip()]


def trailing_comment(body):
    return next((match[0] for match in PROTECTED.finditer(body) if match[0].startswith('//')), '')


def match_fields(pattern, body, code):
    match = re.fullmatch(pattern, code)
    if match is None:
        return None
    spans = (match.span(i) for i in range(1, len(match.groups()) + 1))
    return [body[start:end].strip() if start >= 0 else '' for start, end in spans]


def parse_case(body, code):
    if fields := match_fields(r'(case\s+(?:[^:]|::)+:)\s*(.+;)', body, code):
        return 'case', fields
    return None


def parse_initialiser(body, code):
    if fields := match_fields(r'([:,])\s*(\w+)\s*\{(.*)\}\s*(,?)', body, code):
        prefix, name, value, tail = fields
        return 'ctor', [prefix, name, '{', value, '}', tail]
    return None


def is_declaration_type(typ):
    return bool(not re.search(r'(?<!:):(?!:)', typ) and len(split_top_level_commas(typ)) == 1
                and typ.split()[0] not in FORBIDDEN and re.fullmatch(r'[\w:\s<>,*&\[\]]+', mask(typ)))


def parse_declaration(body, code):
    fields = match_fields(DECL, body, code)
    if not fields or not is_declaration_type(fields[0]):
        return None
    typ, name, opener, value = fields
    closer = ''
    if opener == '{':
        if not value.endswith('}'):
            return None
        value, closer = value[:-1].strip(), '}'
    return 'decl', [re.sub(r'\s+', ' ', typ), name, opener, value, closer, ';']


def parse_assignment(body, code):
    pattern = r'([\w:*&][\w:.>\-\[\]()@ *&]*?)\s*(<<=|>>=|[+*/%&|^\-]?=)\s*(?![=])(.*);'
    if fields := match_fields(pattern, body, code):
        lhs = mask(fields[0])
        balanced = all(lhs.count(a) == lhs.count(b) for a, b in [('(', ')'), ('[', ']')])
        forbidden = FORBIDDEN | {'if', 'while', 'for', 'switch'}
        if balanced and lhs.split()[0] not in forbidden and not lhs.endswith(('>', '<')):
            return 'assign', fields
    return None


def parse_enumerator(body, code):
    if fields := match_fields(r'(\w+)\s*(?:(=)\s*(.*?))?,', body, code):
        return 'enum', fields
    return None


def parse(line, hidden):
    indent = line[:len(line) - len(line.lstrip())]
    body, code = line[len(indent):], hidden[len(indent):]
    comment = trailing_comment(body)
    if comment:
        end = body.rfind(comment)
        body, code = body[:end].rstrip(), code[:end].rstrip()
    for parser in (parse_case, parse_initialiser, parse_declaration, parse_assignment, parse_enumerator):
        if item := parser(body, code):
            kind, fields = item
            return Item(indent, kind, fields, comment)
    return None


def render_braced_value(value, width):
    return value.ljust(width) + (' }' if width else '}')


def render_declaration(fields, widths):
    typ, name, opener, value, closer, tail = fields
    body = typ.ljust(widths[0]) + ' '
    if opener:
        body += name.ljust(widths[1]) + ' ' + opener + ' '
        body += render_braced_value(value, widths[3]) if closer else value
    else:
        body += name
    return body + tail


def render_initialiser(fields, widths):
    prefix, name, opener, value, closer, tail = fields
    return prefix + ' ' + name.ljust(widths[1]) + ' { ' + render_braced_value(value, widths[3]) + tail


def render_case(fields, widths):
    return fields[0].ljust(widths[0]) + ' ' + fields[1]


def render_assignment(fields, widths):
    return fields[0].ljust(widths[0]) + ' ' + fields[1].ljust(widths[1]) + ' ' + fields[2] + ';'


def render_enumerator(fields, widths):
    if fields[1]:
        return fields[0].ljust(widths[0]) + ' ' + fields[1].ljust(widths[1]) + ' ' + fields[2] + ','
    return fields[0] + ','


RENDERERS    = {
    'decl': render_declaration,
    'ctor': render_initialiser,
    'case': render_case,
    'assign': render_assignment,
    'enum': render_enumerator,
}


def render(item, widths):
    return item.indent + RENDERERS[item.kind](item.fields, widths)


def expand_declarators(line, parts, typ, comment):
    indent = line[:len(line) - len(line.lstrip())]
    base = re.sub(r'(?:[*&]\s*(?:(?:const|volatile)\s*)?)+$', '', typ).rstrip()
    declarations = [indent + part + ';' for part in [parts[0]] + [base + ' ' + p for p in parts[1:]]]
    if comment:
        declarations[-1] += ' ' * COMMENT_GAP + comment
    return [(declaration, mask(declaration)) for declaration in declarations]


def split_declarators(lines):
    expanded = []
    for line, code in lines:
        comment = trailing_comment(line)
        body = line[:line.rfind(comment)].rstrip() if comment else line.rstrip()
        if code[:len(body)].endswith(';'):
            parts = split_top_level_commas(body[:-1])
            item = parse(parts[0] + ';', mask(parts[0] + ';'))
            if len(parts) > 1 and item and item.kind == 'decl':
                expanded.extend(expand_declarators(line, parts, item.fields[0], comment))
                continue
        expanded.append((line, code))
    return expanded


def is_comment_only(line, hidden):
    return bool(line.strip() and not hidden.replace('@', '').strip() and line.lstrip().startswith(('//', '/*', '*')))


class Group:
    def __init__(self):
        self.entries = []
        self.active = []
        self.key = None

    def add(self, line, item):
        if item:
            self.key = (item.indent, item.kind)
            self.active.append(len(self.entries))
        self.entries.append(Entry(line, item))

    def widths(self):
        fields = [self.entries[index].item.fields for index in self.active]
        return [max(map(len, column)) for column in zip(*fields)]

    def render(self):
        widths = self.widths()
        proposed = {index: render(self.entries[index].item, widths) for index in self.active}
        comment_column = max(map(len, proposed.values()), default=0) + COMMENT_GAP
        for index, body in proposed.items():
            comment = self.entries[index].item.comment
            if comment:
                proposed[index] = body.ljust(comment_column) + comment
        return proposed

    def unaligned_width(self, index):
        item = self.entries[index].item
        return len(render(item, list(map(len, item.fields))))

    def exclude_over_limit(self, offset, exceptions):
        proposed = self.render()
        while over_limit := [index for index, line in proposed.items() if len(line) > COLUMN_LIMIT]:
            index = max(over_limit, key=self.unaligned_width)
            exceptions.append((offset + index + 1, self.entries[index].line))
            self.active.remove(index)
            proposed = self.render()
        return [proposed.get(index, entry.line) for index, entry in enumerate(self.entries)]


def align(text, report=None):
    output, group, exceptions, groups = [], Group(), [], 0
    lines = zip(text.splitlines(), mask(text).splitlines())
    for line, code in split_declarators(lines):
        item = parse(line, code)
        if is_comment_only(line, code) and group.entries:
            group.add(line, None)
            continue
        if group.entries and (not item or (item.indent, item.kind) != group.key):
            output.extend(group.exclude_over_limit(len(output), exceptions))
            groups += 1
            group = Group()
        if item:
            group.add(line, item)
        else:
            output.append(line)
    if group.entries:
        output.extend(group.exclude_over_limit(len(output), exceptions))
        groups += 1
    if report is not None:
        report.update(groups=groups, exceptions=exceptions)
    return '\n'.join(output) + ('\n' if text.endswith('\n') else '')


def source_files(paths):
    candidates = {path for root in paths for path in (root.rglob('*') if root.is_dir() else [root])}
    return sorted(path for path in candidates
                  if path.suffix in {'.c', '.h', '.cpp', '.hpp'} and not path.as_posix().endswith(FROZEN))


def report(path, stats):
    print(f'{path}: {stats["groups"]} groups; {len(stats["exceptions"])} exceptions')
    for number, line in stats['exceptions']:
        print(f'{path}:{number}: {line}')


def check_python_columns():
    valid = True
    for path in (pathlib.Path(__file__), pathlib.Path(__file__).with_name('test_align_columns.py')):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if len(line) > COLUMN_LIMIT:
                print(f'{path}:{number}: {len(line)} columns > {COLUMN_LIMIT}')
                valid = False
    return valid


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--report', action='store_true')
    parser.add_argument('paths', nargs='*', type=pathlib.Path, default=[pathlib.Path('sources')])
    return parser.parse_args()


def main():
    args = arguments()
    failed = args.check and not check_python_columns()
    for path in source_files(args.paths):
        original, stats = path.read_text(), {}
        result = align(original, stats)
        if args.report:
            report(path, stats)
        if result != original:
            if args.check:
                failed = True
                print(path)
            else:
                path.write_text(result)
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
