#!/usr/bin/env python3
"""Hold every file to its path: declarations in `<folder>::detail::<stem>`, a header re-exporting once, last."""
import pathlib
import re
import sys
from typing import NamedTuple

import shape
import spellings

SOURCES  = pathlib.Path('sources')
DRIVER   = ('sdl-rdp', 'SDL3')
FORWARD  = 'forward.hpp'
SKIPPED  = ('//', '/*', "'", '#')
# The C++ runtime's entry and allocation hooks, glibc's allocator and oxbox's ADL hook are named globally.
RUNTIME  = re.compile(r'main|operator(?:new|delete)|__libc_\w+|reflect_scheme')
# The opaque driver-data tags SDL's internal headers leave a driver to define.
SDL_TAGS = frozenset(('SDL_VideoData', 'SDL_CursorData', 'SDL_PrivateAudioData'))
LEADERS  = shape.EXPRESSION_KEYWORDS | shape.SPECIFIERS | {'using', 'typename', 'auto', 'const'}
KEYS     = frozenset(('class', 'struct', 'union', 'enum', 'concept'))
SKIPS    = frozenset(('friend', 'static_assert', 'using', 'namespace', 'extern'))
OPENERS  = {'(': ')', '[': ']', '{': '}'}
PAIRS    = {'<': '>', '(': ')', '[': ']'}
STOPS    = frozenset(('(', '=', '{', ';', '[', ','))


class Finding(NamedTuple):
    path:    pathlib.Path
    line:    int
    message: str


class Block(NamedTuple):
    name:   str
    line:   int
    tokens: list


def segment(part):
    return part.lower().replace('-', '_').replace('.', '_')


def folder_namespace(relative):
    """The folder path under sources/; the SDL driver's tree is SDL's own `sdl3`."""
    parts = relative.relative_to(SOURCES).parent.parts
    if parts[:2] == DRIVER:
        parts = ('sdl3', *parts[2:])
    return '::'.join(segment(part) for part in parts)


def detail_namespace(relative):
    return f'{folder_namespace(relative)}::detail::{segment(relative.name.split(".")[0])}'


def code(text):
    return [token for token in shape.enabled_lexemes(text) if not token.value.lstrip().startswith(SKIPPED)]


def closing(tokens, index):
    """The index of the bracket that closes the one at index."""
    depth = 0
    for position in range(index, len(tokens)):
        depth += (tokens[position].value in OPENERS) - (tokens[position].value in OPENERS.values())
        if depth == 0:
            return position
    return len(tokens) - 1


def ends_body(tokens, index):
    """Whether a `{ }` closing just before index ends its statement: no `;`, `,`, `)` or next body follows."""
    return index >= len(tokens) or tokens[index].value not in (';', ',', ')', '{')


def statement_end(tokens, index):
    """The index after the statement starting at index: through its `;`, or through a body not followed by one."""
    while index < len(tokens) and tokens[index].value != ';':
        value = tokens[index].value
        index = closing(tokens, index) + 1 if value in OPENERS else index + 1
        if value == '{' and ends_body(tokens, index):
            return index
    return index + 1


def statements(tokens):
    """The statements of a brace-free run of tokens, each a list."""
    found, index = [], 0
    while index < len(tokens):
        end = statement_end(tokens, index)
        found.append(tokens[index:end])
        index = end
    return found


def matched(values, index):
    """The index of the bracket closing the `<`, `[` or `(` at index."""
    depth = 0
    for position in range(index, len(values)):
        value  = values[position]
        depth += (value in PAIRS) - (value in PAIRS.values()) - 2 * (value == '>>')
        if depth <= 0:
            return position
    return len(values) - 1


def without_requires(values):
    """The words after a requires-clause: a parenthesised expression, or a concept id with its arguments."""
    if values[1:2] == ['(']:
        return values[matched(values, 1) + 1:]
    index = 1
    while index < len(values) and (values[index].isidentifier() or values[index] == '::'):
        index += 1
    return values[matched(values, index) + 1:] if values[index:index + 1] == ['<'] else values[index:]


def without_template(values):
    """A declaration's words after its template headers, requires-clauses and attributes."""
    while values:
        if values[0] == 'template':
            values = values[matched(values, 1) + 1:]
        elif values[:2] == ['[', '[']:
            values = values[matched(values, 0) + 1:]
        elif values[0] == 'requires':
            values = without_requires(values)
        else:
            break
    return values


def chain_before(values, index):
    """The declarator `a::b::~c` or `operator new` that ends just before index."""
    if index > 1 and values[index - 2] == 'operator':
        return 'operator' + values[index - 1]
    start = index - 1
    while start > 1 and values[start - 1] == '::' and values[start - 2].isidentifier():
        start -= 2
    start -= start > 0 and values[start - 1] == '~'
    return ''.join(values[start:index]) if index and values[index - 1].isidentifier() else ''


def top_level(values):
    """Each index of values at bracket depth 0, angle brackets after a name counted as brackets."""
    depth = 0
    for index, value in enumerate(values):
        opens  = value in OPENERS or value == '<' and index and (values[index - 1].isidentifier()
                                                                  or values[index - 1] == 'template')
        closes = value in OPENERS.values() or value in ('>', '>>') and depth > 0
        if not depth and not closes:
            yield index
        depth = max(0, depth + opens - closes - (value == '>>' and depth > 1))


def declarators(values):
    """The names a plain declaration declares: a function's name, or each variable of a declarator list."""
    stops = [index for index in top_level(values) if values[index] in STOPS and values[index - 1] != 'operator']
    if not stops or not stops[0]:
        return []
    first  = chain_before(values, stops[0])
    if values[stops[0]] == '(':
        return [first] if first else []
    commas = [index for index in stops if values[index] == ',']
    later  = [chain_before(values, next((stop for stop in stops if stop > comma + 1), len(values)))
              for comma in commas]
    return [name for name in [first, *later] if name]


def declared(statement):
    """The names a statement declares at its scope: none for imports, friends and assertions."""
    values = without_template([token.value for token in statement])
    if values[:1] == ['using']:
        return [values[1]] if values[2:3] == ['='] else []
    if not values or values[0] in SKIPS:
        return []
    if values[0] in KEYS:
        names = [value for value in without_template(values[1:]) if value.isidentifier() and value not in KEYS]
        return names[:1]
    return declarators(values)


def is_namespace(statement):
    values = [token.value for token in statement[:2]]
    return values[:1] == ['namespace'] or values == ['inline', 'namespace']


def namespace_parts(statement):
    """A namespace statement's name, its body, and whether it is inline, attributed or an alias."""
    values = [token.value for token in statement]
    first  = values.index('namespace') + 1
    index  = first
    while index < len(values) and (values[index].isidentifier() or values[index] in ('::', '[', ']')):
        index += 1
    words  = values[first:index]
    name   = ''.join(value for value in words if value not in ('[', ']', 'inline'))
    alias  = values[index:index + 1] == ['=']
    odd    = values[0] == 'inline' or '[' in words or 'inline' in words or alias
    return name, [] if alias else statement[index + 1:-1], odd


class File(NamedTuple):
    relative: pathlib.Path
    folder:   str
    own:      str
    header:   bool
    details:  frozenset


def finding(file, statement, message):
    return Finding(file.relative, statement[0].line, message)


def is_abi(statement, name):
    """A runtime hook, or an SDL tag's class definition or member."""
    head = name.split('::')[0].lstrip('~')
    tag  = head in SDL_TAGS and (statement[0].value in KEYS or '::' in name)
    return bool(RUNTIME.fullmatch(head)) or tag


def extern_findings(file, statement):
    """An `extern` statement or `extern "C"` block, its contents held to the same global-scope test."""
    values = [token.value for token in statement]
    if values[1:3] == ['"C"', '{']:
        return [problem for inner in statements(statement[3:-1]) for problem in global_findings(file, inner)]
    return global_findings(file, statement[2 if values[1:2] == ['"C"'] else 1:])


def global_findings(file, statement):
    """What a statement outside every named namespace may be: ABI, an extern of ABI, a .cpp's imports."""
    values = [token.value for token in statement]
    if values[0] == 'extern':
        return extern_findings(file, statement)
    if values[0] == 'using' and not file.header and values[1] != 'namespace' and values[2:3] != ['=']:
        return []
    if values[0] == 'using':
        return [finding(file, statement, 'a `using` at global scope; import inside the detail namespace')]
    names = declared(statement)
    name  = names[0] if names else ''
    if name and is_abi(statement, name):
        return []
    return [finding(file, statement, f'`{name or values[0]}` at global scope; it belongs in `{file.own}`')]


def inner_findings(file, block):
    """Namespaces inside a named block: an anonymous one only inside a source file's own detail namespace."""
    found = []
    for statement in statements(block.tokens):
        if not is_namespace(statement):
            continue
        name, _, odd = namespace_parts(statement)
        if name or odd or file.header or block.name != file.own:
            found.append(finding(file, statement, f'namespace `{name}` nested in `{block.name}`'))
    return found


def forward_block(file, block):
    """Whether a block is a module forward header's `class Name;` list for one of its folder's files."""
    values = [token.value for token in block.tokens]
    shaped = len(values) % 3 == 0 and all(
        values[start] in ('class', 'struct') and values[start + 1].isidentifier() and values[start + 2] == ';'
        for start in range(0, len(values), 3))
    owned  = block.name.startswith(f'{file.folder}::detail::') and block.name in file.details
    return shaped and owned and file.relative.name == FORWARD


def exported_names(file, blocks):
    """The (stem, Name) pairs the file declares: its detail blocks' declarations and its forward lists."""
    names = set()
    for block in blocks:
        if block.name == file.own or forward_block(file, block):
            stem   = block.name.rsplit('::', 1)[1]
            names |= {(stem, name) for statement in statements(block.tokens) for name in declared(statement)}
    return names


def reexports(block, exported):
    """Whether a block is only `using detail::<stem>::Name;` lines, each naming a (stem, Name) the file declares."""
    values = [token.value for token in block.tokens]
    return bool(values) and len(values) % 7 == 0 and all(
        values[start:start + 3] == ['using', 'detail', '::'] and values[start + 4] == '::'
        and (values[start + 3], values[start + 5]) in exported and values[start + 6] == ';'
        for start in range(0, len(values), 7))


def export_findings(file, block, exported):
    if not file.header:
        return [Finding(file.relative, block.line, f'`{file.folder}` block in a source file; a .cpp exports nothing')]
    if not reexports(block, exported):
        return [Finding(file.relative, block.line, f'`{file.folder}` block holds more than `using '
                                                   'detail::<stem>::Name;` lines naming what this file declares')]
    return []


def block_findings(file, block, exported):
    """A named block is the file's detail namespace, its folder's export block, or a forward header's list."""
    if block.name == file.folder:
        return export_findings(file, block, exported)
    if block.name == file.own or forward_block(file, block):
        return inner_findings(file, block)
    return [Finding(file.relative, block.line, f'namespace `{block.name}` is not `{file.own}` or `{file.folder}`')]


def namespace_findings(file, statement):
    name, body, odd = namespace_parts(statement)
    if odd or not name:
        kind = 'an anonymous' if not name and not odd else 'an inline, attributed or alias'
        return [finding(file, statement, f'{kind} namespace at global scope')], None
    return [], Block(name, statement[0].line, body)


def order_findings(file, tops, blocks):
    """Nothing follows a header's export block, a second one included, and a header opens a named namespace."""
    exports = [index for index, statement in enumerate(tops)
               if is_namespace(statement) and namespace_parts(statement)[0] == file.folder]
    late    = [finding(file, statement, f'`{file.folder}` block is not last')
               for statement in tops[exports[0] + 1:]] if exports else []
    if file.header and not blocks and any(not include_wrapper(statement) for statement in tops):
        return late + [Finding(file.relative, 1, f'a header with no named namespace; declare in `{file.own}`')]
    return late


def include_wrapper(statement):
    """An `extern "C" { }` whose only content was #include lines: it declares nothing itself."""
    return [token.value for token in statement] == ['extern', '"C"', '{', '}']


def chain_at(values, index):
    """The identifiers of the qualified name starting at index, a leading `::` dropped, and the index after it."""
    index += values[index] in LEADERS
    index += index < len(values) and values[index] == '::'
    parts  = []
    while index < len(values) and values[index].isidentifier():
        parts.append(values[index])
        if values[index + 1:index + 2] != ['::']:
            return parts, index + 1
        index += 2
    return parts, index


def opens_namespace(values, index):
    """Whether the name at index is a namespace definition's own, not a `using namespace` directive's."""
    return values[index - 1:index] == ['namespace'] and values[index - 2:index - 1] != ['using']


def chains(tokens):
    """Every qualified name in the token stream but a namespace's own, with its first token."""
    values, index = [token.value for token in tokens], 0
    while index < len(values):
        parts, end = chain_at(values, index)
        if len(parts) > 1 and not opens_namespace(values, index):
            yield tokens[index], parts
        index = max(end, index + 1)


def reaches_out(file, parts):
    """Whether a name spells a detail namespace whose owner is neither the file's folder nor one enclosing it."""
    owners = ['::'.join(parts[:index]) for index, part in enumerate(parts) if part == 'detail' and index]
    return any(not (file.folder == owner or file.folder.startswith(owner + '::')) for owner in owners)


def reach_findings(file, tokens):
    """A name spelled into another folder's detail namespace, at any position; a relative `detail::` is local."""
    return [Finding(file.relative, token.line, f'`{"::".join(parts)}` reaches into another folder\'s detail')
            for token, parts in chains(tokens) if reaches_out(file, parts)]


def file_findings(root, relative, details):
    tokens = code((root / relative).read_text(errors='replace'))
    file   = File(relative, folder_namespace(relative), detail_namespace(relative), relative.suffix == shape.HEADER,
                  details)
    tops   = statements(tokens)
    found  = reach_findings(file, tokens) + directive_findings(file, tokens)
    blocks = []
    for statement in tops:
        if not is_namespace(statement):
            found += global_findings(file, statement)
            continue
        problems, block = namespace_findings(file, statement)
        found  += problems
        blocks += [block] if block else []
    exported = exported_names(file, blocks)
    found   += [problem for block in blocks for problem in block_findings(file, block, exported)]
    return sorted(found + order_findings(file, tops, blocks), key=lambda problem: problem.line)


def directive_findings(file, tokens):
    """`using namespace` in a header, at any depth."""
    return [Finding(file.relative, token.line, '`using namespace` in a header; name what is used')
            for index, token in enumerate(tokens[1:], 1)
            if file.header and token.value == 'namespace' and tokens[index - 1].value == 'using']


def checked(root):
    return [relative for relative in spellings.checked(root) if relative.is_relative_to(SOURCES)]


def findings(root):
    files   = checked(root)
    details = frozenset(detail_namespace(relative) for relative in files)
    for relative in files:
        yield from file_findings(root, relative, details)


def main():
    found = list(findings(spellings.ROOT))
    for problem in found:
        print(f'{problem.path}:{problem.line}: {problem.message}')
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
