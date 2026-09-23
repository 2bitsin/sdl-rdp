#!/usr/bin/env python3
"""Align declaration and statement columns while preserving protected C and C++ text."""
import argparse
import pathlib
import re
from typing import NamedTuple

PROTECTED    = re.compile(r'R"(?P<d>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=d)"'
                         r'|"(?:\\.|[^"\\])*"|(?<![0-9])\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/'
                         r'|^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*', re.S | re.M)
DECL         = r'(.+?[\s*&])([A-Za-z_]\w*(?:\s*\[[^\]]*\])*)\s*(?:([={]|:(?!:))(.*))?;'
FORBIDDEN    = {'return', 'co_return', 'throw', 'delete', 'using', 'typedef', 'case', 'goto',
                'else', 'break', 'class', 'struct', 'enum', 'if', 'while', 'for', 'switch'}
FROZEN       = 'sources/sdl-rdp-backend.so/sdl-rdp-backend.h'
COLUMN_LIMIT = 120
COMMENT_GAP  = 2
CLOSER_OF    = dict(zip(')]}>', '([{<'))


class Fields(NamedTuple):
    typ: str = ''
    name: str = ''
    opener: str = ''
    value: str = ''
    closer: str = ''
    tail: str = ''
    params: str = ''
    signature: str = ''


class Widths(NamedTuple):
    typ: int = 0
    name: int = 0
    opener: int = 0
    value: int = 0
    closer: int = 0
    tail: int = 0
    params: int = 0
    signature: int = 0


class Aligned(NamedTuple):
    text: str
    groups: int
    exceptions: list


class Item(NamedTuple):
    indent: str
    kind: str
    fields: Fields
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
    matches = [m[0] for m in PROTECTED.finditer(body) if m[0].startswith(('//', '/*'))]
    return matches[-1] if matches and body.rstrip().endswith(matches[-1]) else ''


def match_fields(pattern, body, code):
    match = re.fullmatch(pattern, code)
    if match is None:
        return None
    spans = (match.span(i) for i in range(1, len(match.groups()) + 1))
    return [body[start:end].strip() if start >= 0 else '' for start, end in spans]


def parse_case(body, code):
    if fields := match_fields(r'(case\s+(?:[^:]|::)+:)\s*(.+;)', body, code):
        label, statement = fields
        return 'case', Fields(typ=label, value=statement)
    return None


def closing_brace(code, start):
    depth = 0
    for index in range(start, len(code)):
        depth += (code[index] == '{') - (code[index] == '}')
        if depth == 0:
            return index
    return None


def parse_initialiser(body, code):
    match = re.match(r'([:,]?)\s*(\w+)\s*\{', code)
    if not match:
        return None
    end = closing_brace(code, match.end() - 1)
    if end is None or code[end + 1:].strip() not in ('', ','):
        return None
    prefix, name = match.groups()
    value, tail = body[match.end():end].strip(), body[end + 1:].strip()
    return 'ctor', Fields(prefix, name, '{', value, '}', tail)


def parse_function_pointer(body, code):
    pattern = r'(.+?)\s*(\(\*\w+\))\s*(\(.*\))\s*(?:(=)\s*(.*))?;'
    if fields := match_fields(pattern, body, code):
        typ, name, params, opener, value = fields
        return 'decl', Fields(typ, name, opener, value, '', ';', params)
    return None


def parse_function(body, code):
    pattern = r'(.*?)\b(operator\s*(?:\(\)|\[\]|[^ (][^(]*?)|[~\w]+)\s*(\(.*\))\s*(.*);'
    fields = match_fields(pattern, body, code)
    if not fields:
        return None
    typ, name, params, suffix = fields
    if not name.startswith('operator') and (not typ or not is_declaration_type(typ)):
        return None
    qualifiers, arrow, result = suffix.partition('->')
    signature = name + params + (' ' + qualifiers.strip() if qualifiers.strip() else '')
    return 'function', Fields(typ=typ, opener=arrow, value=result.strip(), tail=';', signature=signature)


def is_declaration_type(typ):
    code = re.sub(r'\b(?:alignas|decltype)\([^)]*\)', 'type', mask(typ))
    return bool(code and not code.endswith('::') and not re.search(r'(?<!:):(?!:)', code)
                and code.count('<') == code.count('>') and len(split_top_level_commas(code)) == 1
                and code.split()[0] not in FORBIDDEN
                and re.fullmatch(r'[\w:\s<>,*&\[\]]+', code))


def declaration_fields(typ, name, opener, value, code):
    if opener != '{':
        return Fields(typ, name, opener, value, tail=';')
    if closing_brace(code, code.index('{')) != len(code) - 2:
        return None
    return Fields(typ, name, opener, value[:-1].strip(), '}', ';')


def parse_declaration(body, code):
    fields = match_fields(DECL, body, code)
    if not fields:
        return None
    typ, name, opener, value = fields
    if not is_declaration_type(typ) or name == 'operator':
        return None
    parsed = declaration_fields(re.sub(r'\s+', ' ', typ), name, opener, value, code)
    return ('decl', parsed) if parsed else None


def parse_assignment(body, code):
    pattern = r'([\w:*&][\w:.>\-\[\]()@ *&]*?)\s*(<<=|>>=|[+*/%&|^\-]?=)\s*(?![=])(.*);'
    if fields := match_fields(pattern, body, code):
        target, operator, value = fields
        lhs = mask(target)
        balanced = all(lhs.count(a) == lhs.count(b) for a, b in [('(', ')'), ('[', ']')])
        forbidden = FORBIDDEN | {'if', 'while', 'for', 'switch'}
        if balanced and lhs.split()[0] not in forbidden and not lhs.endswith(('>', '<')):
            return 'assign', Fields(typ=target, opener=operator, value=value)
    return None


def parse_enumerator(body, code):
    if fields := match_fields(r'(\w+)\s*(?:(=)(?!=)\s*([^;]+?))?(,?)', body, code):
        name, opener, value, tail = fields
        if opener and not value:
            return None
        return 'enum', Fields(typ=name, opener=opener, value=value, tail=tail)
    return None


PARSERS = (parse_case, parse_initialiser, parse_function_pointer, parse_function,
           parse_declaration, parse_assignment, parse_enumerator)


def parse(line, hidden):
    indent = line[:len(line) - len(line.lstrip())]
    body, code = line[len(indent):], hidden[len(indent):]
    comment = trailing_comment(body)
    if comment:
        end = body.rfind(comment)
        body, code = body[:end].rstrip(), code[:end].rstrip()
    for parser in PARSERS:
        if item := parser(body, code):
            kind, fields = item
            return Item(indent, kind, fields, comment)
    return None


def render_braced_value(value, width):
    return value.ljust(width) + (' }' if width else '}')


def render_declaration(fields, widths):
    body = fields.typ.ljust(widths.typ) + ' '
    name = fields.name
    if fields.params:
        name = name.ljust(widths.name) + fields.params
    if fields.opener:
        gap = '' if fields.opener == '{' else ' '
        name_width = widths.name + (widths.params if fields.params else 0)
        body += name.ljust(name_width) + gap + fields.opener + ' '
        body += render_braced_value(fields.value, widths.value) if fields.closer else fields.value
    else:
        body += name
    return body + fields.tail


def render_initialiser(fields, widths):
    prefix = fields.typ + ' ' if fields.typ else ''
    name = prefix + fields.name.ljust(widths.name)
    return name + '{ ' + render_braced_value(fields.value, widths.value) + fields.tail


def render_function(fields, widths):
    prefix = fields.typ.ljust(widths.typ) + ' ' if widths.typ else ''
    if fields.opener:
        return prefix + fields.signature.ljust(widths.signature) + ' -> ' + fields.value + fields.tail
    return prefix + fields.signature + fields.tail


def render_case(fields, widths):
    return fields.typ.ljust(widths.typ) + ' ' + fields.value


def render_assignment(fields, widths):
    return fields.typ.ljust(widths.typ) + ' ' + fields.opener.ljust(widths.opener) + ' ' + fields.value + ';'


def render_enumerator(fields, widths):
    if fields.opener:
        return fields.typ.ljust(widths.typ) + ' = ' + fields.value + fields.tail
    return fields.typ + fields.tail


RENDERERS = {
    'decl': render_declaration,
    'ctor': render_initialiser,
    'function': render_function,
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


def split_declarator(line, code):
    comment = trailing_comment(line)
    body = line[:line.rfind(comment)].rstrip() if comment else line.rstrip()
    if not code[:len(body)].endswith(';'):
        return [(line, code)]
    parts = split_top_level_commas(body[:-1])
    item = parse(parts[0] + ';', mask(parts[0] + ';'))
    if len(parts) > 1 and item and item.kind == 'decl':
        return expand_declarators(line, parts, item.fields.typ, comment)
    return [(line, code)]


def split_declarators(lines):
    return [part for line, code in lines for part in split_declarator(line, code)]


def measure(fields):
    widths = {name: max((len(getattr(f, name)) for f in fields), default=0) for name in Fields._fields}
    widths['value'] = max((len(f.value) for f in fields if f.closer), default=0)
    return Widths(**widths)


def is_comment_only(line, hidden):
    return bool(line.strip() and not hidden.replace('@', '').strip() and line.lstrip().startswith(('//', '/*', '*')))


def group_key(item):
    indent = item.indent
    if item.kind == 'ctor' and item.fields.typ:
        indent += '  '
    return indent, item.kind


class Group:
    def __init__(self):
        self.entries = []
        self.active = []
        self.key = None

    def add(self, line, item):
        if item:
            self.key = group_key(item)
            self.active.append(len(self.entries))
        self.entries.append(Entry(line, item))

    def widths(self):
        fields = [self.entries[index].item.fields for index in self.active]
        return measure(fields)

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
        return len(render(item, measure([item.fields])))

    def exclude_over_limit(self, offset, exceptions):
        proposed = self.render()
        while over_limit := [index for index, line in proposed.items() if len(line) > COLUMN_LIMIT]:
            index = max(over_limit, key=self.unaligned_width)
            exceptions.append((offset + index + 1, self.entries[index].line))
            self.active.remove(index)
            proposed = self.render()
        return [proposed.get(index, entry.line) for index, entry in enumerate(self.entries)]


def normalise_empty(text):
    hidden = mask(text)
    spans = list(re.finditer(r'\{[ \t]*\}', hidden))
    for match in reversed(spans):
        text = text[:match.start()] + '{ }' + text[match.end():]
    return text


def normalise_initialiser_part(part):
    code = mask(part)
    match = re.match(r'\w+\s*\{', code)
    if not match:
        return part
    end = closing_brace(code, match.end() - 1)
    if end is None:
        return part
    _, fields = parse_initialiser(part[:end + 1], code[:end + 1])
    return render_initialiser(fields, measure([fields])) + part[end + 1:]


def normalise_initialiser_list(line, code):
    match = re.search(r'(?<!:):(?!:)', code)
    if not match or (code[:match.start()].strip() and ')' not in code[:match.start()]):
        return line
    parts = split_top_level_commas(line[match.end():])
    if len(parts) < 2 or not all(parts):
        return line
    return line[:match.end()] + ' ' + ', '.join(map(normalise_initialiser_part, parts))


def grouped_lines(lines):
    group = Group()
    for line, code in lines:
        item = parse(line, code)
        continuation = is_comment_only(line, code) and group.entries
        if not continuation and group.entries and (not item or group_key(item) != group.key):
            yield group
            group = Group()
        group.add(line, item)
        if not item and not continuation:
            yield group
            group = Group()
    if group.entries:
        yield group


def align(text):
    normalised = normalise_empty(text)
    original = zip(normalised.splitlines(), mask(normalised).splitlines())
    lines = [normalise_initialiser_list(line, code) for line, code in original]
    pairs = zip(lines, mask('\n'.join(lines)).splitlines())
    output, exceptions, count = [], [], 0
    for group in grouped_lines(split_declarators(pairs)):
        count += bool(group.active)
        output.extend(group.exclude_over_limit(len(output), exceptions))
    result = '\n'.join(output) + ('\n' if text.endswith('\n') else '')
    return Aligned(result, count, exceptions)


def source_files(paths):
    candidates = {path for root in paths for path in (root.rglob('*') if root.is_dir() else [root])}
    return sorted(path for path in candidates
                  if path.suffix in {'.c', '.h', '.cpp', '.hpp'} and not path.as_posix().endswith(FROZEN))


def print_statistics(path, stats):
    print(f'{path}: {stats.groups} groups; {len(stats.exceptions)} exceptions')
    for number, line in stats.exceptions:
        print(f'{path}:{number}: {line}')


def check_python_columns():
    paths = (pathlib.Path(__file__), pathlib.Path(__file__).with_name('test_align_columns.py'))
    violations = [(path, number, len(line)) for path in paths
                  for number, line in enumerate(path.read_text().splitlines(), 1) if len(line) > COLUMN_LIMIT]
    for path, number, width in violations:
        print(f'{path}:{number}: {width} columns > {COLUMN_LIMIT}')
    return not violations


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--self-check', action='store_true')
    parser.add_argument('--report', action='store_true')
    parser.add_argument('paths', nargs='*', type=pathlib.Path, default=[pathlib.Path('sources')])
    return parser.parse_args()


def update_file(path, result, check):
    if check:
        print(path)
    else:
        path.write_text(result.text)
    return check


def main():
    args = arguments()
    if args.self_check:
        return int(not check_python_columns())
    failed = False
    for path in source_files(args.paths):
        original = path.read_text()
        result = align(original)
        if args.report:
            print_statistics(path, result)
        if result.text != original:
            failed |= update_file(path, result, args.check)
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
