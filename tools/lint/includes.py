#!/usr/bin/env python3
"""Refuse an include of another module's header unless the including module links that module."""
import pathlib
import re
import sys
from typing import NamedTuple

import cmake

ROOT       = pathlib.Path(__file__).resolve().parents[2]
EXTENSIONS = frozenset({'.c', '.cpp', '.h', '.hpp'})
INCLUDE    = re.compile(r'^\s*#\s*include\s*<(?P<path>[^>]+)>', re.M)
KIND_TAG   = re.compile(r'\.(obj|test|so|exe|lib)$')


class Module(NamedTuple):
    directory: pathlib.Path
    names:     frozenset[str]
    links:     frozenset[str]
    tests:     frozenset[str]


class Finding(NamedTuple):
    path:    pathlib.Path
    line:    int
    include: str
    module:  pathlib.Path


def names(relative):
    """The spellings Link_dependencies accepts for a module: its target name and its leaf."""
    parts = [KIND_TAG.sub('', part) for part in relative.parts[1:]]
    return frozenset({'-'.join(parts), parts[-1]})


def dependencies(path):
    """Positional and TEST names of the module's Link_dependencies calls."""
    calls, _ = cmake.scan(path.read_text())
    words = [word for call in calls if call.name == 'Link_dependencies'
             for word in call.text.partition('(')[2].rstrip(')').split()]
    split = words.index('TEST') if 'TEST' in words else len(words)
    return frozenset(words[:split]), frozenset(words[split + 1:])


def modules(root):
    found = []
    for path in sorted((root / 'sources').rglob('CMakeLists.txt')):
        relative = path.parent.relative_to(root)
        if len(relative.parts) > 1 and not any(part.startswith('_') for part in relative.parts):
            found.append(Module(relative, names(relative), *dependencies(path)))
    return found


def owner(modules_by_directory, relative):
    for parent in (relative, *relative.parents):
        if parent in modules_by_directory:
            return modules_by_directory[parent]
    return None


def is_test(path):
    return '.test' in path.name or any(part.endswith('.test') for part in path.parts)


def checked_files(root, catalogue):
    """Each C or C++ file under sources/ with the module that owns it."""
    for path in sorted((root / 'sources').rglob('*')):
        relative = path.relative_to(root)
        module   = owner(catalogue, relative.parent)
        if path.suffix in EXTENSIONS and module is not None and not any(p.startswith('_') for p in relative.parts):
            yield relative, module


def unlinked(root, catalogue, relative, module):
    linked = module.links | (module.tests if is_test(relative) else frozenset())
    text   = (root / relative).read_text(errors='replace')
    for match in INCLUDE.finditer(text):
        target = owner(catalogue, pathlib.Path('sources', match.group('path')).parent)
        if target is not None and target != module and not target.names & linked:
            yield Finding(relative, cmake.line_of(text, match.start()), match.group('path'), target.directory)


def findings(root):
    catalogue = {module.directory: module for module in modules(root)}
    for relative, module in checked_files(root, catalogue):
        yield from unlinked(root, catalogue, relative, module)


def main():
    found = list(findings(ROOT))
    for finding in found:
        print(f'{finding.path}:{finding.line}: <{finding.include}> needs {finding.module} in Link_dependencies')
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
