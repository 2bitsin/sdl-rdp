#!/usr/bin/env python3
"""Refuse scalar spellings outside the cstdint vocabulary and std operations libc++ 20 lacks; no allow list."""
import pathlib
import subprocess
import sys
from typing import NamedTuple

import fan
import shape

ROOT      = pathlib.Path(__file__).resolve().parents[2]
FACADE    = pathlib.Path('sources/sdl-rdp/freerdp-facade')
FIXED     = {'BYTE': 'std::uint8_t', 'UINT8': 'std::uint8_t', 'UCHAR': 'std::uint8_t', 'BOOLEAN': 'std::uint8_t',
             'INT8': 'std::int8_t', 'UINT16': 'std::uint16_t', 'WORD': 'std::uint16_t', 'USHORT': 'std::uint16_t',
             'INT16': 'std::int16_t', 'SHORT': 'std::int16_t', 'UINT': 'std::uint32_t', 'UINT32': 'std::uint32_t',
             'DWORD': 'std::uint32_t', 'ULONG': 'std::uint32_t', 'INT': 'std::int32_t', 'INT32': 'std::int32_t',
             'LONG': 'std::int32_t', 'SECURITY_STATUS': 'std::int32_t', 'HRESULT': 'std::int32_t',
             'UINT64': 'std::uint64_t', 'ULONGLONG': 'std::uint64_t', 'DWORD64': 'std::uint64_t',
             'QWORD': 'std::uint64_t', 'INT64': 'std::int64_t', 'LONGLONG': 'std::int64_t', 'SIZE_T': 'std::size_t',
             'SSIZE_T': 'std::ptrdiff_t', 'LONG_PTR': 'std::intptr_t', 'ULONG_PTR': 'std::uintptr_t',
             'UINT_PTR': 'std::uintptr_t', 'PUINT32': 'std::uint32_t&', 'LPDWORD': 'std::uint32_t&',
             'BOOL': 'bool, or int in a callback FreeRDP dictates', 'TRUE': 'true', 'FALSE': 'false',
             'WCHAR': 'char16_t', 'CHAR': 'char', 'LPSTR': 'std::string_view', 'LPCSTR': 'std::string_view',
             'LPWSTR': 'std::u16string_view', 'LPCWSTR': 'std::u16string_view', 'LPVOID': 'a typed reference',
             'PVOID': 'a typed reference', 'LPBYTE': 'std::span<std::uint8_t>', 'PBYTE': 'std::span<std::uint8_t>'}
BUILTIN   = {'unsigned': 'std::size_t for a size, count or index, else the cstdint type of its width',
             'long': 'the cstdint type of its width, or decltype of the C API that dictates it',
             'short': 'std::int16_t', 'signed': 'std::int8_t for signed char, else int',
             'wchar_t': 'char16_t, or the Windows API\'s own string type', 'size_t': 'std::size_t',
             'ssize_t': 'std::ptrdiff_t', 'atomic_uint': 'std::atomic<std::uint32_t>',
             'atomic_ulong': 'std::atomic<std::uint64_t>', 'atomic_long': 'std::atomic<std::int64_t>',
             'atomic_ullong': 'std::atomic<std::uint64_t>', 'atomic_llong': 'std::atomic<std::int64_t>'}
QUALIFIED = frozenset(('size_t', 'ptrdiff_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t',
                       'uint32_t', 'uint64_t', 'intptr_t', 'uintptr_t', 'intmax_t', 'uintmax_t'))
BARE      = {name: f'std::{name}' for name in QUALIFIED - {'size_t'}}
SDL       = {f'{prefix}int{width}': f'std::{kind}int{width}_t'
             for prefix, kind in (('U', 'u'), ('S', '')) for width in (8, 16, 32, 64)}
BOUNDARY  = {'HANDLE': 'WaitHandle, the facade alias'}
# libc++ 20, the macOS 26.1 SDK's, has none of these; a name is matched with the qualifier before it.
PORTABLE  = {'move_only_function': 'oxbox::utilities::MoveOnlyFunction',
             'views::chunk': 'oxbox::utilities::Chunk', 'ranges::chunk_view': 'oxbox::utilities::Chunk',
             'views::enumerate': 'oxbox::utilities::Enumerate', 'ranges::enumerate_view': 'oxbox::utilities::Enumerate'}
SCANNED   = ('sources', 'test_package')


class Finding(NamedTuple):
    path:        pathlib.Path
    line:        int
    spelling:    str
    replacement: str


def forbidden(relative):
    """The spellings a file may not write: the facade alone keeps HANDLE for FreeRDP's calls."""
    if relative.is_relative_to(FACADE):
        return FIXED | SDL | BUILTIN | BARE
    return FIXED | SDL | BUILTIN | BARE | BOUNDARY


def portable(tokens, index):
    """The PORTABLE key a token spells: its name with the qualifier before it, else its bare name."""
    name      = tokens[index].value
    qualified = f'{tokens[index - 2].value}::{name}' if index >= 2 and tokens[index - 1].value == '::' else name
    return next((spelling for spelling in (qualified, name) if spelling in PORTABLE), None)


def words(text):
    """Code tokens outside comments and literals, directive bodies included, #include lines excluded."""
    return code_words(shape.enabled_lexemes(text))


def code_words(lexemes):
    """The code tokens of lexemes, as `words` reads a whole text."""
    for token in lexemes:
        if token.value.lstrip().startswith('#'):
            yield from directive_words(token)
        elif not token.value.startswith(('//', '/*', *shape.LITERALS, "'")):
            yield token


def directive_words(token):
    directive = shape.DIRECTIVE.match(token.value)
    if directive is None or directive.group(1) == 'include':
        return
    body = token.value[directive.end():]
    for word in shape.lexemes(body):
        if not word.value.startswith(('//', '/*', *shape.LITERALS, "'")):
            yield word._replace(line=token.line + word.line - 1)


def spelled(tokens, index):
    """The name as written: a `std::` cstdint name and a `.` member access are not the bare spelling."""
    before = [token.value for token in tokens[max(0, index - 2):index]]
    return before[-1:] != ['.'] and not (tokens[index].value in QUALIFIED and before == ['std', '::'])


def file_findings(root, relative):
    spellings = forbidden(relative)
    tokens    = list(words((root / relative).read_text(errors='replace')))
    for index, token in enumerate(tokens):
        if token.value in spellings and spelled(tokens, index):
            yield Finding(relative, token.line, token.value, spellings[token.value])
        elif (operation := portable(tokens, index)) is not None:
            yield Finding(relative, token.line, operation, PORTABLE[operation])


def tracked(root):
    command = ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '--', *SCANNED]
    listing = subprocess.run(command, cwd=root, capture_output=True, text=True, check=True).stdout.splitlines()
    return sorted(pathlib.Path(name) for name in listing)


def checked(root):
    for relative in tracked(root):
        if relative.suffix in shape.EXTENSIONS and (root / relative).is_file():
            yield relative


def findings(root):
    return fan.flattened(file_findings, checked(root), root)


def main():
    found = list(findings(ROOT))
    for finding in found:
        print(f'{finding.path}:{finding.line}: `{finding.spelling}` is {finding.replacement}')
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
