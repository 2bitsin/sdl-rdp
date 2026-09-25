#!/usr/bin/env python3
"""Refuse a contract condition that can change any state, by a call, an operator or a macro; no allow list."""
import collections
import pathlib
import re
import sys
from typing import NamedTuple

import shape
import spellings

ROOT          = pathlib.Path(__file__).resolve().parents[2]
# Evaluated by the language, no user code; `bool(x)` and the casts rest on every `operator bool` being const noexcept.
OPERATORS     = frozenset(('sizeof', 'alignof', 'noexcept', 'decltype', 'static_cast', 'const_cast',
                           'reinterpret_cast', 'bool'))
# Const member queries of the standard types; a project declaration of the same name is judged on its own.
STD_MEMBERS   = frozenset(('size', 'empty', 'count', 'has_value', 'data', 'contains', 'find', 'begin', 'end',
                           'joinable', 'mutex', 'load', 'first', 'last', 'subspan'))
# Standard functions that only read their arguments, by the arities that do; `error_code&` overloads write.
STD_FREE      = {name: None for name in ('any_of', 'all_of', 'none_of', 'contains', 'find', 'find_if', 'count',
                                         'count_if', 'cmp_equal', 'cmp_not_equal', 'cmp_less', 'cmp_greater',
                                         'cmp_less_equal', 'cmp_greater_equal', 'in_range', 'holds_alternative',
                                         'size', 'empty', 'span', 'zero', 'to_underlying', 'string_view')}
STD_FREE     |= {name: 1 for name in ('exists', 'is_regular_file', 'is_directory')}
NOT_CALLS     = frozenset(('return', 'co_return', 'throw', 'case', 'not', 'and', 'or', 'mutable'))
# A parameter of these kinds can reach state the caller sees whatever its own qualifiers say.
REACHING      = frozenset(('unique_ptr', 'shared_ptr', 'weak_ptr', 'reference_wrapper', 'function',
                           'move_only_function', 'function_ref', 'invocable', 'regular_invocable', 'predicate',
                           'HANDLE'))
VIEWS         = frozenset(('span', 'mdspan'))
INDIRECTIONS  = frozenset(('&', '&&', '*'))
ARITHMETIC    = frozenset(('integral', 'signed_integral', 'unsigned_integral', 'floating_point'))
OPERATOR_TEXT = ('<<=', '>>=', '<=>', '++', '--', '==', '!=', '<=', '>=', '<<', '>>', '+=', '-=', '*=', '/=', '%=',
                 '&=', '|=', '^=')
PUNCTUATION   = frozenset('+-*/%&|^<>=!')
WRITES        = frozenset(('++', '--', '=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>=', 'new',
                           'delete'))
ALIAS         = re.compile(r'\b(?:namespace|using)\s+(\w+)\s*=\s*std\s*::')
OWNING        = re.compile(r'\busing\s+(\w+)\s*=\s*std\s*::\s*(?:unique_ptr|shared_ptr)\b')
DEFINE        = re.compile(r'^[ \t]*#[ \t]*define[ \t]+(\w+)', re.M)
INCLUDE       = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
STD           = 'std'
NAME          = re.compile(r'[A-Za-z_]\w*')


class Declaration(NamedTuple):
    """One `auto Name(...)` the project writes: whether it can change what its caller sees, and what calls it."""
    pure:       bool
    parameters: tuple
    owner:      str
    returned:   str

    def accessor(self):
        """The non-const half of an accessor pair: it returns `T&` and writes through no parameter."""
        return self.returned == 'reference' and self.owner != ''

    def admits(self, arguments):
        """A call with this many arguments can reach it: defaults and a pack widen the count."""
        required = sum('=' not in parameter and '...' not in ''.join(parameter) for parameter in self.parameters)
        packed   = any('...' in ''.join(parameter) for parameter in self.parameters)
        return required <= arguments and (packed or arguments <= len(self.parameters))


class Finding(NamedTuple):
    path:    pathlib.Path
    line:    int
    subject: str
    reason:  str

    def report(self):
        return f'{self.path}:{self.line}: `{self.subject}` {self.reason}'


def element_const(words, view):
    """The element type of the view at `view` in a parameter's words is const."""
    depth, cursor = 0, view + 1
    while cursor < len(words):
        depth  += {'<': 1, '>': -1}.get(words[cursor], 0)
        if depth == 0:
            return 'const' in words[view + 1:cursor]
        cursor += 1
    return False


def caller_typed(words):
    """An `auto` no arithmetic concept fixes: a range or view the caller picks can write through (F.16, Con.3)."""
    return any(word == 'auto' and (index == 0 or words[index - 1] not in ARITHMETIC)
               for index, word in enumerate(words))


def mutable_parameter(words, owning, generic):
    """A parameter the callee can write through: a view of mutable elements, an owning, callable or wrapped handle,
    a type the caller chooses, or a reference or pointer whose referee is not const."""
    words = words[:words.index('=')] if '=' in words else words
    if any(word in REACHING or word in owning or word in generic or word.endswith('Handle') for word in words):
        return True
    if caller_typed(words):
        return True
    if (view := next((index for index, word in enumerate(words) if word in VIEWS), None)) is not None:
        return not element_const(words, view)
    indirect = next((index for index, word in enumerate(words) if word in INDIRECTIONS), None)
    return indirect is not None and 'const' not in (words[0], words[indirect - 1])


def parameters(source, opening):
    words, found = source.words(opening + 1, source.pairs[opening]), []
    cursor, start = opening + 1, opening + 1
    while cursor <= source.pairs[opening]:
        if source.word(cursor) == ',' or cursor == source.pairs[opening]:
            found.append(tuple(source.words(start, cursor)))
            start = cursor + 1
        cursor = source.skip(cursor) + 1
    return tuple(found) if words else ()


def qualifiers_end(source, closing):
    cursor = closing + 1
    while source.word(cursor) not in ('->', ';', '{', '=', ''):
        cursor = source.skip(cursor) + 1
    return cursor


def qualifiers(source, closing):
    """The words between a parameter list and its `->`, `;`, `{` or `=`."""
    return set(source.words(closing + 1, qualifiers_end(source, closing)))


def returned(source, closing):
    """What a declaration hands back: `void`, a `reference` (a non-const reference, pointer or reference_wrapper),
    a `const reference`, or a `value`."""
    arrow = qualifiers_end(source, closing)
    if source.word(arrow) != '->':
        return 'value'
    cursor = arrow + 1
    while source.word(cursor) not in ('{', ';', '=', 'requires', ''):
        cursor = source.skip(cursor) + 1
    words = source.words(arrow + 1, cursor)
    if words == ['void']:
        return 'void'
    if 'reference_wrapper' in words:
        return 'reference'
    indirection = words[:-1] if words[-2:] == ['*', 'const'] else words
    if indirection[-1:] in (['&'], ['&&'], ['*']) or 'decltype' in words:
        return 'const reference' if indirection[-2:-1] == ['const'] else 'reference'
    return 'value'


def template_opening(source, before):
    """The `<` of the template head whose `>` is `before`, or None; a head's angles are not written flush."""
    depth, cursor = 0, before
    while cursor >= 0 and source.word(before) == '>':
        depth += {'>': 1, '<': -1}.get(source.word(cursor), 0)
        if depth == 0:
            return cursor if source.word(cursor - 1) == 'template' else None
        cursor = source.openers[cursor] - 1 if source.word(cursor) in (')', ']', '}') else cursor - 1
    return None


def template_names(source, before):
    """The parameter names of the template head that ends at `before`, if one does."""
    opening = template_opening(source, before)
    if opening is None:
        return set()
    names, depth = set(), 0
    for cursor in range(opening + 1, before + 1):
        word   = source.word(cursor)
        depth += {'<': 1, '>': -1}.get(word, 0) if cursor != before else 0
        if (cursor == before or (depth == 0 and word == ',')) and source.word(cursor - 1) != '<':
            names.add(source.word(cursor - 1))
    return names


def head_end(source, index):
    """The token before a declaration's specifiers and attributes."""
    cursor = index - 1
    while source.word(cursor) in shape.SPECIFIERS or source.word(cursor) == ']':
        cursor = source.openers[cursor] - 1 if source.word(cursor) == ']' else cursor - 1
    return cursor


def generic_names(source, index, bodies):
    """The template parameters a declaration's types may name: its own head's and its enclosing classes'."""
    names = template_names(source, head_end(source, index))
    for body in bodies:
        if body.encloses(index):
            names |= template_names(source, head_end(source, body.keyword))
    return names


def specifiers(source, index):
    """The specifiers written before the `auto` at index."""
    found = set()
    while source.word(index - 1) in shape.SPECIFIERS:
        index -= 1
        found.add(source.word(index))
    return found


def declared_name(source, index):
    """The name an `auto [Class::]Name(` at index declares, with its `(`; none for anything else."""
    cursor = index + 1
    while source.word(cursor + 1) == '::':
        cursor += 2
    if not NAME.fullmatch(source.word(cursor)) or source.word(cursor + 1) != '(':
        return None
    return source.word(cursor), cursor + 1


def owner(source, name_index, bodies):
    """The class a declaration belongs to: its `Class::` qualifier, else the innermost class body around it."""
    if source.word(name_index - 1) == '::':
        before = name_index - 2
        return source.word(source.angle_openers[before] - 1 if before in source.angle_openers else before)
    around = [body for body in bodies if body.encloses(name_index)]
    return max(around, key=lambda body: body.start).name if around else ''


def declaration(source, index, opening, context):
    bodies, owning = context
    listed         = parameters(source, opening)
    generic        = generic_names(source, index, bodies)
    constant       = 'const' in qualifiers(source, source.pairs[opening])
    immediate      = 'consteval' in specifiers(source, index)
    handed         = returned(source, source.pairs[opening])
    writes         = any(mutable_parameter(parameter, owning, generic) for parameter in listed)
    pure           = (constant or immediate) and not writes and handed not in ('void', 'reference')
    return Declaration(pure, listed, owner(source, opening - 1, bodies), handed)


def file_declarations(source, owning):
    """Each declared name with its declaration and the class that owns it, '' for a free function."""
    context = (list(shape.classes(source)), owning)
    for index, token in enumerate(source.tokens):
        found = declared_name(source, index) if token.value == 'auto' else None
        if found and found[1] in source.pairs:
            yield found[0], declaration(source, index, found[1], context)


def visible(relative, sources, seen=None):
    """The file, its header or source twin and every project header they include, transitively."""
    seen    = set() if seen is None else seen
    pending = [relative, *(twin for suffix in ('.hpp', '.cpp') if (twin := relative.with_suffix(suffix)) in sources)]
    while pending:
        current = pending.pop()
        if current in seen or current not in sources:
            continue
        seen.add(current)
        pending += included(current, sources)
    return seen


def included(relative, sources):
    text = '\n'.join(sources[relative].lines)
    for name in INCLUDE.findall(text):
        yield from (path for path in (pathlib.Path('sources', name), relative.parent / name) if path in sources)


def reachable(declared, arguments, member):
    """The declarations a call can reach: members only through `.` or `->`, and only those its arity admits."""
    return [found for found in declared if found.admits(arguments) and (found.owner or not member)]


def pure_declarations(reached):
    """Every reachable declaration is pure, or the `T&` half of an accessor pair whose `const` half is pure."""
    twins = {(found.owner, found.parameters) for found in reached if found.pure and found.owner}
    return bool(reached) and all(found.pure or (found.accessor() and (found.owner, found.parameters) in twins)
                                 for found in reached)


def mutable_classes(source):
    """The classes that declare a `mutable` member, whose `const` members may write."""
    for body in shape.classes(source):
        if any(source.word(index) == 'mutable' and source.word(index - 1) != ')'
               for index in range(body.start, body.end)):
            yield body.name


def arguments(source, opening):
    closing = source.pairs.get(opening, opening)
    if closing == opening + 1:
        return 0
    cursor, count = opening + 1, 1
    while cursor < closing:
        count  += source.word(cursor) == ','
        cursor  = source.skip(cursor) + 1
    return count


def lambda_parameters(source, before):
    """The `(` after `before` opens a lambda's parameters: `before` closes a lambda introducer."""
    opening = source.openers.get(before)
    return opening is not None and shape.lambda_introducer(source, opening)


def callee(source, opening):
    """The name a `(` calls, through template arguments; '' for a call on an expression, None for grouping."""
    before = opening - 1
    if before in source.angle_openers:
        before = source.angle_openers[before] - 1
    word = source.word(before)
    if word in NOT_CALLS:
        return None
    if NAME.fullmatch(word):
        return before
    if word in (')', '}') or (word == ']' and not lambda_parameters(source, before)):
        return ''
    return None


def qualifier(source, index, aliases):
    """The first segment of the qualification before a name, an alias of `std` read as `std`; '' for none."""
    first = None
    while source.word(index - 1) == '::' and NAME.fullmatch(source.word(index - 2)):
        index -= 2
        first  = source.word(index)
    return STD if first in aliases else (first or '')


def pure_call(source, index, opening, scope):
    declared, aliases, _ = scope
    name, member         = source.word(index), source.word(index - 1) in ('.', '->')
    count                = arguments(source, opening)
    if name in OPERATORS:
        return True
    if not member and qualifier(source, index, aliases) == STD:
        return name in STD_FREE and STD_FREE[name] in (None, count)
    reached = reachable(declared.get(name, ()), count, member)
    return pure_declarations(reached) if reached else member and name in STD_MEMBERS


def operator_run(source, cursor, end):
    """The operators spelled by the punctuation tokens written flush from `cursor`, by maximal munch."""
    last = cursor
    while (last + 1 < end and source.word(last + 1) in PUNCTUATION and last + 1 not in source.angle_openers
           and source.tokens[last + 1].offset == source.tokens[last].offset + 1):
        last += 1
    text, found = ''.join(source.words(cursor, last + 1)), []
    while text:
        spelled = next((operator for operator in OPERATOR_TEXT if text.startswith(operator)), text[0])
        found.append(spelled)
        text = text[len(spelled):]
    return found, last + 1


def introducers(source, start, end):
    """The token indices inside lambda introducers, where `=` is a capture default, not an assignment."""
    opened = (index for index in range(start, end) if source.word(index) == '[')
    return {inside for index in opened if shape.lambda_introducer(source, index)
            for inside in range(index, source.pairs[index] + 1)}


def writes(source, start, end):
    """Each operator in the range that writes: increments, assignments, `new` and `delete`."""
    captured, cursor = introducers(source, start, end), start
    while cursor < end:
        word = source.word(cursor)
        if word in PUNCTUATION and cursor not in source.angles and cursor not in source.angle_openers:
            spelled, after = operator_run(source, cursor, end)
            yield from ((cursor, operator) for operator in spelled if operator in WRITES and cursor not in captured)
            cursor = after
            continue
        if word in ('new', 'delete'):
            yield cursor, word
        cursor += 1


def condition_findings(source, opening, scope):
    """What a contract's condition does that the lint cannot show leaves every state as it was."""
    end, macros = shape.first_argument_end(source, opening), scope[2]
    for cursor in range(opening + 1, end):
        index = callee(source, cursor) if source.word(cursor) == '(' else None
        if index == '':
            yield cursor, '(', 'calls an expression, not a name; hoist it'
        elif index is not None and not pure_call(source, index, cursor, scope):
            yield index, source.word(index), 'is not known pure; hoist it'
        if source.word(cursor) in macros:
            yield cursor, source.word(cursor), 'is a project macro; hoist it'
    yield from ((index, operator, 'writes; hoist it') for index, operator in writes(source, opening + 1, end))


def contracts(source):
    for index, token in enumerate(source.tokens):
        if token.value in shape.CONTRACTS and source.word(index + 1) == '(' and source.word(index - 1) != 'auto':
            yield index


def conversions(source):
    """Every `operator bool` declared without `const noexcept`, which the accepted casts rely on."""
    declared = (index for index, token in enumerate(source.tokens)
                if token.value == 'operator' and source.words(index + 1, index + 3) == ['bool', '('])
    yield from (index for index in declared if not {'const', 'noexcept'} <= qualifiers(source, source.pairs[index + 2]))


class Tree:
    """Every checked source lexed once, with its declarations, `std` aliases and macros."""

    def __init__(self, root):
        self.sources      = {relative: shape.Source(root / relative) for relative in spellings.checked(root)}
        texts             = {relative: '\n'.join(source.lines) for relative, source in self.sources.items()}
        owning            = set().union(*(OWNING.findall(text) for text in texts.values()))
        self.macros       = set().union(*(DEFINE.findall(text) for text in texts.values()))
        mutating          = set().union(*(mutable_classes(source) for source in self.sources.values()))
        self.declarations = {relative: [(name, found._replace(pure=found.pure and found.owner not in mutating))
                                        for name, found in file_declarations(source, owning)]
                             for relative, source in self.sources.items()}
        self.aliases      = {relative: set(ALIAS.findall(text)) for relative, text in texts.items()}

    def scope(self, relative):
        """The declarations by name, the `std` aliases and the project macros a file sees."""
        seen, declared = visible(relative, self.sources), collections.defaultdict(list)
        for name, found in (item for path in seen for item in self.declarations[path]):
            declared[name].append(found)
        return declared, {STD}.union(*(self.aliases[path] for path in seen)), self.macros


def file_findings(tree, relative):
    source = tree.sources[relative]
    starts = list(contracts(source))
    scope  = tree.scope(relative) if starts else ({}, set(), set())
    for index in starts:
        for at, subject, reason in condition_findings(source, index + 1, scope):
            yield Finding(relative, source.tokens[at].line, subject, f'in {source.word(index)} {reason}')
    for index in conversions(source):
        yield Finding(relative, source.tokens[index].line, 'operator bool', 'is not const noexcept')


def findings(root):
    tree = Tree(root)
    for relative in tree.sources:
        yield from file_findings(tree, relative)


def main():
    found = list(findings(ROOT))
    for finding in found:
        print(finding.report())
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
