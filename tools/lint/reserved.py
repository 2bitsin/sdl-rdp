#!/usr/bin/env python3
"""Refuse identifiers [lex.name] reserves: an underscore then a capital, or a double underscore anywhere."""
import re
import sys
from typing import NamedTuple

import fan
import spellings

RESERVED = re.compile(r'_[A-Z]\w*|\w*__\w*')
# Named by their owners: oxbox's reflection hooks (oxbox #53), NVENC's struct tags, POSIX's sysconf names,
# glibc's allocator entry points and the preprocessor's own macros.
FOREIGN  = re.compile(r'_(?:Label|Decode|Encode|NV_ENC_\w+|SC_\w+)|__libc_\w+|__VA_ARGS__|__cplusplus')


class Finding(NamedTuple):
    path: spellings.pathlib.Path
    line: int
    name: str


def file_findings(root, relative):
    for token in spellings.words((root / relative).read_text(errors='replace')):
        if RESERVED.fullmatch(token.value) and not FOREIGN.fullmatch(token.value):
            yield Finding(relative, token.line, token.value)


def findings(root):
    return fan.flattened(file_findings, spellings.checked(root), root)


def main():
    found = list(findings(spellings.ROOT))
    for finding in found:
        print(f'{finding.path}:{finding.line}: `{finding.name}` is reserved; a member function is UpperCamel, '
              'member data _lower, and no name holds a double underscore')
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
