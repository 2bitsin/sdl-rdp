#!/usr/bin/env python3
"""Align declaration and statement columns while preserving protected C and C++ text."""
import argparse
import itertools
import pathlib
import re
from types import SimpleNamespace
from typing import NamedTuple

RAW_DELIMITER_LIMIT = 16  # C++ raw-string delimiters have at most 16 characters.
PROTECTED           = re.compile(r'R"(?P<d>[^ ()\\\t\r\n]{0,' + str(RAW_DELIMITER_LIMIT) + r'})\(.*?\)(?P=d)"'
                                 r'|"(?:\\.|[^"\\])*"|(?<![0-9])\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/'
                                 r'|^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*', re.S | re.M)
EQUALS              = r'(?<![=!<>+*/%&|^\-])=(?!=|>)'
DECL                = r'(.+?[\s*&])([A-Za-z_]\w*(?:\s*\[[^\]]*\])*)\s*(?:(' + EQUALS + r'|[{]|:(?!:))(.*))?;'
FORBIDDEN           = {'return', 'co_return', 'throw', 'delete', 'using', 'typedef', 'case', 'goto',
                       'else', 'break', 'if', 'while', 'for', 'switch'}
SOURCE_SUFFIXES     = frozenset({'.c', '.h', '.cpp', '.hpp'})
COLUMN_LIMIT        = 120
COMMENT_GAP         = 2
INDENT_WIDTH        = 2
ARROW               = '->'
ARROW_SEPARATOR     = f' {ARROW} '
CLOSER_OF           = dict(zip(')]}>', '([{<'))
BRACKETS            = dict(zip('([{', ')]}'))
CLOSING             = dict(BRACKETS, B='}')
MINOR_KINDS         = frozenset({'forward', 'alias', 'enumdef'})
ANCHORED_KINDS      = frozenset({'enum', 'table', 'ctor'})
FORWARD_TYPES       = frozenset({'class', 'struct', 'union', 'enum', 'enum class'})
SCOPE_WORDS         = frozenset({'namespace', 'class', 'struct', 'union', 'enum', 'extern'})
QUALIFIER_WORDS     = frozenset({'const', 'volatile', 'noexcept', 'override', 'final'})
BUILTIN_TYPES       = frozenset({'int', 'bool', 'char', 'float', 'double', 'long', 'short', 'unsigned', 'signed',
                                 'void', 'auto'})
SPECIFIER           = re.compile(r'\b(?:override|final)\b|(?<![=!<>])=(?!=)')
TRAILING_NOEXCEPT   = re.compile(r'\bnoexcept(?:\([^)]*\))?$')
BODY_BRACE          = re.compile(r'(?:[)\]]|\b(?:mutable|const|noexcept|override|final|else|do|try)'
                                 r'|->[\w:<>&*\s]+)\s*\{$')
TABLE_TAIL          = re.compile(r'[\s,})\];]*\{?')
ARGUMENT            = re.compile(r'''["'@]|\b\d|[-+/%!?.|^~]|\w\s*\(|^[*&]|\b(?:true|false|nullptr|this)\b''')


class CaseFields(NamedTuple):
    label: str
    statement: str


class AssignmentFields(NamedTuple):
    target: str
    operator: str
    value: str
    tail: str = ';'


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
    arguments: str = ''


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
    width: int


class ExcludedForColumns(NamedTuple):
    """A member that fits the limit in its run but would push other members over it, so it stands alone."""
    line_number: int
    text: str
    width: int


class Aligned(NamedTuple):
    text: str
    groups: int
    exceptions: list[Overflow | ExcludedForColumns]


class Item(NamedTuple):
    indent: str
    kind: str
    fields: Fields
    comment: str


class Physical(NamedTuple):
    line: str
    code: str
    original: str
    hidden: str
    protected: bool = False


class Segment(NamedTuple):
    column: int
    text: str
    code: str
    separator: str
    extra: int


class Logical(NamedTuple):
    key: tuple[int, int]
    text: str
    code: str
    extra: int


class Member(NamedTuple):
    key: tuple[int, int]
    item: Item
    extra: int


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


def step_bracket(stack, char):
    if char in BRACKETS:
        stack.append(char)
    elif stack and CLOSING[stack[-1]] == char:
        stack.pop()
    else:
        return int(char in CLOSER_OF and char != '>')
    return 0


def scan_brackets(code, stack=()):
    stack, unmatched = list(stack), 0
    for char in code:
        unmatched += step_bracket(stack, char)
    return stack, unmatched


def is_balanced(code):
    stack, unmatched = scan_brackets(code)
    return not stack and not unmatched


def code_end(code):
    return code.rstrip(' @')


def trailing_comment(body):
    matches = [m[0] for m in PROTECTED.finditer(body) if m[0].startswith(('//', '/*'))]
    return matches[-1] if matches and body.rstrip().endswith(matches[-1]) else ''


def without_comment(line):
    comment = trailing_comment(line)
    return line[:line.rfind(comment)].rstrip() if comment else line.rstrip()


def match_fields(pattern, body, code):
    match = re.fullmatch(pattern, code)
    if match is None:
        return None
    spans = (match.span(i) for i in range(1, len(match.groups()) + 1))
    return [body[start:end].strip() if start >= 0 else '' for start, end in spans]


def parse_case(body, code):
    if fields := match_fields(r'(case\s+(?:[^:]|::)+:|default:)\s*(.+;)', body, code):
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
    arrow = suffix.find(ARROW)
    start = arrow + len(ARROW) if arrow >= 0 else 0
    code = mask(suffix)
    match = SPECIFIER.search(code, start) or TRAILING_NOEXCEPT.search(code.rstrip(), start)
    if match:
        return suffix[:match.start()].strip(), suffix[match.start():].strip()
    return suffix, ''


def function_parts(body, code):
    pattern = r'(.*?)(?<![\w:])((?:\w+::)*(?:operator\s*(?:\(\)|\[\]|[^ (][^(]*?)|\w+)|~\w+)(\s*\()'
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


def is_argument(part):
    code = re.sub(r'/\*.*?\*/', '', part).replace('...', '').strip()
    code = re.sub(r'(?<![=!<>])=(?!=).*', '', code)
    code = re.sub(r'<[^<>]*>|\[[^\]]*\]', '', code).strip()
    lone = re.fullmatch(r'[a-z_]\w*', code) and code not in BUILTIN_TYPES and not code.endswith('_t')
    return bool(ARGUMENT.search(mask(code)) or lone)


def paren_initialised(typ, signature):
    name, _, arguments = signature.partition('(')
    parts = split_top_level_commas(arguments[:-1])
    if not typ or not any(is_argument(part) for part in parts if part):
        return None
    return 'decl', DeclarationFields(typ, name.strip(), '', '', arguments='(' + arguments)


def parse_function(body, code):
    parts = function_parts(body, code)
    if not parts:
        return None
    typ, signature, suffix = parts
    name = signature.split('(', 1)[0].strip()
    if name in FORBIDDEN or (typ and not is_declaration_type(typ)):
        return None
    if '::' in name and suffix != ';' and not suffix.endswith(';'):
        return None
    if suffix == ';' and (local := paren_initialised(typ, signature)):
        return local
    return function_fields(typ, signature, suffix)


def function_fields(typ, signature, suffix):
    suffix, tail = function_tail(suffix)
    suffix, specifier = function_suffix(suffix)
    if specifier.startswith('=') and not re.fullmatch(r'=\s*(?:0|default|delete)', specifier):
        return None
    qualifiers, arrow, result = suffix.partition(ARROW)
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
    end = closing_brace(code, code.index('{'))
    if end is None or code[end + 1:] != ';':
        return None
    return DeclarationFields(typ, name, opener, value[:-1].strip(), '}')


def is_declarator(typ, name, opener):
    if typ in ('class', 'struct') and opener == ':':
        return False
    return is_declaration_type(typ) and name != 'operator' and name not in QUALIFIER_WORDS


def parse_declaration(body, code):
    fields = match_fields(DECL, body, code)
    if not fields or not is_declarator(*fields[:3]):
        return None
    typ, name, opener, value = fields
    parsed = declaration_fields(re.sub(r'\s+', ' ', typ), name, opener, value, code)
    if not parsed:
        return None
    return 'forward' if not opener and parsed.typ in FORWARD_TYPES else 'decl', parsed


def parse_assignment(body, code):
    pattern = r'([\w:*&][\w:.>\-\[\]()@ *&]*?)\s*(<<=|>>=|[+*/%&|^\-]=|' + EQUALS + r')\s*(.*);'
    if fields := match_fields(pattern, body, code):
        target, operator, value = fields
        lhs = mask(target)
        if is_balanced(lhs) and lhs.split()[0] not in FORBIDDEN and not lhs.endswith(('>', '<')):
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
    if len(cells) < 2 or any('{' in mask(cell) for cell in cells) or not TABLE_TAIL.fullmatch(code[end + 1:]):
        return None
    return 'table', TableFields(tuple(cells), body[end + 1:])


def parse_alias(body, code):
    if fields := match_fields(r'(using)\s+(\w+)\s*(=)\s*(.*);', body, code):
        return 'alias', DeclarationFields(*fields)
    return None


def parse_enum_class(body, code):
    if fields := match_fields(r'(enum(?:\s+class)?)\s+(\w+)\s*(\{.*\});', body, code):
        typ, name, value = fields
        return 'enumdef', DeclarationFields(typ, name, '{', value[1:-1].strip(), '}')
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
    special = re.fullmatch(r'=\s*(?:default|delete)', fields.specifier)
    recurs  = re.search(rf'\b{re.escape(name)}\b', fields.signature[len(name) + 1:])
    return name in constructors or bool(special) or bool(recurs)


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


def closers_of(stack):
    return ''.join(CLOSING[opener] for opener in reversed(stack))


def without_closers(text, closers):
    for closer in reversed(closers):
        if not text.endswith(closer):
            break
        text = text[:-1].rstrip()
    return text


def is_open_head(item, stack):
    kind, fields = item.kind, item.fields
    if kind == 'function' or (kind == 'decl' and fields.params):
        return stack[-1:] == ['(']
    if kind == 'decl' and fields.opener == '{':
        return stack[-1:] == ['{'] and not SCOPE_WORDS.intersection((fields.typ + ' ' + fields.name).split())
    return (kind == 'decl' and fields.opener == '=') or kind == 'assign'


def head_field(item):
    if item.kind == 'function':
        return 'signature'
    return 'params' if item.kind == 'decl' and item.fields.params else 'value'


def as_head(item, closers):
    name = head_field(item)
    fields = item.fields._replace(**{name: without_closers(getattr(item.fields, name), closers)}, tail='')
    if item.kind == 'decl':
        fields = fields._replace(closer='')
    return item._replace(fields=fields)


def head_item(line, code, constructors):
    body, comment = without_comment(line), trailing_comment(line)
    stack, _ = scan_brackets(code[:len(body)])
    closers = closers_of(stack)
    item = parse(body + closers + ';', code[:len(body)] + closers + ';', constructors)
    if not item or not is_open_head(item, stack):
        return None
    return as_head(item, closers)._replace(comment=comment)


def render_braced_value(value, width):
    return value.ljust(width) + ' }' if value else '}'


def render_declaration(fields, widths):
    body = fields.typ.ljust(widths.typ) + ' '
    name = fields.name + fields.arguments
    if fields.params:
        name = name.ljust(widths.name) + fields.params
    if fields.opener:
        name_width = widths.name + (widths.params if fields.params else 0)
        body += name.ljust(name_width) + widths.gap + fields.opener + ' '
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
    head = fields.signature
    if fields.arrow:
        head = head.ljust(widths.signature) + ARROW_SEPARATOR + fields.result
    if fields.specifier:
        head = head.ljust(widths.specifier) + ' ' + fields.specifier
    return prefix + head + fields.tail


def render_case(fields, widths):
    return fields.label.ljust(widths.label) + ' ' + fields.statement


def render_assignment(fields, widths):
    target = fields.target.ljust(widths.target)
    return (target + ' ' + fields.operator.ljust(widths.operator) + ' ' + fields.value).rstrip() + fields.tail


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
    'forward': render_declaration,
    'alias': render_declaration,
    'enumdef': render_declaration,
    'ctor': render_initialiser,
    'function': render_function,
    'case': render_case,
    'assign': render_assignment,
    'enum': render_enumerator,
}


def render(item, widths):
    return item.indent + RENDERERS[item.kind](item.fields, widths)


def column_widths(fields):
    return {name: max(len(getattr(f, name)) for f in fields) for name in fields[0]._fields}


def measure_table(fields):
    columns = zip(*(field.cells for field in fields))
    return SimpleNamespace(cells=[max(map(len, column)) for column in columns])


def measure_declaration(fields):
    widths = column_widths(fields)
    widths['value'] = max((len(f.value) for f in fields if f.closer), default=0)
    widths['gap'] = ' ' if any(f.opener in ('=', ':') for f in fields) else ''
    return SimpleNamespace(**widths)


def specifier_start(fields, signature_width):
    if fields.arrow:
        return signature_width + len(ARROW_SEPARATOR) + len(fields.result)
    return len(fields.signature)


def measure_function(fields):
    signature = max((len(f.signature) for f in fields if f.arrow), default=0)
    specifier = max((specifier_start(f, signature) for f in fields if f.specifier), default=0)
    return SimpleNamespace(typ=max(len(f.typ) for f in fields), signature=signature, specifier=specifier)


MEASURERS = {
    'table': measure_table,
    'decl': measure_declaration,
    'forward': measure_declaration,
    'alias': measure_declaration,
    'enumdef': measure_declaration,
    'function': measure_function,
}


def measure(items):
    fields = [item.fields for item in items]
    measurer = MEASURERS.get(items[0].kind)
    return measurer(fields) if measurer else SimpleNamespace(**column_widths(fields))


def is_comment_only(line, hidden):
    return bool(line.strip() and not hidden.replace('@', '').strip() and line.lstrip().startswith(('//', '/*', '*')))


def group_key(item, depth):
    indent = item.indent
    if item.kind == 'ctor' and item.fields.prefix:
        indent += ' ' * (len(item.fields.prefix) + 1)
    kind = ('table', len(item.fields.cells)) if item.kind == 'table' else item.kind
    return indent, kind, depth


class Group:
    def __init__(self, members, forced):
        self.members = members
        self.forced = {index: forced[member.key] for index, member in enumerate(members) if member.key in forced}

    def render(self, active):
        if not active:
            return {}
        widths = measure([self.members[index].item for index in active])
        proposed = {index: render(self.members[index].item, widths) for index in active}
        comment_column = max(map(len, proposed.values()), default=0) + COMMENT_GAP
        for index, body in proposed.items():
            if comment := self.members[index].item.comment:
                proposed[index] = body.ljust(comment_column) + comment
        return proposed

    def widths(self, active):
        proposed = self.render(active)
        return {index: self.width(index, text) for index, text in proposed.items()}

    def width(self, index, text):
        member = self.members[index]
        return len(text) + member.extra

    def overflow(self, active):
        return sum(max(0, width - COLUMN_LIMIT) for width in self.widths(active).values())

    def unaligned_width(self, index):
        item = self.members[index].item
        return self.width(index, render(item, measure([item])))

    def exclusion_cost(self, active, index):
        return self.overflow([other for other in active if other != index]), -self.unaligned_width(index)

    def setter(self, active, over):
        return min(sorted(set(active) - set(over)) + over, key=lambda index: self.exclusion_cost(active, index))

    def readmit(self, active, excluded):
        for index in excluded:
            trial = sorted(active + [index])
            if not self.overflow(trial):
                active = trial
        return active

    def aligned(self):
        active = [index for index in range(len(self.members)) if index not in self.forced]
        excluded = []
        while over := [index for index, width in self.widths(active).items() if width > COLUMN_LIMIT]:
            excluded.append(self.setter(active, over))
            active.remove(excluded[-1])
        active = self.readmit(active, excluded)
        widths = self.widths(sorted(active + excluded))
        overflows = {self.members[index].key: widths[index] for index in excluded if index not in active}
        overflows.update({self.members[index].key: width for index, width in self.forced.items()})
        return {self.members[index].key: text for index, text in self.render(active).items()}, overflows


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
    return render_initialiser(fields, measure([Item('', 'ctor', fields, '')])) + part[end + 1:]


def normalise_initialiser_list(line, code):
    match = re.search(r'(?<=\))\s*:(?!:)(?=\s*\w+\s*\{)', code)
    if not match:
        return line
    parts = split_top_level_commas(line[match.end():])
    if len(parts) < 2 or not all(parts):
        return line
    return line[:match.end()] + ' ' + ', '.join(map(normalise_initialiser_part, parts))


def expand_declarators(line, parts, typ, comment):
    indent = indentation(line)
    base = re.sub(r'(?:[*&]\s*(?:(?:const|volatile)\s*)?)+$', '', typ).rstrip()
    declarations = [indent + part + ';' for part in [parts[0]] + [base + ' ' + p for p in parts[1:]]]
    if comment:
        declarations[-1] += ' ' + comment
    return [Physical(declaration, mask(declaration), declaration, mask(declaration)) for declaration in declarations]


def split_declarator(physical):
    line, code = physical.line, physical.code
    body = without_comment(line)
    if not code[:len(body)].endswith(';') or not is_balanced(code[:len(body)]):
        return [physical]
    parts = split_top_level_commas(body[:-1])
    item = parse(parts[0] + ';', mask(parts[0] + ';'))
    if len(parts) > 1 and item and item.kind == 'decl':
        return expand_declarators(line, parts, item.fields.typ, trailing_comment(line))
    return [physical]


def split_declarators(lines):
    return [part for physical in lines for part in split_declarator(physical)]


def constructor_tail_end(physical, index):
    if index + 1 >= len(physical):
        return index
    code = physical[index + 1].code
    return index + 1 if code.lstrip().startswith(':') and code.rstrip().endswith('{ }') else index


def brace_depths(lines):
    """The brace depth each line starts at."""
    return list(itertools.accumulate((line.code.count('{') - line.code.count('}') for line in lines), initial=0))


def member_functions(lines):
    functions = [(indentation(line.line), depth, parse_function(line.line.strip(), line.code.strip()))
                 for line, depth in zip(lines, brace_depths(lines))]
    return [(indent, depth, parsed[1]) for indent, depth, parsed in functions if parsed and parsed[0] == 'function']


def constructor_indents(lines):
    """Each constructor name's member indent and the brace depth its declarations sit at."""
    functions = member_functions(lines)
    base = min(((indent, depth) for indent, depth, _ in functions), key=lambda owner: len(owner[0]), default=('', 0))
    owners = {fields.signature.split('(', 1)[0].lstrip('~'): base for _, _, fields in functions
              if not fields.typ and is_function_declaration(fields, set())}
    for line, depth in zip(lines, brace_depths(lines)):
        if match := re.match(r'\s*(?:template\s*<.*>\s*)?(?:class|struct)\s+(?:\w+::)*(\w+).*\{', line.code):
            owners[match[1]] = indentation(line.line) + ' ' * INDENT_WIDTH, depth + 1
    return owners


def normalise_member_indents(lines):
    owners = constructor_indents(lines)
    for line, depth in zip(lines, brace_depths(lines)):
        match = re.match(r'\s*~?(\w+)\(', line.code)
        if match and match[1] in owners and owners[match[1]][1] == depth:
            indent = owners[match[1]][0]
            line = line._replace(line=indent + line.line.lstrip(), code=indent + line.code.lstrip())
        yield line


def protected_lines(text):
    starts = {text.count('\n', 0, m.start()): text.count('\n', 0, m.end()) for m in PROTECTED.finditer(text)}
    return {number for start, end in starts.items() for number in range(start + 1, end + 1)}


def physical_lines(text):
    normalised = normalise_empty(normalise_spacing(text))
    pairs = zip(normalised.splitlines(), mask(normalised).splitlines())
    lines = [normalise_initialiser_list(line, code) for line, code in pairs]
    interior = protected_lines(text)
    originals = zip(lines, mask('\n'.join(lines)).splitlines(), text.splitlines(), mask(text).splitlines())
    physical = [Physical(*parts, number in interior) for number, parts in enumerate(originals)]
    return split_declarators(normalise_member_indents(physical))


def scan_line(code, stack):
    stack, _ = scan_brackets(code, stack)
    if stack[-1:] == ['{'] and BODY_BRACE.search(code_end(code)):
        stack[-1] = 'B'
    return stack


def statement_closed(stack, code, kind):
    return (not stack and code_end(code).endswith((';', '}'))) or (kind == 'function' and stack == ['B'])


def is_new_statement(line, stack, depth):
    return not line.line.strip() or (not stack and len(indentation(line.line)) <= depth)


def statement_extent(physical, start, kind):
    stack, depth, skipped = scan_line(physical[start].code, []), len(indentation(physical[start].line)), {}
    for end in range(start + 1, len(physical)):
        line = physical[end]
        closed = statement_closed(stack, physical[end - 1].code, kind)
        if closed or (kind != 'expression' and is_new_statement(line, stack, depth)):
            return skipped
        before, stack = stack.count('B'), scan_line(line.code, stack)
        if not before or stack.count('B') < before:
            skipped[end] = depth
    return skipped


def can_anchor(hidden, column):
    code = code_end(hidden)
    if code.endswith((';', '{', '}')) or column >= len(code):
        return False
    return hidden[column] != ' ' and hidden[column - 1] in ' ([{,'


def parent_line(physical, index, column):
    for above in range(index - 1, -1, -1):
        original = physical[above].original
        if not original.strip():
            return None
        if len(indentation(original)) < column:
            return above
    return None


def anchor_of(physical, index):
    line = physical[index]
    if line.protected or not line.original.strip() or line.original.lstrip().startswith('#'):
        return None
    column = len(indentation(line.original))
    above = parent_line(physical, index, column)
    if above is None or not can_anchor(physical[above].hidden, column):
        return None
    return above, column


def find_anchors(physical):
    anchors = {index: anchor_of(physical, index) for index in range(len(physical))}
    return {index: anchor for index, anchor in anchors.items() if anchor}


def normalised_column(physical, column):
    count = len(''.join(physical.original[:column].split()))
    starts = [match.start() for match in re.finditer(r'\S', physical.line)]
    return starts[count] if count < len(starts) else len(physical.line)


def segment(physical, column, start, end):
    raw = physical.line[start:end]
    text = raw.rstrip()
    separator = raw[len(text):]
    return Segment(column, text, physical.code[start:start + len(text)], separator, len(physical.line) - len(text))


def split_segments(physical, columns, anchored):
    bounds = [0] + [normalised_column(physical, column) for column in columns] + [len(physical.line)]
    spans = zip([0] + columns, bounds, bounds[1:])
    segments = [segment(physical, column, start, end) for column, start, end in spans]
    if anchored:
        first = segments[0]
        indent = len(indentation(first.text))
        segments[0] = first._replace(text=first.text[indent:], code=first.code[indent:])
    return segments


def line_segments(physical, anchors):
    columns = {}
    for parent, column in anchors.values():
        columns.setdefault(parent, set()).add(column)
    return [split_segments(line, sorted(columns.get(index, ())), index in anchors)
            for index, line in enumerate(physical)]


def logical_streams(segments, anchors):
    streams = {None: []}
    for index, parts in enumerate(segments):
        for part in parts:
            stream = (index, part.column) if part.column else anchors.get(index)
            logical = Logical((index, part.column), part.text, part.code, part.extra)
            streams.setdefault(stream, []).append(logical)
    return streams


def statement_text(physical, index, skipped):
    return indentation(physical[index].line) + ' '.join(physical[line].line.strip() for line in [index, *skipped])


def local_head(item, statement):
    local = parse(statement, mask(statement))
    if item.kind != 'function' or not local or local.kind != 'decl' or not local.fields.arguments:
        return item
    name, _, arguments = item.fields.signature.partition('(')
    fields = DeclarationFields(item.fields.typ, name.strip(), '', '', tail='', arguments='(' + arguments)
    return item._replace(kind='decl', fields=fields)


def open_statement(physical, logical, constructors):
    index = logical.key[0]
    item = head_item(logical.text, logical.code, constructors)
    if not item:
        unfinished = code_end(logical.code).strip()
        boundary = not unfinished or unfinished.endswith((';', '{', '}', ':')) or unfinished.startswith('@')
        if not boundary and unfinished.split()[0] not in SCOPE_WORDS | {'template'}:
            return None, statement_extent(physical, index, 'expression')
        return None, {}
    skipped = statement_extent(physical, index, item.kind)
    return local_head(item, statement_text(physical, index, skipped)), skipped


def top_level_item(physical, logical, constructors):
    index = logical.key[0]
    item, skipped = parse(logical.text, logical.code, constructors), {}
    if not item and not code_end(logical.code).endswith(';'):
        item, skipped = open_statement(physical, logical, constructors)
    if item and item.kind == 'function':
        depth = len(indentation(logical.text))
        skipped.update(dict.fromkeys(range(index + 1, constructor_tail_end(physical, index) + 1), depth))
    return item, skipped


def inner_item(logical, constructors, depth=-1):
    item = parse(logical.text, logical.code, constructors)
    if item and item.kind in ANCHORED_KINDS:
        return item, False
    if is_comment_only(logical.text, logical.code):
        return None, False
    return None, len(indentation(logical.text)) <= depth


def top_level_items(physical, logicals, constructors):
    skipped = {}
    for logical in logicals:
        if logical.key[0] in skipped:
            yield inner_item(logical, constructors, skipped[logical.key[0]])
            continue
        if is_comment_only(logical.text, logical.code):
            yield None, False
            continue
        item, extent = top_level_item(physical, logical, constructors)
        skipped |= extent
        yield item, False


def anchored_items(logicals, constructors):
    return (inner_item(logical, constructors) for logical in logicals)


def kind_of(key):
    return key[1][0] if isinstance(key[1], tuple) else key[1]


def stays_open(key, depth, item_key):
    if len(key[0]) != depth:
        return len(key[0]) < depth
    return item_key is not None and (key == item_key or kind_of(key) in MINOR_KINDS)


def still_open(open_groups, depth, item_key=None):
    return {key: group for key, group in open_groups.items() if stays_open(key, depth, item_key)}


def stream_groups(logicals, items, depths):
    groups, open_groups = [], {}
    for position, (item, continued) in enumerate(items):
        if continued:
            continue
        if item is None:
            open_groups = still_open(open_groups, len(indentation(logicals[position].text)))
            continue
        key = group_key(item, depths[logicals[position].key[0]])
        if item.kind not in MINOR_KINDS:
            open_groups = still_open(open_groups, len(key[0]), key)
        if key not in open_groups:
            open_groups[key] = []
            groups.append(open_groups[key])
        open_groups[key].append(position)
    return groups


def stream_items(physical, stream, logicals, constructors):
    if stream is None:
        return list(top_level_items(physical, logicals, constructors))
    return list(anchored_items(logicals, constructors))


def assemble(segments, anchors, rendered):
    offsets, output = {}, []
    for index, parts in enumerate(segments):
        text = ' ' * offsets[anchors[index]] if index in anchors else ''
        for part in parts:
            offsets[(index, part.column)] = len(text)
            aligned = rendered.get((index, part.column), part.text)
            text += aligned + (part.separator if part is not parts[-1] or aligned == part.text else '')
        output.append(text)
    return output


def overflows(output, excluded):
    return [(Overflow if width > COLUMN_LIMIT else ExcludedForColumns)(index + 1, output[index], width)
            for (index, _), width in sorted(excluded.items())]


def constructor_names(code):
    return set(re.findall(r'\b(?:class|struct)\s+(?:\w+::)*(\w+)', code)) | set(re.findall(r'~(\w+)\s*\(', code))


def stream_members(streams, physical, constructors):
    groups, depths = [], brace_depths(physical)
    for stream, logicals in streams.items():
        parsed = stream_items(physical, stream, logicals, constructors)
        levels = depths if stream is None else [0] * len(depths)
        groups += [[Member(logicals[p].key, parsed[p][0], logicals[p].extra) for p in positions]
                   for positions in stream_groups(logicals, parsed, levels)]
    return groups


def render_groups(groups, forced):
    texts, excluded = {}, {}
    for members in groups:
        aligned, over = Group(members, forced).aligned()
        texts.update(aligned)
        excluded.update(over)
    return texts, excluded


def padding_chain(index, segments, anchors, texts):
    chain, limit = [], None
    while index is not None:
        parts = [part for part in reversed(segments[index]) if limit is None or part.column < limit]
        paddings = [((index, part.column), len(texts.get((index, part.column), part.text)) - len(part.text))
                    for part in parts]
        chain += [(key, padding) for key, padding in paddings if padding > 0]
        index, limit = anchors.get(index, (None, None))
    return chain


def padding_source(index, width, segments, anchors, texts):
    chain = padding_chain(index, segments, anchors, texts)
    if not chain or width - sum(padding for _, padding in chain) > COLUMN_LIMIT:
        return None
    return chain[0][0]


def pushed_overflows(output, layout, texts):
    pushed = {}
    for index, line in enumerate(output):
        width = len(line)
        source = width > COLUMN_LIMIT and padding_source(index, width, *layout, texts)
        if source:
            pushed.setdefault(source, width)
    return pushed


def lay_out(segments, anchors, groups):
    forced = {}
    while True:
        texts, excluded = render_groups(groups, forced)
        output = assemble(segments, anchors, texts)
        pushed = pushed_overflows(output, (segments, anchors), texts)
        if not pushed.keys() - forced.keys():
            return output, excluded
        forced.update(pushed)


def align(text):
    physical = physical_lines(text)
    anchors = find_anchors(physical)
    segments = line_segments(physical, anchors)
    groups = stream_members(logical_streams(segments, anchors), physical, constructor_names(mask(text)))
    output, excluded = lay_out(segments, anchors, groups)
    result = '\n'.join(output) + ('\n' if text.endswith('\n') else '')
    return Aligned(result, len(groups), overflows(output, excluded))


def source_files(paths):
    """The C and C++ files formatting owns: .c, .h, .cpp and .hpp, symlinks aside."""
    candidates = {path for root in paths for path in (root.rglob('*') if root.is_dir() else [root])}
    return sorted(path for path in candidates if path.suffix in SOURCE_SUFFIXES and not path.is_symlink())


def print_statistics(path, stats):
    print(f'{path}: {stats.groups} groups; {len(stats.exceptions)} exceptions')
    for exception in stats.exceptions:
        print(f'{path}:{exception.line_number}: {type(exception).__name__} {exception.width}: {exception.text}')


def check_python_columns(directory):
    paths = (directory / 'columns.py', directory / 'test_columns.py')
    violations = [(path, number, len(line)) for path in paths
                  for number, line in enumerate(path.read_text().splitlines(), 1) if len(line) > COLUMN_LIMIT]
    for path, number, width in violations:
        print(f'{path}:{number}: {width} columns > {COLUMN_LIMIT}')
    return not violations


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--self-check', action='store_true')
    parser.add_argument('--report', action='store_true')
    parser.add_argument('paths', nargs='*', type=pathlib.Path)
    return parser.parse_args(argv)


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


def main(argv=None):
    args = arguments(argv)
    if args.self_check:
        return int(not check_python_columns(args.paths[0] if args.paths else pathlib.Path(__file__).parent))
    update = report_if_changed if args.check else write_if_changed
    failed = False
    for path in source_files(args.paths or [pathlib.Path('sources')]):
        original = path.read_text()
        result = align(original)
        if args.report:
            print_statistics(path, result)
        failed |= update(path, original, result)
    return int(failed and args.check)


if __name__ == '__main__':
    raise SystemExit(main())
