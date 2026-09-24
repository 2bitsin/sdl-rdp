#!/usr/bin/env python3
"""Check source shape and reject stale entries in the structural debt ratchet."""
import argparse
import ast
import collections
import dataclasses
import enum
import os
import pathlib
import re
import sys
import typing

# Limits: Step 0 quality gate brief, 2026-09-23; function limits: sdl-rdp#4 and the done table.
FILE_LINES           = 400
CLASS_LINES          = 150
DATA_MEMBERS         = 10
MEMBER_FUNCTIONS     = 15
COMMENT_PERCENT      = 15
LAYOUT               = 0
NOLINT_LINES         = 10
COLUMNS              = 120
BODY_LINES_NORM      = 20
BODY_LINES           = 40
COMPLEXITY           = 10
PARAMETERS           = 5
NESTING              = 2
LONG_LINES           = 0
COMPOUND_CONTRACTS   = 0
C_FILES              = 0
HEADER_CLASSES       = 1
HEADER_BODIES        = 0
HEADER               = '.hpp'
EXTENSIONS           = frozenset(('.c', '.h', '.cpp', '.hpp'))
FROZEN               = frozenset(('sources/sdl-rdp-backend.so/sdl-rdp-backend.h',))
PUBLIC_DATA_CLASSES  = frozenset((('sources/sdl-rdp-backend.so/_detail/state.hpp', 'Peer'),
                                  ('sources/sdl-rdp-backend.so/_detail/state.hpp', 'State')))
FIXTURE_ROOTS        = frozenset(('Test', 'TestWithParam'))
GTEST_MACROS         = frozenset(('TEST', 'TEST_F', 'TEST_P'))
NOT_FUNCTIONS        = frozenset(('using', 'typedef', 'enum', 'class', 'struct', 'friend', 'static_assert'))
NOT_DATA             = NOT_FUNCTIONS | {'template'}
NOT_DECLARATORS      = frozenset(('decltype', 'alignas', 'sizeof'))
BRACKETS             = {')': '(', ']': '[', '}': '{'}
BRACKET_NAMES        = {'(': 'parentheses', '[': 'brackets', '{': 'braces'}
ANGLE_STOPS          = frozenset((';', '{', ')', ']', '}', '=', '<'))
ANGLE_BEFORE         = re.compile(r'[\w:>]')
ANGLE_AFTER          = re.compile(r'[\w:]')
REFERENCE_FOLLOWERS  = frozenset((',', ')', '=', ';', ':'))
STATEMENT_BOUNDARIES = frozenset((';', '{', '}'))
EXPRESSION_KEYWORDS  = frozenset(('return', 'co_return', 'co_yield', 'throw', 'case', 'goto', 'delete', 'new',
                                  'sizeof', 'not', 'else', 'do'))
LAMBDA_AFTER_WORDS   = frozenset(('return', 'co_return', 'co_yield', 'throw', 'case'))
LAMBDA_STOPS         = frozenset((';', ',', ')', ']', '}'))
SUBSCRIPTED_ENDINGS  = frozenset((')', ']', '>', '"', "'"))
CONTROLS             = frozenset(('if', 'for', 'while', 'switch', 'do'))
CONDITION_PREFIXES   = frozenset(('constexpr', 'consteval', '!'))
CONTRACTS            = frozenset(('Expects', 'Ensures'))
LOGICAL              = frozenset(('&&', '||', 'and', 'or'))
BRANCHES             = LOGICAL | {'if', 'for', 'while', 'case', 'catch', '?'}
SCOPES               = frozenset(('namespace', 'extern', 'class', 'struct', 'union'))
EVALUATED            = frozenset(('constexpr', 'consteval'))
GENERATED            = (['=', 'default'], ['=', 'delete'])
ACCESS               = frozenset(('public', 'protected', 'private'))
RANKS                = {'public': 0, 'protected': 1, 'private': 2}
FUNCTION_LIMITS      = {'complexity': COMPLEXITY, 'parameters': PARAMETERS, 'nesting': NESTING}
HARD_ENTRY           = re.compile(rf' body lines \d+ > {BODY_LINES}$')
CONDITIONALS         = frozenset(('if', 'ifdef', 'ifndef'))
LITERALS             = ('"', 'R"')
PYTHON_CONTROLS      = (ast.If, ast.For, ast.AsyncFor, ast.While, ast.Match)
PYTHON_SCOPES        = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)
RECEIVERS            = frozenset(('self', 'cls'))
DIRECTIVE            = re.compile(r'[ \t]*#[ \t]*(\w*)[ \t]*(\w*)')
LEXEMES              = re.compile(
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\\n])*\''
    r'|//[^\n]*|/\*.*?\*/|^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*'
    r"|[A-Za-z_]\w*|\d(?:'?[\w.])*|::|&&|\|\||->|[^\s]", re.S | re.M)


class Token(typing.NamedTuple):
    value:  str
    line:   int
    offset: int

    @property
    def lines(self):
        return range(self.line, self.line + self.value.count('\n') + 1)


class Measure(typing.NamedTuple):
    """A value over its limit; a measure that is not waivable fails whatever the allow list says."""
    line:     int
    label:    str
    value:    float
    limit:    int
    waivable: bool = True


class Finding(typing.NamedTuple):
    """An allow-file line: the report a finding prints is the entry that would list it."""
    key:      str
    waivable: bool = True


class AllowEntry(typing.NamedTuple):
    line:   int
    reason: str


class Declarator(typing.NamedTuple):
    name:       str
    parameters: int


class ClassSpan(typing.NamedTuple):
    """A class definition: its key and name, the line and token of its key, and its body braces."""
    kind:    str
    name:    str
    line:    int
    keyword: int
    start:   int
    end:     int

    def encloses(self, index):
        return self.start <= index <= self.end


class Member(typing.NamedTuple):
    access: str
    first:  int
    end:    int


class Function(typing.NamedTuple):
    """A function or lambda: its first token, parameter list, body braces and the statements measured."""
    name:       str
    line:       int
    first:      int
    parameters: int
    opening:    int
    closing:    int
    start:      int
    end:        int


class Control(typing.NamedTuple):
    children: tuple


class Brace(enum.Enum):
    FUNCTION    = enum.auto()
    SCOPE       = enum.auto()
    INITIALISER = enum.auto()
    VALUE       = enum.auto()


DEFINITIONS = frozenset((Brace.FUNCTION, Brace.SCOPE))


class Exposure(enum.Enum):
    """The access sections whose data members count against a class with behaviour."""
    STRICT  = frozenset(('public', 'protected'))
    FIXTURE = frozenset(('public',))
    OPEN    = frozenset()


@dataclasses.dataclass
class ClassTally:
    access:       str
    highest:      int                 = -1
    layout:       int                 = 0
    generated:    int                 = 0
    functions:    list[str]           = dataclasses.field(default_factory=list)
    data:         collections.Counter = dataclasses.field(default_factory=collections.Counter)
    declarations: collections.Counter = dataclasses.field(default_factory=collections.Counter)

    def enter(self, access):
        self.access = access
        self.layout += RANKS[access] < self.highest
        self.highest = max(self.highest, RANKS[access])

    def function(self, function, name):
        self.highest = max(self.highest, RANKS[self.access])
        special = (name, '~' + name, 'operator=')
        self.layout += function == name and any(value not in special for value in self.functions)
        self.layout += self.declarations[self.access] > 0
        self.functions.append(function)

    def datum(self, count):
        self.highest = max(self.highest, RANKS[self.access])
        self.data[self.access] += count
        self.declarations[self.access] += 1


class Source:
    """One C or C++ file, lexed once: tokens, bracket pairs, template angles and line data."""

    def __init__(self, path):
        self.path = path
        text = path.read_text()
        self.lines = text.splitlines()
        self.tokens, comments, literals = lex(text)
        nonblank = {number for number, line in enumerate(self.lines, 1) if line.strip()}
        self.nonblank = len(nonblank)
        self.comment_lines = comments & nonblank
        self.literal_columns = literal_columns(text, literals)
        self.pairs, self.unmatched = matching(self.tokens)
        self.openers = {closing: opening for opening, closing in self.pairs.items()}
        self.angles = template_angles(text, self.tokens, self.pairs)
        self.angle_openers = {closing: opening for opening, closing in self.angles.items()}

    def word(self, index):
        return self.tokens[index].value if 0 <= index < len(self.tokens) else ''

    def words(self, start, end):
        return [token.value for token in self.tokens[start:end]]

    def skip(self, index):
        """Return the index that closes the bracket or template angle opened at index, else index."""
        return self.angles.get(index, self.pairs.get(index, index))


def lexemes(text):
    line = 1
    previous = 0
    for match in LEXEMES.finditer(text):
        line += text.count('\n', previous, match.start())
        yield Token(match.group(), line, match.start())
        line += match.group().count('\n')
        previous = match.end()


def live_lexemes(text):
    """Yield the lexemes outside #if 0 regions."""
    level = 0
    for token in lexemes(text):
        directive = DIRECTIVE.match(token.value) if token.value.lstrip().startswith('#') else None
        words = directive.groups() if directive else ('', '')
        if level == 0 and words == ('if', '0'):
            level = 1
        elif level:
            level = disabled_level(words[0], level)
        else:
            yield token


def disabled_level(keyword, level):
    if keyword in CONDITIONALS:
        return level + 1
    if keyword == 'endif' or (keyword in ('else', 'elif') and level == 1):
        return level - 1
    return level


def lex(text):
    """Return the code tokens, the comment-only lines and the string literal tokens of a text."""
    tokens, code, comments, literals = [], set(), set(), []
    for token in live_lexemes(text):
        if token.value.startswith(('//', '/*')):
            comments.update(token.lines)
            continue
        code.update(token.lines)
        if token.value.startswith(LITERALS):
            literals.append(token)
        if not token.value.lstrip().startswith('#'):
            tokens.append(token)
    return tokens, comments - code, literals


def literal_columns(text, literals):
    """Map each line to the column spans string literals occupy on it."""
    columns = collections.defaultdict(list)
    for literal in literals:
        start = text.rfind('\n', 0, literal.offset) + 1
        pieces = text[start:literal.offset + len(literal.value)].split('\n')
        for number, piece in enumerate(pieces, literal.line):
            begin = literal.offset - start if number == literal.line else 0
            columns[number].append((begin, len(piece)))
    return columns


def matching(tokens):
    """Return the bracket pairs and the indices of brackets left unmatched."""
    stack, pairs, unmatched = [], {}, []
    for index, token in enumerate(tokens):
        if token.value in BRACKETS.values():
            stack.append(index)
        elif token.value in BRACKETS and stack and tokens[stack[-1]].value == BRACKETS[token.value]:
            pairs[stack.pop()] = index
        elif token.value in BRACKETS:
            unmatched.append(index)
    return pairs, sorted(unmatched + stack)


def template_angles(text, tokens, pairs):
    """Pair each '<' written flush between names with a '>' that no ';', '{', '=', '<' or outer closer precedes."""
    angles = {}
    for index in reversed([index for index, token in enumerate(tokens) if angle_candidate(text, token)]):
        if (closing := angle_close(tokens, pairs, angles, index)) is not None:
            angles[index] = closing
    return angles


def angle_candidate(text, token):
    return (token.value == '<' and token.offset > 0
            and ANGLE_BEFORE.match(text[token.offset - 1]) is not None
            and ANGLE_AFTER.match(text[token.offset + 1:token.offset + 2]) is not None)


def angle_close(tokens, pairs, angles, index):
    cursor = index + 1
    while cursor < len(tokens):
        word = tokens[cursor].value
        if word == '>':
            return cursor
        if word in ANGLE_STOPS and cursor not in angles:
            return None
        cursor = angles.get(cursor, pairs.get(cursor, cursor)) + 1
    return None


def over_limits(line, measures):
    return [Measure(line, label, value, limit) for label, value, limit in measures if value > limit]


def finding(scope, measure):
    return Finding(f'{scope}: {measure.label} {measure.value} > {measure.limit}', measure.waivable)


def file_scope(path):
    return os.path.relpath(path, 'sources')


def line_scope(path, measure):
    return f'{file_scope(path)}:{measure.line}'


def function_table(line, name, body, others):
    """Return the measures over limit for one function: body lines, then each (label, value) in others."""
    rows = [(f'{name} {label}', value, FUNCTION_LIMITS[label]) for label, value in others]
    return body_line_measures(line, name, body) + over_limits(line, rows)


def body_line_measures(line, name, body):
    if body > BODY_LINES:
        return [Measure(line, f'{name} body lines', body, BODY_LINES, waivable=False)]
    return over_limits(line, ((f'{name} body lines', body, BODY_LINES_NORM),))


def classes(source):
    for index, token in enumerate(source.tokens[:-1]):
        if token.value not in ('class', 'struct') or source.word(index - 1) == 'enum':
            continue
        cursor = class_body(source, index + 2)
        if cursor in source.pairs and source.word(cursor) == '{':
            yield ClassSpan(token.value, source.word(index + 1), token.line, index, cursor, source.pairs[cursor])


def class_body(source, cursor):
    inherited = False
    while cursor < len(source.tokens):
        word = source.word(cursor)
        if word in ('{', ';') or (word in ('>', ',', '*', '&', ')', '(', '=') and not inherited):
            return cursor
        inherited |= word == ':'
        cursor += 1
    return cursor


def members(source, start, end):
    cursor = start + 1
    first = cursor
    while cursor < end:
        word = source.word(cursor)
        if word in ACCESS and source.word(cursor + 1) == ':':
            yield Member(word, cursor, cursor + 1)
            cursor += 1
            first = cursor + 1
        elif word == ';':
            yield Member('', first, cursor)
            first = cursor + 1
        elif word == '{' and cursor in source.pairs and brace_kind(source, first, cursor) is Brace.FUNCTION:
            yield Member('', first, source.pairs[cursor] + 1)
            first = source.pairs[cursor] + 1
            cursor = source.pairs[cursor]
        elif cursor in source.pairs:
            cursor = source.pairs[cursor]
        cursor += 1


def declarator(source, start, end, name):
    """Return the function a declaration in [start, end) declares, or None."""
    start = template_header_end(source, start)
    if start >= end or source.word(start) in NOT_FUNCTIONS:
        return None
    cursor = start
    while cursor < end:
        word = source.word(cursor)
        if word in ('=', '{', ';') or (word == '(' and source.word(cursor + 1) in ('*', '&')):
            return None
        if (found := named_declarator(source, start, cursor, name)) is not None:
            return found
        cursor = source.skip(cursor) + 1
    return None


def template_header_end(source, start):
    """Return the index after a leading 'template <...>' header, whose '<' is an angle by grammar."""
    if source.words(start, start + 2) != ['template', '<']:
        return start
    cursor = start + 2
    while cursor < len(source.tokens) and source.word(cursor) != '>':
        cursor = source.skip(cursor) + 1
    return cursor + 1


def named_declarator(source, start, cursor, name):
    word = source.word(cursor)
    if word == 'operator':
        return operator_declarator(source, cursor)
    if word == '(' and cursor > start and source.word(cursor - 1) not in NOT_DECLARATORS:
        return call_declarator(source, start, cursor, name)
    return None


def operator_declarator(source, cursor):
    parameters = next((index for index in range(cursor + 2, len(source.tokens)) if source.word(index) == '('), None)
    if parameters is None:
        return None
    return Declarator('operator=' if source.word(cursor + 1) == '=' else 'operator', parameters)


def call_declarator(source, start, cursor, name):
    candidate = source.word(cursor - 1)
    if candidate == name and cursor - start > 1 and source.word(cursor - 2) == '~':
        return Declarator('~' + name, cursor)
    if cursor - 1 in source.angle_openers:
        return Declarator(source.word(source.angle_openers[cursor - 1] - 1), cursor)
    return Declarator(candidate, cursor)


def data_count(source, start, end):
    return 0 if start >= end or source.word(start) in NOT_DATA else top_level_items(source, start, end)


def top_level_items(source, start, end):
    """Count the comma-separated items in [start, end), brackets and template arguments included in one item."""
    count = 1
    cursor = start
    while cursor < end:
        count += source.word(cursor) == ','
        cursor = source.skip(cursor) + 1
    return count


def tally_members(source, item):
    tally = ClassTally('private' if item.kind == 'class' else 'public')
    for member in members(source, item.start, item.end):
        if member.access:
            tally.enter(member.access)
        elif (found := declarator(source, member.first, member.end, item.name)) is not None:
            tally.function(found.name, item.name)
            tally.generated += source.words(member.end - 2, member.end) in GENERATED
        elif (count := data_count(source, member.first, member.end)):
            tally.datum(count)
    return tally


def class_measures(source, fixtures):
    for item in classes(source):
        name, line = item.name, item.line
        tally = tally_members(source, item)
        if all(function == name for function in tally.functions):
            continue
        exposed = sum(tally.declarations[access] for access in exposure(source.path, name, fixtures).value)
        size = source.tokens[item.end].line - line + 1
        yield from over_limits(line, ((f'{name} lines',            size,                     CLASS_LINES),
                                      (f'{name} data members',     sum(tally.data.values()), DATA_MEMBERS),
                                      (f'{name} member functions', len(tally.functions),     MEMBER_FUNCTIONS),
                                      (f'{name} layout',           tally.layout + exposed,   LAYOUT)))


def header_measures(source):
    """Measure a header's classes with member functions, nested classes counted with their owner, and bodies."""
    if source.path.suffix != HEADER:
        return []
    items = list(classes(source))
    owners = [item for item in items if outermost(item, items)]
    behaving = sum(any(behaves(source, inner) for inner in items if owner.encloses(inner.start)) for owner in owners)
    heads = template_heads(source)
    plain = {item for item in items if item.keyword not in heads}
    bodies = collections.Counter(owner.name for function in definitions(source, 0, len(source.tokens))
                                 if (owner := body_owner(source, function, items, plain)) is not None)
    return over_limits(1, (('classes with member functions', behaving, HEADER_CLASSES),
                           *((f'{name} bodies in header', count, HEADER_BODIES) for name, count in bodies.items())))


def outermost(item, items):
    return not any(other.encloses(item.start) for other in items if other is not item)


def behaves(source, item):
    """A class behaves when it declares a member function that is not defaulted or deleted."""
    tally = tally_members(source, item)
    return len(tally.functions) > tally.generated


def body_owner(source, function, items, plain):
    """Return the outermost class owning a body that is neither a template nor constexpr, else None."""
    enclosing = [item for item in items if item.encloses(function.first)] or qualifiers(function, items)
    if exempt(source, function) or not all(item in plain for item in enclosing):
        return None
    return next((item for item in enclosing if outermost(item, items)), None)


def qualifiers(function, items):
    """Return the outermost class an out-of-class definition names as its qualifier."""
    names = function.name.split('::')[:-1]
    return [item for item in items if item.name in names and outermost(item, items)][:1]


def exempt(source, function):
    """A function template, abbreviated ones included, or a constexpr or consteval function."""
    parameters = source.words(function.parameters + 1, source.pairs[function.parameters])
    return (generic(source, function.first) or 'auto' in parameters
            or not EVALUATED.isdisjoint(source.words(function.first, function.parameters)))


def template_heads(source):
    """Return the index that follows each template header with parameters."""
    return {template_header_end(source, index) for index in range(len(source.tokens)) if generic(source, index)}


def generic(source, index):
    return source.words(index, index + 2) == ['template', '<'] and source.word(index + 2) != '>'


def exposure(path, name, fixtures):
    if (path.as_posix(), name) in PUBLIC_DATA_CLASSES:
        return Exposure.OPEN
    return Exposure.FIXTURE if name in fixtures else Exposure.STRICT


def definitions(source, start, end):
    """Yield every function defined outside another function body."""
    first = start
    cursor = start
    while cursor < end:
        word = source.word(cursor)
        if word == ';':
            first = cursor + 1
        elif word in ACCESS and source.word(cursor + 1) == ':':
            cursor += 1
            first = cursor + 1
        elif word == '{' and cursor in source.pairs:
            kind = brace_kind(source, first, cursor)
            yield from brace_definitions(source, kind, first, cursor)
            first = definition_end(source, kind, cursor) + 1 if kind in DEFINITIONS else first
            cursor = definition_end(source, kind, cursor)
        else:
            cursor = source.skip(cursor)
        cursor += 1


def brace_definitions(source, kind, first, opening):
    if kind is Brace.FUNCTION:
        yield function_definition(source, first, opening)
    elif kind is Brace.SCOPE:
        yield from definitions(source, opening + 1, source.pairs[opening])


def definition_end(source, kind, opening):
    if kind is Brace.FUNCTION and source.word(opening - 1) == 'try':
        return handlers_end(source, opening)
    return source.pairs[opening]


def brace_kind(source, first, opening):
    if declarator(source, first, opening, '') is None:
        return Brace.SCOPE if SCOPES & set(source.words(first, opening)) else Brace.VALUE
    return Brace.INITIALISER if initialises_member(source, opening) else Brace.FUNCTION


def initialises_member(source, opening):
    return source.word(source.pairs[opening] + 1) in (',', '{')


def function_definition(source, first, opening):
    parameters = declarator(source, first, opening, '').parameters
    name = key_name(source, first, parameters)
    line = source.tokens[parameters].line
    if source.word(opening - 1) == 'try':
        closing = handlers_end(source, opening)
        return Function(name, line, first, parameters, opening, closing, opening - 1, closing + 1)
    closing = source.pairs[opening]
    return Function(name, line, first, parameters, opening, closing, opening + 1, closing)


def handlers_end(source, opening):
    cursor = source.pairs[opening]
    while source.word(cursor + 1) == 'catch':
        cursor = source.pairs[source.pairs[cursor + 2] + 1]
    return cursor


def key_name(source, first, parameters):
    """Return the name as written before the parameter list; a gtest case is Suite.Name."""
    if source.word(parameters - 1) in GTEST_MACROS:
        return '.'.join(word for word in source.words(parameters + 1, source.pairs[parameters]) if word != ',')
    operators = [index for index in range(first, parameters) if source.word(index) == 'operator']
    start = operators[-1] if operators else source.angle_openers.get(parameters - 1, parameters) - 1
    while start - first > 1 and source.word(start - 1) == '::' and source.word(start - 2)[:1].isidentifier():
        start -= 2
    return ''.join(source.words(start, parameters))


def lambdas(source, functions):
    """Return every lambda, named by its enclosing function and its line."""
    found = [function for index in range(len(source.tokens)) if (function := lambda_at(source, index))]
    return [function._replace(name=lambda_name(function, functions)) for function in found]


def lambda_name(function, functions):
    enclosing = next((other.name for other in functions if other.start <= function.first < other.end), '')
    return f'{enclosing} lambda@{function.line}'.lstrip()


def lambda_at(source, index):
    if not lambda_introducer(source, index):
        return None
    cursor = source.pairs[index] + 1
    cursor = source.angles[cursor] + 1 if cursor in source.angles else cursor
    parameters = cursor if source.word(cursor) == '(' else None
    opening = lambda_body(source, cursor)
    if opening is None:
        return None
    closing = source.pairs[opening]
    return Function('', source.tokens[index].line, index, parameters, opening, closing, opening + 1, closing)


def lambda_introducer(source, index):
    previous = source.word(index - 1)
    if source.word(index) != '[' or index not in source.pairs or '[' in (previous, source.word(index + 1)):
        return False
    subscripted = previous[:1].isalnum() or previous[:1] == '_' or previous[-1:] in SUBSCRIPTED_ENDINGS
    return not subscripted or previous in LAMBDA_AFTER_WORDS


def lambda_body(source, cursor):
    while cursor < len(source.tokens) and source.word(cursor) not in LAMBDA_STOPS:
        if source.word(cursor) == '{':
            return cursor if cursor in source.pairs else None
        cursor = source.skip(cursor) + 1
    return None


def parameter_count(source, opening):
    if opening is None or source.words(opening + 1, source.pairs[opening]) in ([], ['void']):
        return 0
    return top_level_items(source, opening + 1, source.pairs[opening])


def body_lines(source, function):
    first = source.tokens[function.opening].line
    lines = {number for token in source.tokens[function.start:function.end] for number in token.lines}
    return len({number for number in lines if number > first})


def own_indices(function, nested):
    excluded = {index for other in nested for index in range(other.first, other.closing + 1)}
    return [index for index in range(function.start, function.end) if index not in excluded]


def complexity(source, function, nested):
    return 1 + sum(branches(source, index) for index in own_indices(function, nested))


def branches(source, index):
    word = source.word(index)
    return word in BRANCHES and not (word == '&&' and is_reference(source, index))


def is_reference(source, index):
    """A '&&' between a type written at a statement start and a declared name is a reference."""
    named = source.word(index + 1)[:1].isidentifier() and source.word(index + 2) in REFERENCE_FOLLOWERS
    return (named or source.word(index + 1) == '[') and declaration_start(source, index - 1)


def declaration_start(source, cursor):
    consumed = False
    while cursor >= 0:
        word = source.word(cursor)
        if word == ')' and source.word(source.openers.get(cursor, 0) - 1) == 'decltype':
            cursor = source.openers[cursor] - 2
        elif cursor in source.angle_openers:
            cursor = source.angle_openers[cursor] - 1
        elif word == '::' or (word[:1].isidentifier() and word not in EXPRESSION_KEYWORDS):
            cursor -= 1
        else:
            break
        consumed = True
    return consumed and (cursor < 0 or source.word(cursor) in STATEMENT_BOUNDARIES
                         or source.word(cursor - 1) == 'for')


def depth(controls):
    return max((1 + depth(control.children) for control in controls), default=0)


def parse_block(source, cursor, end):
    """Return the control statements of the statements in [cursor, end)."""
    controls = []
    while cursor < end:
        found, cursor = parse_statement(source, cursor, end)
        controls += found
    return controls


def parse_statement(source, cursor, end):
    while source.word(cursor) == '[' and source.word(cursor + 1) == '[':
        cursor = source.pairs[cursor] + 1
    word = source.word(cursor)
    if word in CONTROLS:
        return parse_control(source, cursor, end)
    if word == '{':
        return parse_block(source, cursor + 1, source.pairs[cursor]), source.pairs[cursor] + 1
    if word == 'try':
        return parse_handlers(source, cursor + 1, end)
    if word in ('case', 'default'):
        return [], next(index for index in range(cursor, end) if source.word(index) == ':') + 1
    return [], expression_end(source, cursor, end)


def parse_control(source, cursor, end):
    word = source.word(cursor)
    if word == 'do':
        body, cursor = parse_statement(source, cursor + 1, end)
        return [Control(tuple(body))], condition_end(source, cursor + 1) + 1
    body, cursor = parse_statement(source, condition_end(source, cursor + 1), end)
    if word != 'if' or cursor >= end or source.word(cursor) != 'else':
        return [Control(tuple(body))], cursor
    alternative, following = parse_statement(source, cursor + 1, end)
    if source.word(cursor + 1) == 'if':
        return [Control(tuple(body))] + alternative, following
    return [Control(tuple(body + alternative))], following


def parse_handlers(source, cursor, end):
    controls, cursor = parse_statement(source, cursor, end)
    while cursor < end and source.word(cursor) == 'catch':
        handler, cursor = parse_statement(source, source.pairs[cursor + 1] + 1, end)
        controls += handler
    return controls, cursor


def condition_end(source, cursor):
    while source.word(cursor) in CONDITION_PREFIXES:
        cursor += 1
    return source.pairs[cursor] + 1 if source.word(cursor) == '(' else cursor


def expression_end(source, cursor, end):
    while cursor < end and source.word(cursor) != ';':
        cursor = source.pairs.get(cursor, cursor) + 1
    return cursor + 1


def function_measures(source, function, nested):
    return function_table(function.line, function.name, body_lines(source, function),
                          (('complexity', complexity(source, function, nested)),
                           ('parameters', parameter_count(source, function.parameters)),
                           ('nesting',    depth(parse_block(source, function.start, function.end)))))


def functions_measures(source):
    functions = list(definitions(source, 0, len(source.tokens)))
    closures = lambdas(source, functions)
    return [measure for function in functions + closures
            for measure in function_measures(source, function, nested_in(function, closures))]


def nested_in(function, closures):
    return [other for other in closures if function.start <= other.first and other.closing < function.end]


def long_lines(lines, literals):
    return [number for number, text in enumerate(lines, 1) if width(text, literals.get(number, ())) > COLUMNS]


def width(text, literals):
    """Return the columns a line uses when string literals count only up to the limit."""
    used = 0
    position = 0
    for begin, end in sorted(literals):
        used += begin - position
        used += min(end - begin, max(0, COLUMNS - used))
        position = end
    return used + len(text) - position


def long_line_measures(lines, literals):
    overlong = long_lines(lines, literals)
    return over_limits(overlong[0] if overlong else 1, (('long lines', len(overlong), LONG_LINES),))


def compound_contracts(source):
    return [source.tokens[index].line for index in range(len(source.tokens) - 1)
            if source.word(index) in CONTRACTS and index + 1 in source.pairs
            and LOGICAL & set(first_argument(source, index + 1))]


def first_argument(source, opening):
    """Yield the words of a call's first argument outside the lambdas within it."""
    cursor = opening + 1
    end = first_argument_end(source, opening)
    while cursor < end:
        closure = lambda_at(source, cursor)
        if closure is None:
            yield source.word(cursor)
        cursor = (closure.closing if closure else cursor) + 1


def first_argument_end(source, opening):
    cursor = opening + 1
    while cursor < source.pairs[opening] and source.word(cursor) != ',':
        cursor = source.skip(cursor) + 1
    return min(cursor, source.pairs[opening])


def tree_findings(sources):
    measured = [source for source in sources if source.path.as_posix() not in FROZEN]
    nolint = sum('NOLINT' in line for source in measured for line in source.lines)
    contracts = sum(len(compound_contracts(source)) for source in measured)
    c_files = sum(source.path.suffix == '.c' for source in measured)
    totals = (('NOLINT lines',       nolint,    NOLINT_LINES),
              ('compound contracts', contracts, COMPOUND_CONTRACTS),
              ('c files',            c_files,   C_FILES))
    return [finding(file_scope('sources'), measure) for measure in over_limits(1, totals)]


def file_measures(source):
    percent = 100 * len(source.comment_lines) / source.nonblank if source.nonblank else 0
    return over_limits(1, (('file lines',      source.nonblank, FILE_LINES),
                           ('comment percent', percent,         COMMENT_PERCENT)))


def unbalanced_finding(source):
    word = source.word(source.unmatched[0])
    label = f'unbalanced {BRACKET_NAMES[BRACKETS.get(word, word)]}'
    return Finding(f'{file_scope(source.path)}: {label}')


def source_findings(source, fixtures=frozenset()):
    path = source.path
    shape = file_measures(source) + list(class_measures(source, fixtures)) + header_measures(source)
    if path.as_posix() in FROZEN:
        return [finding(file_scope(path), measure) for measure in shape]
    shape += long_line_measures(source.lines, source.literal_columns)
    if source.unmatched:
        return [finding(file_scope(path), measure) for measure in shape] + [unbalanced_finding(source)]
    return ([finding(file_scope(path), measure) for measure in shape]
            + [finding(line_scope(path, measure), measure) for measure in functions_measures(source)])


def python_findings(path):
    lines = path.read_text().splitlines()
    functions = [node for node in ast.walk(ast.parse('\n'.join(lines)))
                 if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))]
    measured = [measure for node in functions for measure in python_function_measures(node, lines)]
    return ([finding(file_scope(path), measure) for measure in long_line_measures(lines, {})]
            + [finding(line_scope(path, measure), measure) for measure in measured])


def python_function_measures(node, lines):
    return function_table(node.lineno, node.name, python_body_lines(node, lines),
                          (('parameters', python_parameters(node.args)),
                           ('nesting',    max(python_nesting(node, 0, lines), default=0))))


def python_body_lines(node, lines):
    body = lines[node.body[0].lineno - 1:node.end_lineno]
    return sum(bool(text.strip()) and not text.strip().startswith('#') for text in body)


def python_parameters(arguments):
    positional = [argument.arg for argument in arguments.posonlyargs + arguments.args]
    explicit = positional[1:] if positional[:1] and positional[0] in RECEIVERS else positional
    return len(explicit) + len(arguments.kwonlyargs) + bool(arguments.vararg) + bool(arguments.kwarg)


def python_nesting(node, level, lines):
    """Yield the depth of every control statement below node, an elif keeping its if's depth."""
    for child in ast.iter_child_nodes(node):
        if isinstance(child, PYTHON_SCOPES):
            continue
        inner = level
        if isinstance(child, PYTHON_CONTROLS):
            inner = level if lines[child.lineno - 1].lstrip().startswith('elif') else level + 1
            yield inner
        yield from python_nesting(child, inner, lines)


def check_allow(findings, allow):
    entries = allow_entries(allow)
    reports = [finding.key for finding in findings if not finding.waivable or finding.key not in entries]
    reports += allow_errors(allow, entries, {finding.key for finding in findings})
    for report in reports:
        print(report)
    return int(bool(reports))


def allow_errors(allow, entries, current):
    """Report entries without a reason, entries for a limit no entry may waive, and stale entries."""
    return [f'{allow}:{entry.line}: {problem}: {key}'
            for key, entry in sorted(entries.items(), key=lambda item: item[1].line)
            for problem, present in (('allow entry without a reason', not entry.reason),
                                     ('allow entry for a hard limit', HARD_ENTRY.search(key)),
                                     ('stale allow entry',            key not in current))
            if present]


def allow_entries(allow):
    """Map each allow entry to its line in the allow file and its reason."""
    if not allow:
        return {}
    parsed = [(number, *line.partition('#')) for number, line in enumerate(allow.read_text().splitlines(), 1)]
    return {entry.strip(): AllowEntry(number, reason.strip()) for number, entry, _, reason in parsed if entry.strip()}


def fixture_classes(sources):
    bases = {}
    helpers = set()
    for source in sources:
        declared = [(item.name, base_names(source, item.start)) for item in classes(source)]
        for name, parents in declared:
            bases.setdefault(name, set()).update(parents)
        if source.path.name.startswith('test-') or source.path.name.endswith('.test.cpp'):
            helpers.update(name for name, _ in declared)
    return fixture_closure(bases, helpers)


def base_names(source, start):
    prefix = []
    cursor = start - 1
    while cursor >= 0 and source.word(cursor) not in ('class', 'struct'):
        prefix.append(source.word(cursor))
        cursor -= 1
    return prefix[:prefix.index(':')] if ':' in prefix else []


def fixture_closure(bases, helpers):
    fixtures = set(FIXTURE_ROOTS)
    while True:
        derived = {name for name, parents in bases.items() if parents & fixtures}
        mixins = {parent for name in fixtures for parent in bases.get(name, ()) if parent in helpers}
        additional = (derived | mixins) - fixtures
        if not additional:
            return fixtures
        fixtures.update(additional)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow', type=pathlib.Path)
    args = parser.parse_args()
    sources = [Source(path) for path in sorted(pathlib.Path('sources').rglob('*'))
               if path.suffix in EXTENSIONS and path.is_file()]
    fixtures = fixture_classes(sources)
    findings = [finding for source in sources for finding in source_findings(source, fixtures)]
    findings += tree_findings(sources)
    findings += [finding for path in sorted(pathlib.Path('bin').glob('*.py')) for finding in python_findings(path)]
    return check_allow(findings, args.allow)


if __name__ == '__main__':
    sys.exit(main())
