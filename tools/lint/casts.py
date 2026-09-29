#!/usr/bin/env python3
"""Refuse functional and C-style casts (ES.48, ES.49): named casts and braced initialisation only; no allow list."""
import re
import sys
from typing import NamedTuple

import fan
import spellings

FUNDAMENTAL = frozenset(('bool', 'char', 'char8_t', 'char16_t', 'char32_t', 'wchar_t', 'short', 'int', 'long',
                         'signed', 'unsigned', 'float', 'double', 'void'))
QUALIFIED   = spellings.QUALIFIED | {'byte'}
QUALIFIERS  = frozenset(('const', 'volatile'))
DECLARING   = frozenset(('operator',))
CLOSING     = frozenset((')', ']', '>'))
CONTROLS    = frozenset(('if', 'while', 'for', 'switch'))
LEADING     = frozenset(('return', 'throw', 'else', 'do', 'case', 'co_return', 'co_yield', 'co_await'))
OPERANDS    = frozenset(('(', '-', '+', '!', '~', '*', '&', '::'))
DECLARATORS = frozenset(('*', '&', '&&'))
OPERATORS   = frozenset(('and', 'or', 'not', 'xor', 'bitand', 'bitor', 'compl', 'and_eq', 'or_eq', 'xor_eq',
                         'not_eq', 'const', 'volatile', 'noexcept', 'override', 'final', 'mutable', 'requires'))
TRAIT       = re.compile(r'\w+_t')
WORD        = re.compile(r'\w')
BRACKETS    = {'(': ')', '<': '>'}


class Finding(NamedTuple):
    path: spellings.pathlib.Path
    line: int
    cast: str


def value(tokens, index):
    return tokens[index].value if 0 <= index < len(tokens) else ''


def closing(tokens, index):
    """The index of the bracket closing the one at index, or len(tokens) when it never closes."""
    opener, depth = value(tokens, index), 0
    for position in range(index, len(tokens)):
        depth += {opener: 1, BRACKETS[opener]: -1}.get(tokens[position].value, 0)
        if depth == 0:
            return position
    return len(tokens)


def opening(tokens, index):
    depth = 0
    for position in range(index, -1, -1):
        depth += {')': 1, '(': -1}.get(tokens[position].value, 0)
        if depth == 0:
            return position
    return -1


def head_names(tokens, first, end):
    """The type parameters one `template <...>` head declares; a value parameter follows its type."""
    names, depth = set(), 0
    for position in range(first, end):
        word = value(tokens, position)
        depth += {'<': 1, '>': -1, '(': 1, ')': -1, '{': 1, '}': -1}.get(word, 0)
        back = position - 1
        while value(tokens, back) == '.':
            back -= 1
        before = value(tokens, back)
        declared = value(tokens, position + 1) in (',', '=') or position + 1 == end
        valued   = before in FUNDAMENTAL | QUALIFIED | DECLARATORS | QUALIFIERS | {'auto'}
        if depth == 0 and declared and WORD.match(word) and not valued:
            names.add(word)
    return names


def type_names(tokens):
    """The names a file declares as types: `using Name = ...;` aliases and its template heads' type parameters."""
    names = {value(tokens, index + 1) for index, token in enumerate(tokens)
             if token.value == 'using' and value(tokens, index + 2) == '=' and WORD.match(value(tokens, index + 1))}
    for index, token in enumerate(tokens):
        if token.value == 'template' and value(tokens, index + 1) == '<':
            names |= head_names(tokens, index + 2, closing(tokens, index + 1))
    return names


def named_type(tokens, index, names):
    """The end of a type named at index (a scalar, a `std::` scalar or trait, decltype, a parameter, an alias)."""
    word = value(tokens, index)
    if value(tokens, index - 1) in ('::', '.', '->'):
        return None
    if word in FUNDAMENTAL or word in names:
        return index + 1
    if word == 'decltype' and value(tokens, index + 1) == '(':
        return closing(tokens, index + 1) + 1
    if word != 'std' or value(tokens, index + 1) != '::':
        return None
    name = value(tokens, index + 2)
    if name in QUALIFIED:
        return index + 3
    if TRAIT.fullmatch(name) and value(tokens, index + 3) == '<':
        return closing(tokens, index + 3) + 1
    return None


def single(tokens, index):
    """A cast converts one value: a top-level comma in the group makes it a constructor call."""
    depth = 0
    for position in range(index, closing(tokens, index)):
        depth += {'(': 1, ')': -1, '{': 1, '}': -1, '[': 1, ']': -1}.get(tokens[position].value, 0)
        if depth == 1 and tokens[position].value == ',':
            return False
    return True


def pointer_declarator(tokens, index):
    """`bool(SDLCALL*)(...)`: a group ending in `*` or `&` declares a function pointer, not a cast."""
    return value(tokens, closing(tokens, index) - 1) in DECLARATORS


def template_argument(tokens, index):
    """`std::pair<int, void(int)>`: the innermost bracket around index is a template argument list."""
    depth = {')': 0, ']': 0, '}': 0, '>': 0}
    openers = {'(': ')', '[': ']', '{': '}', '<': '>'}
    for position in range(index - 1, -1, -1):
        word = value(tokens, position)
        if word in depth:
            depth[word] += 1
        elif word in openers and depth[openers[word]]:
            depth[openers[word]] -= 1
        elif word in openers or word == ';':
            return word == '<' and closes_as_template(tokens, position)
    return False


def closes_as_template(tokens, index):
    """A `<` opens template arguments only when its `>` comes before the next `;` or unmatched `)`."""
    angles, parentheses = 0, 0
    for position in range(index, len(tokens)):
        word        =  value(tokens, position)
        angles      += {'<': 1, '>': -1}.get(word, 0)
        parentheses += {'(': 1, ')': -1}.get(word, 0)
        if parentheses < 0 or word in (';', '{', '}') or angles == 0:
            return angles == 0 and parentheses >= 0
    return False


def base_initialiser(tokens, index, end):
    """`Derived(v) : BaseTy(v) { }`: a constructor's initialiser constructs, it does not cast."""
    return (value(tokens, index - 1) == ':' and value(tokens, index - 2) in (')', 'noexcept')
            and value(tokens, closing(tokens, end) + 1) in ('{', ','))


def functional(tokens, index, names):
    """`int(x)`, `Result(x)`, `decltype(y)(z)`: a type called like a function, outside function types."""
    end = named_type(tokens, index, names)
    return (end is not None and value(tokens, end) == '(' and value(tokens, index - 1) not in DECLARING
            and not pointer_declarator(tokens, end) and single(tokens, end) and not template_argument(tokens, index)
            and not base_initialiser(tokens, index, end))


def after_control(tokens, index):
    """`if (c) (void)f();`: a closing parenthesis that ends a statement's condition starts an expression."""
    return value(tokens, opening(tokens, index) - 1) in CONTROLS


def expression_start(tokens, index):
    before = value(tokens, index - 1)
    if before == ')':
        return after_control(tokens, index - 1)
    return before in LEADING or not (WORD.match(before) or before in CLOSING)


def type_id(tokens, first, end, names):
    """Whether tokens[first:end] spell a type, and whether it is one the lint knows (not a bare identifier)."""
    known, position = False, first
    while position < end:
        word = value(tokens, position)
        stop = named_type(tokens, position, names)
        if stop is not None:
            known, position = True, stop
        elif word in QUALIFIERS or word == '::' or word in DECLARATORS or (WORD.match(word) and word[0].isalpha()):
            position += 1
        else:
            return None
    return known if first < end else None


def c_style(tokens, index, names):
    """`(int)x`, `(char*)p`, `(MyEnum)x`: a parenthesised type starting an expression, followed by an operand."""
    if value(tokens, index) != '(' or not expression_start(tokens, index):
        return None
    end  = closing(tokens, index)
    kind = type_id(tokens, index + 1, end, names)
    if kind is None:
        return None
    operand = value(tokens, end + 1)
    word    = WORD.match(operand) is not None and operand not in OPERATORS
    same    = end + 1 < len(tokens) and tokens[end + 1].line == tokens[end].line
    return end if (word and (kind or same)) or (kind and operand in OPERANDS) else None


def spelled(tokens, first, end):
    """The type as written, a space between two words (`unsigned char`, `Frame const*`)."""
    text = ''
    for token in tokens[first:end]:
        text += f' {token.value}' if WORD.match(token.value) and WORD.match(text[-1:]) else token.value
    return text


def file_findings(root, relative):
    tokens = list(spellings.words((root / relative).read_text(errors='replace')))
    names  = type_names(tokens)
    for index, token in enumerate(tokens):
        end = c_style(tokens, index, names)
        if end is not None:
            yield Finding(relative, token.line, spelled(tokens, index + 1, end).strip())
        elif functional(tokens, index, names):
            yield Finding(relative, token.line, spelled(tokens, index, named_type(tokens, index, names)).strip())


def findings(root):
    return fan.flattened(file_findings, spellings.checked(root), root)


def main():
    found = list(findings(spellings.ROOT))
    for finding in found:
        print(f'{finding.path}:{finding.line}: cast to `{finding.cast}`: Narrowed<T>(x) for a narrowing, T{{ x }} for '
              'a widening, static_cast<T>(x) for a float or enum conversion, a comparison for a truth value')
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
