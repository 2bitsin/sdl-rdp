#!/usr/bin/env python3
"""Write the name table of every macro, function, type, enumerator and variable FreeRDP 3 and WinPR 3 declare."""
import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile

import clang.cindex

ROOT      = pathlib.Path(__file__).resolve().parents[2]
TABLE     = ROOT / 'tools/lint/facade.names'
TREES     = ('winpr3', 'freerdp3')
REQUIRED  = re.compile(r'^\s*Require\s*\(\s*FreeRDP\s+VERSION\s+"([^"]+)"', re.M)
VERSION   = re.compile(r'^#define FREERDP_VERSION_FULL "([^"]+)"', re.M)
KINDS     = {'MACRO_DEFINITION': 'macro', 'FUNCTION_DECL': 'function', 'TYPEDEF_DECL': 'typedef',
             'TYPE_ALIAS_DECL': 'typedef', 'STRUCT_DECL': 'struct', 'UNION_DECL': 'struct', 'CLASS_DECL': 'struct',
             'ENUM_DECL': 'enum', 'ENUM_CONSTANT_DECL': 'enumerator', 'VAR_DECL': 'variable'}
NAMED     = re.compile(r'[A-Za-z_]\w*')
SCOPES    = frozenset(('LINKAGE_SPEC', 'STRUCT_DECL', 'UNION_DECL', 'CLASS_DECL', 'ENUM_DECL'))
# WinPR spellings C++ owns too: std::byte and locals named byte.
SHARED    = frozenset(('byte',))
# `_` and a lowercase letter is a project data member's shape; WinPR spells its socket and C runtime shims so.
MEMBER    = re.compile(r'_[a-z]')
# er.h defines winpr/asn1.h's ER_TAG_* enumerators as macros; der.h includes er.h.
APART     = ('freerdp/crypto/der.h', 'freerdp/crypto/er.h')
OPTIONS   = (clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
             | clang.cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)


def required_version(root):
    """The FreeRDP version sources/CMakeLists.txt requires."""
    return REQUIRED.search((root / 'sources/CMakeLists.txt').read_text()).group(1)


def conan_home(root, override):
    """The CONAN_HOME buildutil resolves: the flag, then the environment, then the repository's own."""
    return pathlib.Path(override or os.environ.get('CONAN_HOME') or root / '_conanhome')


def header_version(include):
    return VERSION.search((include / 'freerdp3/freerdp/version.h').read_text()).group(1)


def freerdp_include(home, version):
    """The include directory of the cached FreeRDP package at this version."""
    found = [include for include in sorted(home.glob('p/*/p/include'))
             if (include / 'freerdp3/freerdp/version.h').is_file() and header_version(include) == version]
    if not found:
        raise SystemExit(f'facade-names: no FreeRDP {version} package under {home}; build once to fill the cache')
    return found[0]


def openssl_includes(home):
    """WinPR's ssl.h includes OpenSSL; any cached OpenSSL declares what it needs to parse."""
    return [include for include in sorted(home.glob('p/*/p/include')) if (include / 'openssl/ssl.h').is_file()][:1]


def headers(include):
    return sorted(str(path.relative_to(include / tree)) for tree in TREES for path in (include / tree).rglob('*.h'))


def resource_dir():
    """The compiler's builtin headers, which the libclang wheel does not carry."""
    return subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True, check=True).stdout.strip()


def units(include):
    """Every header in one translation unit but the ones that clash with it, which get their own."""
    together = [header for header in headers(include) if header not in APART]
    apart    = [header for header in APART if (include / 'freerdp3' / header).is_file()]
    return [group for group in (together, apart) if group]


def parse(include, extra, group):
    text = ''.join(f'#include <{header}>\n' for header in group)
    with tempfile.NamedTemporaryFile('w', suffix='.cpp') as unit:
        unit.write(text)
        unit.flush()
        arguments = ['-x', 'c++', '-std=c++20', '-resource-dir', resource_dir(),
                     *(f'-I{include / tree}' for tree in TREES), *(f'-I{path}' for path in extra)]
        return clang.cindex.Index.create().parse(unit.name, args=arguments, options=OPTIONS)


def declared(cursor):
    """The named declarations at file scope, inside `extern "C"`, and nested in records and enums; no fields."""
    for child in cursor.get_children():
        if child.kind.name in KINDS and NAMED.fullmatch(child.spelling):
            yield child
        if child.kind.name in SCOPES:
            yield from declared(child)


def owner(cursor, trees):
    """The header a declaration is written in, relative to its include tree, or None outside FreeRDP and WinPR."""
    written = cursor.location.file
    if written is None:
        return None
    path = pathlib.Path(written.name).resolve()
    return next((str(path.relative_to(tree)) for tree in trees if path.is_relative_to(tree)), None)


def kept(name):
    return name not in SHARED and not MEMBER.match(name)


def table(parsed, include):
    """Each name once, with every kind it is declared as and the first header declaring it."""
    trees = [(include / tree).resolve() for tree in TREES]
    rows  = {}
    for cursor in (cursor for unit in parsed for cursor in declared(unit.cursor)):
        header = owner(cursor, trees)
        if header is not None and kept(cursor.spelling):
            kinds, _ = rows.setdefault(cursor.spelling, (set(), header))
            kinds.add(KINDS[cursor.kind.name])
    return {name: (','.join(sorted(kinds)), header) for name, (kinds, header) in rows.items()}


def rendered(version, rows):
    lines = [f'FreeRDP {version}'] + [f'{name}\t{kinds}\t{header}' for name, (kinds, header) in sorted(rows.items())]
    return '\n'.join(lines) + '\n'


def generate(include, extra):
    """Any clang error fails: a declaration it could not parse would drop out of the table unseen."""
    parsed = [parse(include, extra, group) for group in units(include)]
    if errors := [str(diagnostic) for unit in parsed for diagnostic in unit.diagnostics
                  if diagnostic.severity >= clang.cindex.Diagnostic.Error]:
        raise SystemExit('facade-names: ' + '\n'.join(errors))
    return rendered(header_version(include), table(parsed, include))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--conan-home', help='the conan cache holding FreeRDP (default: as buildutil resolves it)')
    parser.add_argument('--output', type=pathlib.Path, default=TABLE)
    args    = parser.parse_args(argv)
    home    = conan_home(ROOT, args.conan_home)
    include = freerdp_include(home, required_version(ROOT))
    args.output.write_text(generate(include, openssl_includes(home)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
