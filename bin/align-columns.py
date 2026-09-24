#!/usr/bin/env python3
"""Align declaration and statement columns while preserving protected C and C++ text."""
import argparse
import pathlib
import re
from types import SimpleNamespace
from typing import NamedTuple

RAW_DELIMITER_LIMIT = 16  # C++ raw-string delimiters have at most 16 characters.
PROTECTED           = re.compile(r'R"(?P<d>[^ ()\\\t\r\n]{0,' + str(RAW_DELIMITER_LIMIT) + r'})\(.*?\)(?P=d)"'
                                 r'|"(?:\\.|[^"\\])*"|(?<![0-9])\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/'
                                 r'|^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*', re.S | re.M)
DECL                = r'(.+?[\s*&])([A-Za-z_]\w*(?:\s*\[[^\]]*\])*)\s*(?:([={]|:(?!:))(.*))?;'
FORBIDDEN           = {'return', 'co_return', 'throw', 'delete', 'using', 'typedef', 'case', 'goto',
                       'else', 'break', 'if', 'while', 'for', 'switch'}
FROZEN              = 'sources/sdl-rdp-backend.so/sdl-rdp-backend.h'
COLUMN_LIMIT        = 120
COMMENT_GAP         = 2
INDENT_WIDTH        = 2
CLOSER_OF           = dict(zip(')]}>', '([{<'))


class CaseFields(NamedTuple):
    label: str
    statement: str


class AssignmentFields(NamedTuple):
    target: str
    operator: str
    value: str


class EnumeratorFields(NamedTuple):
    name: str
    equals: str
    value: str
    tail: str


class InitialiserFields(NamedTuple):
    prefix: str
    name: str
    value: str
    tail: str


class DeclarationFields(NamedTuple):
    typ: str
    name: str
    opener: str
    value: str
    closer: str = ''
    tail: str = ';'
    params: str = ''


class FunctionFields(NamedTuple):
    typ: str
    signature: str
    arrow: str
    result: str
    specifier: str = ''
    tail: str = ';'


class TableFields(NamedTuple):
    cells: tuple[str, ...]
    tail: str


Fields = (CaseFields | AssignmentFields | EnumeratorFields | InitialiserFields
          | DeclarationFields | FunctionFields | TableFields)


class Overflow(NamedTuple):
    line_number: int
    text: str


class Aligned(NamedTuple):
    text: str
    groups: int
    exceptions: list[Overflow]


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


def opens_bracket(code, index):
    char = code[index]
    return char in '([{' or (char == '<' and re.search(r'[\w:>]$', code[:index])
                            and re.match(r'[\w:]', code[index + 1:]))


def update_brackets(stack, code, index):
    char = code[index]
    if opens_bracket(code, index):
        stack.append(char)
    elif char in CLOSER_OF and stack and stack[-1] == CLOSER_OF[char]:
        stack.pop()


def split_top_level_commas(text):
    hidden, stack, start, result = mask(text), [], 0, []
    for index, char in enumerate(hidden):
        update_brackets(stack, hidden, index)
        if char == ',' and not stack:
            result.append(text[start:index].strip())
            start = index + 1
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
        return 'case', CaseFields(label, statement)
    return None


def closing_delimiter(code, start, opener, closer):
    depth = 0
    for index in range(start, len(code)):
        depth += (code[index] == opener) - (code[index] == closer)
        if depth == 0:
            return index
    return None


def closing_brace(code, start):
    return closing_delimiter(code, start, '{', '}')


def parse_initialiser(body, code):
    match = re.match(r'([:,]?)\s*(\w+)\s*\{', code)
    if not match:
        return None
    end = closing_brace(code, match.end() - 1)
    if end is None or code[end + 1:].strip() not in ('', ',', '{ }'):
        return None
    prefix, name = match.groups()
    value, tail = body[match.end():end].strip(), body[end + 1:].strip()
    return 'ctor', InitialiserFields(prefix, name, value, ' ' + tail if tail.startswith('{') else tail)


def parse_function_pointer(body, code):
    pattern = r'(.+?)\s*(\(\*\w+\))\s*(\(.*\))\s*(?:(=)\s*(.*))?;'
    if fields := match_fields(pattern, body, code):
        typ, name, params, opener, value = fields
        return 'decl', DeclarationFields(typ, name, opener, value, params=params)
    return None


def function_suffix(suffix):
    arrow = suffix.find('->')
    start = arrow + 2 if arrow >= 0 else 0
    match = re.search(r'\b(?:override|final|noexcept)\b|(?<![=])=(?!=)', mask(suffix)[start:])
    if match:
        position = start + match.start()
        return suffix[:position].strip(), suffix[position:].strip()
    return suffix, ''


def function_parts(body, code):
    pattern = r'(.*?)(?<![\w:])(operator\s*(?:\(\)|\[\]|[^ (][^(]*?)|[~\w]+)(\s*\()'
    match = re.match(pattern, code)
    if not match:
        return None
    start = match.end() - 1
    end = closing_delimiter(code, start, '(', ')')
    if end is None:
        return None
    typ = body[:match.start(2)].strip()
    signature = body[match.start(2):end + 1]
    return typ, signature, body[end + 1:].strip()


def function_tail(suffix):
    if suffix.endswith(';'):
        return suffix[:-1], ';'
    match = re.search(r'(?<!:):(?!:)|\{', mask(suffix))
    if match:
        return suffix[:match.start()].strip(), ' ' + suffix[match.start():]
    return suffix, ''


def parse_function(body, code):
    parts = function_parts(body, code)
    if not parts:
        return None
    typ, signature, suffix = parts
    name = signature.split('(', 1)[0].strip()
    if name in FORBIDDEN or (typ and not is_declaration_type(typ)):
        return None
    suffix, tail = function_tail(suffix)
    suffix, specifier = function_suffix(suffix)
    qualifiers, arrow, result = suffix.partition('->')
    qualifier_pattern = r'(?:const|volatile|noexcept(?:\([^)]*\))?|[& ])+'
    if qualifiers and not re.fullmatch(qualifier_pattern, qualifiers.strip()):
        return None
    signature += ' ' + qualifiers.strip() if qualifiers.strip() else ''
    return 'function', FunctionFields(typ, signature, arrow, result.strip(), specifier, tail)


def is_declaration_type(typ):
    code = re.sub(r'\b(?:alignas|decltype)\([^)]*\)', 'type', mask(typ))
    code = re.sub(r'\([^()]*\)', '', code)
    return bool(code and not code.endswith('::') and not re.search(r'(?<!:):(?!:)', code)
                and code.count('<') == code.count('>') and len(split_top_level_commas(code)) == 1
                and code.split()[0] not in FORBIDDEN
                and re.fullmatch(r'[\w:\s<>,*&\[\]]+', code))


def declaration_fields(typ, name, opener, value, code):
    if opener != '{':
        return DeclarationFields(typ, name, opener, value)
    if closing_brace(code, code.index('{')) != len(code) - 2:
        return None
    return DeclarationFields(typ, name, opener, value[:-1].strip(), '}')


def parse_declaration(body, code):
    fields = match_fields(DECL, body, code)
    if not fields:
        return None
    typ, name, opener, value = fields
    if typ in ('class', 'struct') and opener == ':':
        return None
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
        if balanced and lhs.split()[0] not in FORBIDDEN and not lhs.endswith(('>', '<')):
            return 'assign', AssignmentFields(target, operator, value)
    return None


def parse_enumerator(body, code):
    if fields := match_fields(r'(\w+)\s*(?:(=)(?!=)\s*([^;]+?))?(,?)', body, code):
        name, opener, value, tail = fields
        if opener and not value:
            return None
        return 'enum', EnumeratorFields(name, opener, value, tail)
    return None


def parse_table(body, code):
    if not code.startswith('{'):
        return None
    end = closing_brace(code, 0)
    if end is None:
        return None
    cells = split_top_level_commas(body[1:end])
    if len(cells) < 2 or any('{' in mask(cell) for cell in cells):
        return None
    return 'table', TableFields(tuple(cells), body[end + 1:])


def parse_alias(body, code):
    if fields := match_fields(r'(using)\s+(\w+)\s*(=)\s*(.*);', body, code):
        return 'decl', DeclarationFields(*fields)
    return None


def parse_enum_class(body, code):
    if fields := match_fields(r'(enum(?:\s+class)?)\s+(\w+)\s*(\{.*\});', body, code):
        typ, name, value = fields
        return 'decl', DeclarationFields(typ, name, '{', value[1:-1].strip(), '}')
    return None


def parse_binding(body, code):
    if fields := match_fields(r'(auto(?:[ &]|const)*)\s*(\[[^]]+\])\s*(=)\s*(.*);', body, code):
        return 'decl', DeclarationFields(*fields)
    return None


def parse_designated(body, code):
    if fields := match_fields(r'(\.\w+)\s*(=)\s*(.*?)(,?)', body, code):
        return 'enum', EnumeratorFields(*fields)
    return None


PARSERS = (parse_table, parse_alias, parse_enum_class, parse_binding, parse_designated, parse_case,
           parse_initialiser, parse_function_pointer, parse_function,
           parse_declaration, parse_assignment, parse_enumerator)


def indentation(line):
    return line[:len(line) - len(line.lstrip())]


def is_function_declaration(fields, constructors):
    if fields.typ or constructors is None or fields.signature.startswith(('operator', '~')):
        return True
    name = fields.signature.split('(', 1)[0]
    return name in constructors or bool(fields.specifier) or name in fields.signature[len(name) + 1:]


def accepted_kind(item, constructors):
    kind, fields = item
    return kind != 'function' or is_function_declaration(fields, constructors)


def parse_body(body, code, constructors):
    for parser in PARSERS:
        item = parser(body, code)
        if item and accepted_kind(item, constructors):
            return item
    return None


def parse(line, hidden, constructors=None):
    indent = indentation(line)
    body, code = line[len(indent):], hidden[len(indent):]
    comment = trailing_comment(body)
    if comment:
        end = body.rfind(comment)
        body, code = body[:end].rstrip(), code[:end].rstrip()
    if item := parse_body(body, code, constructors):
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
        gap = widths.gap
        name_width = widths.name + (widths.params if fields.params else 0)
        body += name.ljust(name_width) + gap + fields.opener + ' '
        body += render_braced_value(fields.value, widths.value) if fields.closer else fields.value
    else:
        body += name
    return body.rstrip() + fields.tail


def render_initialiser(fields, widths):
    prefix = fields.prefix + ' ' if fields.prefix else ''
    name = prefix + fields.name.ljust(widths.name)
    return name + '{ ' + render_braced_value(fields.value, widths.value) + fields.tail


def render_function(fields, widths):
    prefix = fields.typ.ljust(widths.typ) + ' ' if widths.typ else ''
    signature = fields.signature
    if fields.arrow:
        signature = signature.ljust(widths.signature) + ' -> ' + fields.result
    if fields.specifier:
        width = widths.signature + (4 + widths.result if widths.arrow else 0)
        signature = signature.ljust(width) + ' ' + fields.specifier
    if fields.tail.startswith(' '):
        width = widths.signature + (4 + widths.result if widths.arrow else 0)
        signature = signature.ljust(width)
    return prefix + signature + fields.tail


def render_case(fields, widths):
    return fields.label.ljust(widths.label) + ' ' + fields.statement


def render_assignment(fields, widths):
    return fields.target.ljust(widths.target) + ' ' + fields.operator.ljust(widths.operator) + ' ' + fields.value + ';'


def render_enumerator(fields, widths):
    if fields.equals:
        return fields.name.ljust(widths.name) + ' = ' + fields.value + fields.tail
    return fields.name + fields.tail


def render_table(fields, widths):
    cells = [cell.ljust(width) for cell, width in zip(fields.cells, widths.cells)]
    return '{ ' + ', '.join(cells) + ' }' + fields.tail


RENDERERS = {
    'table': render_table,
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
    indent = indentation(line)
    base = re.sub(r'(?:[*&]\s*(?:(?:const|volatile)\s*)?)+$', '', typ).rstrip()
    declarations = [indent + part + ';' for part in [parts[0]] + [base + ' ' + p for p in parts[1:]]]
    if comment:
        declarations[-1] += ' ' + comment
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


def declaration_gap(fields):
    return ' ' if any(f.opener in ('=', ':') for f in fields) else ''


def measure_table(fields):
    columns = zip(*(field.cells for field in fields))
    return SimpleNamespace(cells=[max(map(len, column)) for column in columns])


def measure(fields):
    if not fields:
        return SimpleNamespace()
    if isinstance(fields[0], TableFields):
        return measure_table(fields)
    widths = {name: max(len(getattr(f, name)) for f in fields) for name in fields[0]._fields}
    if isinstance(fields[0], DeclarationFields):
        widths['value'] = max((len(f.value) for f in fields if f.closer), default=0)
        widths['gap'] = declaration_gap(fields)
    return SimpleNamespace(**widths)


def is_comment_only(line, hidden):
    return bool(line.strip() and not hidden.replace('@', '').strip() and line.lstrip().startswith(('//', '/*', '*')))


def group_key(item):
    indent = item.indent
    if item.kind == 'ctor' and item.fields.prefix:
        indent += ' ' * (len(item.fields.prefix) + 1)
    kind = ('table', len(item.fields.cells)) if item.kind == 'table' else item.kind
    return indent, kind


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
        excluded = {}
        while over_limit := [index for index, line in proposed.items() if len(line) > COLUMN_LIMIT]:
            index = max(over_limit, key=self.unaligned_width)
            excluded[index] = self.entries[index].line
            exceptions.append(Overflow(offset + index + 1, excluded[index]))
            self.active.remove(index)
            proposed = self.render()
        return [proposed.get(index, excluded.get(index, entry.line)) for index, entry in enumerate(self.entries)]


def replace_outside_protected(text, pattern, replacement):
    for match in reversed(list(re.finditer(pattern, mask(text)))):
        text = text[:match.start()] + replacement + text[match.end():]
    return text


def normalise_spacing(text):
    return replace_outside_protected(text, r'(?<=\S)[ \t]+', ' ')


def normalise_empty(text):
    return replace_outside_protected(text, r'\{[ \t]*\}', '{ }')


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
    match = re.search(r'(?<=\))\s*:(?!:)(?=\s*\w+\s*\{)', code)
    if not match:
        return line
    parts = split_top_level_commas(line[match.end():])
    if len(parts) < 2 or not all(parts):
        return line
    return line[:match.end()] + ' ' + ', '.join(map(normalise_initialiser_part, parts))


def continuation_end(lines, start):
    line, code = lines[start]
    for end in range(start + 1, len(lines)):
        following, hidden = lines[end]
        if not following.strip() or len(indentation(following)) <= len(indentation(line)):
            return None
        code += ' ' + hidden.strip()
        if code.count('(') == code.count(')'):
            return end
    return None


def declaration_head(item, line):
    fields = item.fields
    if item.kind == 'decl' and fields.params:
        params = line[line.index(')', line.index('(*')) + 1:].lstrip()
        return item._replace(fields=fields._replace(params=params, tail=''))
    if item.kind == 'function':
        signature = line.strip()[len(fields.typ):].lstrip()
        fields = fields._replace(signature=signature, arrow='', result='', specifier='', tail='')
        return item._replace(fields=fields)
    return None


def without_comment(line):
    comment = trailing_comment(line)
    return line[:line.rfind(comment)].rstrip() if comment else line


def multiline_declaration(lines, start, constructors):
    line, code = lines[start]
    if code.count('(') <= code.count(')') or code.rstrip().endswith(';'):
        return None
    end = continuation_end(lines, start)
    if end is None:
        return None
    combined = indentation(line) + ' '.join(without_comment(text).strip() for text, _ in lines[start:end + 1])
    item = parse(combined, mask(combined), constructors)
    head = declaration_head(item, without_comment(line)) if item else None
    if head:
        head = head._replace(comment=trailing_comment(line))
    return (head, end) if head else None


def equals_head(line, code, constructors):
    if code.rstrip().endswith(';'):
        return None
    parsed = parse_declaration(line.strip() + ';', code.strip() + ';')
    if parsed and parsed[1].opener == '=':
        return Item(indentation(line), 'decl', parsed[1]._replace(tail=''), '')
    return None


def constructor_tail_end(lines, index):
    if index + 1 >= len(lines):
        return index
    _, code = lines[index + 1]
    return index + 1 if code.lstrip().startswith(':') and code.rstrip().endswith('{ }') else index


def constructor_indents(lines):
    functions = [(indentation(line), parse_function(line.strip(), code.strip())) for line, code in lines]
    functions = [(indent, parsed[1]) for indent, parsed in functions if parsed]
    base = min((indent for indent, _ in functions), key=len, default='')
    owners = {fields.signature.split('(', 1)[0].lstrip('~'): base for _, fields in functions
              if not fields.typ and is_function_declaration(fields, set())}
    for line, code in lines:
        if match := re.match(r'\s*(?:class|struct)\s+(\w+).*\{', code):
            owners[match[1]] = indentation(line) + ' ' * INDENT_WIDTH
    return owners


def normalise_member_indents(lines):
    owners = constructor_indents(lines)
    for line, code in lines:
        match = re.match(r'\s*~?(\w+)\(', code)
        if match and match[1] in owners:
            line = owners[match[1]] + line.lstrip()
            code = owners[match[1]] + code.lstrip()
        yield line, code


def initializer_end(lines, start):
    code = ''
    for end in range(start, len(lines)):
        code += lines[end][1]
        balanced = all(code.count(a) == code.count(b) for a, b in [('(', ')'), ('[', ']'), ('{', '}')])
        if balanced and code.rstrip().endswith(';'):
            return end
    return start


def parsed_lines(lines, constructors):
    continuation = -1
    for index, (line, code) in enumerate(lines):
        if index <= continuation:
            yield line, code, None, True
            continue
        item = equals_head(line, code, constructors)
        if item:
            continuation = initializer_end(lines, index)
        item = item or parse(line, code, constructors)
        multiline = multiline_declaration(lines, index, constructors) if not item else None
        if multiline:
            item, continuation = multiline
        if item and item.kind == 'function':
            continuation = max(continuation, constructor_tail_end(lines, index))
        yield line, code, item, False


def ends_group(group, item, continuation):
    return not continuation and group.entries and (not item or group_key(item) != group.key)


def grouped_lines(lines, constructors):
    group = Group()
    for line, code, item, continued in parsed_lines(lines, constructors):
        continuation = continued or (is_comment_only(line, code) and group.entries)
        if ends_group(group, item, continuation):
            yield group
            group = Group()
        group.add(line, item)
        if not item and not continuation:
            yield group
            group = Group()
    if group.entries:
        yield group


def constructor_names(code):
    return set(re.findall(r'\b(?:class|struct)\s+(\w+)', code))


def align(text):
    normalised = normalise_empty(normalise_spacing(text))
    original = zip(normalised.splitlines(), mask(normalised).splitlines())
    lines = [normalise_initialiser_list(line, code) for line, code in original]
    pairs = zip(lines, mask('\n'.join(lines)).splitlines())
    output, exceptions, count = [], [], 0
    declarations = split_declarators(normalise_member_indents(list(pairs)))
    for group in grouped_lines(declarations, constructor_names(mask(normalised))):
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


def write_if_changed(path, original, result):
    if result.text == original:
        return False
    path.write_text(result.text)
    return True


def report_if_changed(path, original, result):
    if result.text == original:
        return False
    print(path)
    return True


def main():
    args = arguments()
    if args.self_check:
        return int(not check_python_columns())
    update = report_if_changed if args.check else write_if_changed
    failed = False
    for path in source_files(args.paths):
        original = path.read_text()
        result = align(original)
        if args.report:
            print_statistics(path, result)
        failed |= update(path, original, result)
    return int(failed and args.check)


if __name__ == '__main__':
    raise SystemExit(main())
