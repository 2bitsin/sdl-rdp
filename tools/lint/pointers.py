#!/usr/bin/env python3
"""Refuse pointers outside the signatures an ABI writes, judged by clang on canonical types."""
import argparse
import collections
import concurrent.futures
import functools
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
from typing import NamedTuple

import shape

ROOT            = pathlib.Path(__file__).resolve().parents[2]
KINDS           = ('parameters', 'members', 'returns', 'null checks', 'unchecked')
ABI_HOLDERS     = ('sdl-rdp/backend', 'sdl-rdp/SDL3')
COUNTED         = frozenset(('sdl-rdp/headless-client.test tests', 'sdl-rdp/integration', 'sdl-rdp/integration tests',
                             'sdl-rdp/sample-gate.test tests'))
UNCONSTRAINED   = re.compile(r'^\s*(typename|class)\b')
BINDING         = re.compile(r'^(/[^:]+):(\d+):(\d+): note: "(\w+)" binds here')
PRINTED         = re.compile(r'^Binding for "(\w+)":$')
DECLARED        = re.compile(r'(operator\s*(?:\(\)|\[\]|[^\s\w(]+|\s\w+)|[A-Za-z_]\w*)\s*\(')
NOT_NAMES       = re.compile(r'\[\[.*?\]\]|"[^"]*"')
NOT_DECLARED    = frozenset(('decltype', 'alignas', '__attribute__', '__declspec')) | shape.MARKS
HEAD_END        = re.compile(r'[{;]')
THREADS         = os.cpu_count() or 1
SITE            = ('sitefn', 'sitelambda', 'siteouter')
HEAD_LINES      = 20
TEMPLATE_HEAD   = re.compile(r'^\s*template\s*<')
CLASS_HEAD      = re.compile(r'^\s*(?:class|struct|union)\s+(?:(?:\[\[.*?\]\]|alignas\(.*?\))\s*)*(\w+(?:::\w+)*)')
GCC_ONLY        = ('-fconstexpr-ops-limit=',)
# buildutil's reflect generator writes `reflect_scheme(T*)` and oxbox calls it with a null `T*` tag.
REFLECT_ENTRY   = 'reflect_scheme'
PLATFORM_TAGS   = frozenset(('win32', 'linux', 'macos', 'emscripten', 'posix', 'apple', 'native'))
LIVE_PLATFORMS  = frozenset(('native', 'posix', 'linux'))
CARRIED_ENTRIES = re.compile(r'^(BUILD_TESTING|CMAKE_BUILD_TYPE|CMAKE_TOOLCHAIN_FILE|BUILDUTIL_\w+|\w+_OPTION_\w+)$')
QUERIES         = r'''
set traversal AsIs
set output diag
set bind-root false
let own allOf(unless(isExpansionInSystemHeader()), isExpansionInFileMatching("^{sources}"),
              unless(isExpansionInFileMatching("^{abi}")))
let foreign anyOf(unless(isExpansionInFileMatching("^{sources}")), isExpansionInFileMatching("^{abi}"))
let ptr hasCanonicalType(pointerType())
let owning cxxRecordDecl(has(cxxDestructorDecl(isUserProvided())),
                         has(cxxConstructorDecl(isCopyConstructor(), isDeleted())))
let raii classTemplateSpecializationDecl(hasSpecializedTemplate(classTemplateDecl(has(owning))),
                                         hasAnyTemplateArgument(refersToDeclaration(functionDecl())))
let composite type(anyOf(pointerType(), memberPointerType(), arrayType(),
                          recordType(hasDeclaration(classTemplateSpecializationDecl(unless(raii))))))
let anyptr hasCanonicalType(type(anyOf(composite, referenceType(pointee(composite)))))
let crecord recordDecl(unless(hasAncestor(namespaceDecl())), foreign)
let owner hasAncestor(functionDecl(unless(isImplicit()),
                                   optionally(cxxMethodDecl(ofClass(cxxRecordDecl().bind("pclass")))),
                                   optionally(cxxMethodDecl(hasOverloadedOperatorName("()")).bind("pcall")),
                                   optionally(cxxMethodDecl(ofClass(raii)).bind("praii"))).bind("pfn"))
let outer functionDecl(unless(isImplicit()), unless(cxxMethodDecl(ofClass(isLambda()))))
let enclosing functionDecl(outer, optionally(cxxMethodDecl(ofClass(cxxRecordDecl().bind("lclass")))))
let closure optionally(hasAncestor(lambdaExpr(optionally(hasAncestor(enclosing.bind("lfn"))),
                                              optionally(hasAncestor(varDecl().bind("lvar")))).bind("plambda")))
let fnref ignoringParenImpCasts(declRefExpr(to(functionDecl().bind("slot"))))
let addressed ignoringParenImpCasts(unaryOperator(hasOperatorName("&"), hasUnaryOperand(fnref)))
let lamref ignoringParenImpCasts(cxxMemberCallExpr(on(ignoringParenImpCasts(anyOf(
    lambdaExpr().bind("lslot"), declRefExpr(to(varDecl(hasDescendant(lambdaExpr().bind("lslot"))))))))))
let value anyOf(fnref, addressed, lamref)
let flow anyOf(value, ignoringParenImpCasts(conditionalOperator(hasTrueExpression(value))),
               ignoringParenImpCasts(conditionalOperator(hasFalseExpression(value))))
let pparam parmVarDecl(own, hasType(ptr))
let pchecked pparam.bind("checked")
let fnptr hasCanonicalType(pointerType(pointee(ignoringParens(functionType()))))
let voidptr hasCanonicalType(pointerType(pointee(voidType())))
let cpointee anyOf(hasType(hasCanonicalType(pointerType(pointee(voidType())))),
                   hasType(hasCanonicalType(pointerType(pointee(isAnyCharacter())))),
                   hasType(hasCanonicalType(pointerType(pointee(hasDeclaration(recordDecl(foreign)))))))
let holder hasCanonicalType(recordType(hasDeclaration(recordDecl(unless(hasAncestor(namespaceDecl())), foreign,
                                                                 has(fieldDecl(hasType(ptr)))))))
let site allOf(optionally(hasAncestor(functionDecl(unless(isImplicit())).bind("sitefn"))),
               optionally(hasAncestor(lambdaExpr().bind("sitelambda"))),
               optionally(hasAncestor(outer.bind("siteouter"))))
let pfield allOf(own, unless(isImplicit()), unless(hasParent(raii)),
                 anyOf(hasType(anyptr), fieldDecl(hasType(holder)).bind("fholder")))
let pret allOf(own, unless(isImplicit()), unless(cxxMethodDecl(ofClass(raii))), returns(anyptr))
let recpointee hasType(hasCanonicalType(pointerType(pointee(hasDeclaration(
    recordDecl(unless(hasAncestor(namespaceDecl()))).bind("precord"))))))
let dependent hasType(pointerType(pointee(templateTypeParmType(hasDeclaration(templateTypeParmDecl().bind("tparm"))))))
let signature hasParent(typeLoc(anyOf(hasParent(functionDecl()), hasParent(typeLoc(hasParent(functionDecl()))))))
match parmVarDecl(own, signature, hasType(anyptr), owner, closure, optionally(parmVarDecl(cpointee).bind("pc")),
                  optionally(recpointee), optionally(parmVarDecl(dependent).bind("pdep")),
                  optionally(parmVarDecl(hasType(voidptr)).bind("pvoid"))).bind("param")
let deref anyOf(unaryOperator(hasOperatorName("*")), memberExpr(isArrow()), arraySubscriptExpr())
let indexed allOf(hasType(anyptr), unless(isTypeDependent()), unless(declRefExpr()), unless(explicitCastExpr()))
match declRefExpr(own, to(parmVarDecl(own, hasType(anyptr)).bind("dereferenced")),
                  anyOf(hasParent(deref), hasParent(implicitCastExpr(hasParent(deref)))))
match expr(own, indexed, has(declRefExpr(isTypeDependent(), to(parmVarDecl(own).bind("dereferenced")))),
           anyOf(hasParent(deref), hasParent(implicitCastExpr(hasParent(deref)))))
let throughfn callExpr(unless(callee(functionDecl())), callee(expr(hasType(fnptr))))
let into anyOf(callExpr(callee(functionDecl(own))), throughfn)
let converted implicitCastExpr(anyOf(hasParent(into), hasParent(implicitCastExpr(hasParent(into)))))
let passed anyOf(hasParent(into), hasParent(converted))
let forwarder callExpr(callee(functionDecl(hasAnyName("::std::forward", "::std::move"))), passed)
let handed anyOf(passed, hasParent(forwarder), hasParent(implicitCastExpr(hasParent(forwarder))),
                 hasParent(lambdaExpr()))
match declRefExpr(own, to(parmVarDecl(own, hasType(anyptr)).bind("escaped")), unless(handed))
match expr(own, indexed, has(declRefExpr(isTypeDependent(), to(parmVarDecl(own).bind("escaped")))), unless(handed))
match fieldDecl(pfield, hasParent(recordDecl().bind("fclass"))).bind("field")
match functionDecl(own, anyOf(hasAnyParameter(hasType(anyptr)), returns(anyptr)),
                   hasAnyBody(stmt().bind("body"))).bind("bodied")
match functionDecl(pret, closure, optionally(hasReturnTypeLoc(typeLoc().bind("rloc"))),
                   optionally(cxxMethodDecl(ofClass(cxxRecordDecl().bind("rclass")))),
                   optionally(hasAncestor(functionDecl(isExternC()).bind("rexport"))),
                   optionally(decl(isInstantiated()).bind("rinst"))).bind("ret")
match functionDecl(own, isMain()).bind("entry")
let part refersToDeclaration(functionDecl().bind("part"))
match classTemplateSpecializationDecl(raii, forEachTemplateArgument(part))
let invoker cxxMethodDecl(hasOverloadedOperatorName("()")).bind("part")
let acquiring hasDeclaration(cxxRecordDecl(has(functionTemplateDecl(has(invoker)))))
match cxxMethodDecl(ofClass(raii), forEachDescendant(substNonTypeTemplateParmExpr(hasType(hasCanonicalType(recordType(
    acquiring))))))
let released substNonTypeTemplateParmExpr(hasDescendant(declRefExpr(to(functionDecl().bind("part")))))
match cxxOperatorCallExpr(isExpansionInFileMatching("/unique_ptr[.]h$"),
                          callee(cxxMethodDecl(own, forEachDescendant(callExpr(callee(released)))).bind("released")))
let reflected hasDeclaration(anyOf(enumDecl(), cxxRecordDecl(has(friendDecl(has(functionDecl(hasName("{reflect}"))))))))
let tagged parmVarDecl(hasType(pointerType(pointee(hasCanonicalType(reflected)))))
match functionDecl(own, hasName("{reflect}"), isConstexpr(), parameterCountIs(1), unless(cxxMethodDecl()),
                   hasParameter(0, tagged)).bind("reflected")
match functionDecl(own, isDefinition(), isExternC()).bind("cdef")
match translationUnitDecl(hasDescendant(functionDecl(own, isDefinition(), isExternC())),
                          forEachDescendant(functionDecl(isExternC(), foreign).bind("cside")))
match binaryOperator(own, hasOperatorName("="), hasLHS(memberExpr(member(fieldDecl(hasParent(crecord))))),
                     hasRHS(flow))
let cast ignoringParenImpCasts(explicitCastExpr(hasSourceExpression(anyOf(fnref, addressed))))
match callExpr(own, callee(functionDecl(isExternC(), foreign)), unless(hasAnyArgument(cast)),
               forEachArgumentWithParam(flow, parmVarDecl()))
let cname parmVarDecl(hasType(pointerType(pointee(isAnyCharacter(), isConstQualified()))))
let property functionDecl(isExternC(), foreign, hasAnyParameter(cname), hasAnyParameter(hasType(voidptr)))
let erased ignoringParenImpCasts(cxxReinterpretCastExpr(hasSourceExpression(anyOf(fnref, addressed))))
match callExpr(own, callee(property), hasAnyArgument(erased))
let registers functionDecl(isExternC(), foreign, hasAnyParameter(hasType(fnptr)))
let acquirer refersToDeclaration(functionDecl(optionally(registers.bind("registrar"))).bind("acquirer"))
match cxxConstructExpr(own, hasType(hasCanonicalType(recordType(hasDeclaration(
                           classTemplateSpecializationDecl(raii, forEachTemplateArgument(acquirer)))))),
                       forEachArgumentWithParam(ignoringParenImpCasts(declRefExpr(to(functionDecl().bind("rslot")))),
                                                parmVarDecl()))
let forwardsfn declRefExpr(to(parmVarDecl(hasType(fnptr))))
match functionDecl(own, isDefinition(), hasAnyParameter(hasType(fnptr)),
                   hasDescendant(callExpr(callee(registers),
                                          hasAnyArgument(ignoringParenImpCasts(forwardsfn))))).bind("registrar")
let callback anyOf(hasType(fnptr), hasType(references(classTemplateSpecializationDecl(hasName("::std::function")))))
match callExpr(own, callee(functionDecl(foreign, unless(isTemplateInstantiation()))),
               forEachArgumentWithParam(anyOf(flow, ignoringImplicit(cxxConstructExpr(hasArgument(0, flow)))),
                                        parmVarDecl(callback)))
match initListExpr(own, hasType(hasCanonicalType(recordType(hasDeclaration(crecord)))), forEach(expr(flow)))
match cxxOperatorCallExpr(isExpansionInFileMatching("/unique_ptr[.]h$"), callee(cxxMethodDecl(own, ofClass(anyOf(
    classTemplateSpecializationDecl(hasSpecializedTemplate(classTemplateDecl(has(cxxRecordDecl().bind("deleter"))))),
    cxxRecordDecl().bind("deleter"))))))
match callExpr(own, callee(functionDecl(hasName("Expects"))), hasArgument(0, anyOf(
    ignoringParenImpCasts(binaryOperator(hasAnyOperatorName("==", "!="),
                                         hasEitherOperand(ignoringParenImpCasts(declRefExpr(to(pchecked)))))),
    ignoringParenImpCasts(declRefExpr(to(pchecked))))))
match implicitCastExpr(own, hasCastKind("CK_PointerToBoolean"),
                       hasSourceExpression(ignoringParenImpCasts(declRefExpr(to(pparam.bind("tested"))))))
match binaryOperator(own, hasAnyOperatorName("==", "!="),
                     hasEitherOperand(ignoringParenImpCasts(declRefExpr(to(pparam.bind("tested"))))),
                     hasEitherOperand(ignoringParenImpCasts(cxxNullPtrLiteralExpr())))
match declRefExpr(own, to(pparam.bind("used")))
match expr(own, indexed, has(declRefExpr(isTypeDependent(), to(parmVarDecl(own).bind("used")))))
match declRefExpr(own, to(pparam.bind("unwrapped")), anyOf(hasParent(explicitCastExpr()),
                                                         hasParent(implicitCastExpr(hasParent(explicitCastExpr())))))
match callExpr(own, callee(functionDecl(foreign)), hasAnyArgument(ignoringParenImpCasts(anyOf(
    declRefExpr(to(pparam.bind("unwrapped"))),
    expr(indexed, has(declRefExpr(isTypeDependent(), to(parmVarDecl(own).bind("unwrapped")))))))))
let ctype hasType(pointerType(pointee(isAnyCharacter())))
let string classTemplateSpecializationDecl(hasName("::std::basic_string"))
let maybe classTemplateSpecializationDecl(hasName("::std::optional"),
                                          hasTemplateArgument(0, refersToType(hasCanonicalType(recordType(
                                              hasDeclaration(string))))))
match functionDecl(own, parameterCountIs(1), hasParameter(0, ctype),
                   returns(hasCanonicalType(recordType(hasDeclaration(maybe))))).bind("ctext")
match cxxConstructExpr(own, hasDeclaration(cxxConstructorDecl(ofClass(matchesName("^::std::unique_ptr$")))),
                       hasArgument(0, ignoringParenImpCasts(declRefExpr(to(pparam.bind("adopted"))))))
match callExpr(own, forEachArgumentWithParam(ignoringParenImpCasts(anyOf(
                       declRefExpr(to(pparam.bind("forwarded"))),
                       expr(indexed, has(declRefExpr(isTypeDependent(), to(parmVarDecl(own).bind("forwarded"))))))),
                   parmVarDecl().bind("into")))
match typedefNameDecl(foreign, hasType(hasCanonicalType(recordType(hasDeclaration(recordDecl(own).bind("abirecord"))))))
match declRefExpr(own, to(functionDecl(hasAnyParameter(pparam)).bind("referee")), site)
match fieldDecl(own, hasType(fnptr), hasParent(recordDecl().bind("regrecord"))).bind("regfn")
match fieldDecl(own, hasType(voidptr), hasParent(recordDecl().bind("regrecord"))).bind("regvoid")
match classTemplateSpecializationDecl(raii).bind("raiitype")
let tableref anyOf(memberExpr(member(valueDecl().bind("ftable"))), declRefExpr(to(varDecl().bind("ftable"))))
let throughtable callExpr(throughfn, callee(expr(anyOf(ignoringParenImpCasts(tableref), hasDescendant(tableref)))))
let deduced functionDecl(hasReturnTypeLoc(loc(autoType()))).bind("rdeduced")
let relay optionally(hasReturnValue(ignoringParenImpCasts(anyOf(throughtable,
                                                                callExpr(callee(functionDecl().bind("relayed")))))))
match returnStmt(own, forFunction(functionDecl(optionally(deduced)).bind("rfn")), relay).bind("rstmt")
match typedefNameDecl(unless(isExpansionInSystemHeader()),
                      hasType(qualType(hasDeclaration(typedefNameDecl().bind("aliased"))))).bind("alias")
enable output print
let named qualType(hasDeclaration(typedefNameDecl().bind("talias")))
let spelled anyOf(named, references(named))
let typed allOf(hasType(hasCanonicalType(qualType().bind("type"))), optionally(decl(isInstantiated()).bind("tinst")),
                optionally(hasType(spelled)))
match parmVarDecl(own, hasType(anyptr), typed).bind("typed")
match fieldDecl(pfield, typed).bind("typed")
let abifn functionDecl(isExternC(), isExpansionInFileMatching("^{abi}"),
                       hasType(hasCanonicalType(qualType().bind("type"))))
match declRefExpr(to(abifn), hasAncestor(typeLoc(loc(decltypeType()))),
                  hasAncestor(decl(anyOf(typedefNameDecl(), declaratorDecl())).bind("abitable")))
let argument parmVarDecl(own, hasType(anyptr)).bind("fnarg")
let fnarg ignoringParenImpCasts(anyOf(declRefExpr(to(argument)),
                                      callExpr(callee(functionDecl(hasAnyName("::std::forward", "::std::move"))),
                                               hasArgument(0, ignoringParenImpCasts(declRefExpr(to(argument)))))))
let tabled optionally(anyOf(ignoringParenImpCasts(tableref), hasDescendant(tableref)))
match callExpr(own, throughfn, callee(expr(tabled)), forEachArgumentWithParamType(fnarg, qualType()))
match functionDecl(pret, hasReturnTypeLoc(typeLoc().bind("rloc")), returns(hasCanonicalType(qualType().bind("type"))),
                   optionally(returns(spelled)), optionally(functionDecl(isInstantiated()).bind("tinst")))
'''


class Location(NamedTuple):
    path:   str
    line:   int
    column: int


class Finding(NamedTuple):
    path:   pathlib.Path
    line:   int
    column: int
    kind:   str
    type:   str
    owner:  str

    def report(self):
        return f'{self.path}:{self.line}:{self.column}: {KIND_TEXT[self.kind]} ({self.type}) in {self.owner or "?"}'

    def key(self):
        return (str(self.path), self.kind, self.owner, self.type)


KIND_TEXT = {'parameters':  'pointer parameter',
             'members':     'pointer member',
             'returns':     'pointer return',
             'null checks': 'null-check contract on a parameter outside the ABI boundary',
             'unchecked':   'boundary pointer used without a null test'}


def query_text(root):
    sources = re.escape(f'{root}/sources/')
    abi     = re.escape(f'{root}/sources/sdl-rdp/abi/')
    return QUERIES.replace('{sources}', sources).replace('{abi}', abi).replace('{reflect}', REFLECT_ENTRY)


def compiler_install_dir(compiler):
    """The GCC whose libstdc++ the build used, so clang parses the same library."""
    libgcc = subprocess.run([compiler, '-print-libgcc-file-name'], capture_output=True, text=True, check=True)
    return pathlib.Path(libgcc.stdout.strip()).parent


def clang_arguments(arguments, install_dirs):
    """A compile command rewritten for clang's front end: clang itself, the build's libstdc++, no GCC-only flags."""
    compiler, *rest = arguments
    kept = [argument for argument in rest if not argument.startswith(GCC_ONLY)]
    if 'clang' in pathlib.Path(compiler).name:
        return ['clang++', *kept]
    if compiler not in install_dirs:
        install_dirs[compiler] = compiler_install_dir(compiler)
    return ['clang++', f'--gcc-install-dir={install_dirs[compiler]}', *kept]


def database_units(root, database, install_dirs):
    """The project's C++ translation units in one compile database, as clang commands, by resolved path."""
    units, sources = {}, root / 'sources'
    for entry in json.loads(database.read_text()):
        path = pathlib.Path(entry['directory'], entry['file']).resolve()
        if path.suffix == '.cpp' and path.is_relative_to(sources):
            arguments   = entry.get('arguments') or shlex.split(entry['command'])
            units[path] = {'directory': entry['directory'], 'file': str(path),
                           'arguments': clang_arguments(arguments, install_dirs)}
    return units


def other_platform(path):
    """A source tagged for a platform the Linux build does not target: `wire.win32.cpp`, `x.macos/`."""
    tags = {tag for part in path.parts for tag in part.split('.')[1:]} & PLATFORM_TAGS
    return bool(tags) and not tags & LIVE_PLATFORMS


def tracked_units(root):
    listed = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--', 'sources'],
                            capture_output=True, text=True, check=True)
    paths  = (pathlib.Path(name) for name in listed.stdout.split('\0') if name.endswith('.cpp'))
    return {(root / path).resolve() for path in paths if not other_platform(path)}


def cache_entries(build):
    """The build's CMake cache as `name -> (type, value)`."""
    found = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if (entry := re.match(r'^([A-Za-z_][\w.+-]*):(\w+)=(.*)$', line)):
            found[entry[1]] = (entry[2], entry[3])
    return found


def bench_database(root, build):
    """The benches are configured only for `buildutil bench`: the same configure with them on, never built."""
    if not (build / 'CMakeCache.txt').exists():
        return None
    entries = cache_entries(build)
    target  = build / 'pointers' / 'benches'
    if entries.get('BUILD_BENCHMARKING', ('', 'ON'))[1] == 'ON':
        return None
    database  = target / 'compile_commands.json'
    generated = max(path.stat().st_mtime for name in ('CMakeCache.txt', 'compile_commands.json')
                    if (path := build / name).exists())
    if database.exists() and database.stat().st_mtime >= generated:
        return database
    carried = [f'-D{name}={value}' for name, (kind, value) in entries.items()
               if kind == 'UNINITIALIZED' or CARRIED_ENTRIES.match(name)]
    command = [entries['CMAKE_COMMAND'][1], '-S', str(root), '-B', str(target), '-G', entries['CMAKE_GENERATOR'][1],
               *carried, '-DBUILD_BENCHMARKING=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    configured = subprocess.run(command, capture_output=True, text=True)
    if configured.returncode:
        raise SystemExit(f'pointers: the bench configure failed:\n{configured.stdout[-2000:]}{configured.stderr}')
    return database


def translation_units(root, build):
    """Every tracked translation unit as a clang command; one the databases do not hold fails the lint."""
    install_dirs = {}
    units        = database_units(root, build / 'compile_commands.json', install_dirs)
    tracked      = tracked_units(root)
    if tracked - units.keys() and (benches := bench_database(root, build)):
        units = database_units(root, benches, install_dirs) | units
    if missing := sorted(tracked - units.keys()):
        raise SystemExit('pointers: not in the compile database: ' + ', '.join(str(path) for path in missing))
    return list(units.values())


def dependencies(build):
    """Each object's inputs as ninja recorded them, for the result cache; none without ninja's log."""
    ninja = shutil.which('ninja')
    if not ninja or not (build / '.ninja_deps').exists():
        return {}
    run = subprocess.run([ninja, '-C', str(build), '-t', 'deps'], capture_output=True, text=True)
    found, current = {}, None
    for line in run.stdout.splitlines():
        if line and not line.startswith(' '):
            current = line.split(':', 1)[0]
            found[current] = []
        elif line.strip() and current:
            found[current].append(line.strip())
    return found


def preprocessed_inputs(unit):
    """A unit's headers from the preprocessor, for a unit ninja never built."""
    arguments = unit['arguments']
    if '-o' in arguments:
        cut       = arguments.index('-o')
        arguments = arguments[:cut] + arguments[cut + 2:]
    listed = subprocess.run([*arguments, '-M'], cwd=unit['directory'],
                            capture_output=True, text=True)
    return None if listed.returncode else listed.stdout.replace('\\\n', ' ').split(':', 1)[-1].split()


def object_of(unit):
    arguments = unit['arguments']
    return arguments[arguments.index('-o') + 1] if '-o' in arguments else None


def unit_key(unit, queries, inputs):
    """A digest of the command, the queries and every input's size and time; no key for an input unrecorded or gone."""
    paths = [pathlib.Path(unit['directory']) / path for path in [unit['file'], *(inputs or [])]]
    if inputs is None or not all(path.exists() for path in paths):
        return None
    digest = hashlib.sha256(json.dumps(unit['arguments']).encode() + queries.encode())
    for path in paths:
        status = path.stat()
        digest.update(f'{path}:{status.st_size}:{status.st_mtime_ns}'.encode())
    return digest.hexdigest()


def clang_query():
    found = shutil.which('clang-query') or sorted(pathlib.Path('/usr/lib').glob('llvm-*/bin/clang-query'))
    if not found:
        raise SystemExit('pointers: clang-query is not installed')
    return str(found if isinstance(found, str) else found[-1])


def matches(output):
    """clang-query's output as one dictionary per match: a location per bound node, a text per printed type."""
    blocks, current, printed = [], None, None
    for line in output.splitlines():
        if line.startswith('Match #'):
            current, printed = {}, None
            blocks.append(current)
        elif current is None:
            continue
        elif (bound := BINDING.match(line)):
            path, row, column, name = bound.groups()
            current[name] = [path, int(row), int(column)]
        elif printed == 'type':
            current['type'], printed = line, None
        else:
            printed = (heading := PRINTED.match(line)) and heading[1]
    return blocks


@functools.lru_cache(maxsize=None)
def file_lines(path):
    return pathlib.Path(path).read_text(errors='replace').splitlines()


def declared_name(location):
    """The name a function declaration at this location declares: the first word called, attributes aside."""
    lines = file_lines(location[0])[location[1] - 1:location[1] - 1 + HEAD_LINES]
    head  = HEAD_END.split(NOT_NAMES.sub(' ', '\n'.join(lines)[location[2] - 1:]), maxsplit=1)[0]
    return next((word for word in DECLARED.findall(head) if word not in NOT_DECLARED), '?')


def query(tool, database, queries, unit):
    run = subprocess.run([tool, '-p', str(database), '-f', str(queries), unit['file']], capture_output=True, text=True)
    if run.returncode or ('error:' in run.stderr and 'Match #' not in run.stdout):
        raise SystemExit(f'pointers: clang could not parse {unit["file"]}:\n{run.stdout[-1000:]}{run.stderr[-2000:]}')
    return matches(run.stdout)


def lint_text(queries, tool):
    """Everything a cached match depends on besides the unit: the queries and the clang-query that runs them."""
    version = subprocess.run([tool, '--version'], capture_output=True, text=True).stdout
    return queries + version


def cached_keys(units, text, build):
    inputs = dependencies(build)
    with concurrent.futures.ThreadPoolExecutor(THREADS) as pool:
        listed = pool.map(lambda unit: inputs.get(object_of(unit)) or preprocessed_inputs(unit), units)
        return {unit['file']: unit_key(unit, text, recorded) for unit, recorded in zip(units, listed)}


def cache_file(work, unit):
    return work / 'cache' / f'{hashlib.sha256(unit["file"].encode()).hexdigest()}.json'


def cached(work, unit, key):
    """A unit's matches as its last run left them, when its key still holds; each unit is its own file, written as
    the unit finishes, so a run stopped midway keeps what it parsed."""
    path = cache_file(work, unit)
    if not key or not path.exists():
        return None
    entry = json.loads(path.read_text())
    return entry['matches'] if entry['key'] == key else None


def parsed(tool, work, unit, key):
    found = query(tool, work, work / 'queries.txt', unit)
    if key:
        cache_file(work, unit).write_text(json.dumps({'key': key, 'matches': found}))
    return found


def query_units(root, build):
    """Every translation unit's matches, parsed again only where the unit or one of its inputs changed."""
    work = build / 'pointers'
    (work / 'cache').mkdir(parents=True, exist_ok=True)
    units, text = translation_units(root, build), query_text(root)
    (work / 'compile_commands.json').write_text(json.dumps(units))
    (work / 'queries.txt').write_text(text)
    tool  = clang_query()
    keys  = cached_keys(units, lint_text(text, tool), build)
    found = {unit['file']: cached(work, unit, keys[unit['file']]) for unit in units}
    stale = [unit for unit in units if found[unit['file']] is None]
    with concurrent.futures.ThreadPoolExecutor(THREADS) as pool:
        for unit, matches in zip(stale, pool.map(lambda unit: parsed(tool, work, unit, keys[unit['file']]), stale)):
            found[unit['file']] = matches
    return [{name: value if name == 'type' else Location(*value) for name, value in block.items()}
            for matches in found.values() for block in matches]


class Facts:
    """What the matches say, deduplicated across translation units by declaration location."""

    SETS = {'entry': 'entries', 'deleter': 'deleters', 'checked': 'checked', 'tested': 'tested', 'adopted': 'tested',
            'used': 'used', 'abirecord': 'abirecords', 'reflected': 'reflected', 'dereferenced': 'dereferenced',
            'unwrapped': 'unwrapped', 'ctext': 'ctexts', 'escaped': 'escaped', 'cdef': 'cdefs', 'cside': 'csides',
            'registrar': 'registrars', 'fholder': 'holders', 'raiitype': 'raiitypes',
            'part': 'parts', 'released': 'parts'}

    def __init__(self, blocks):
        self.parameters, self.returns, self.forwarded = {}, {}, collections.defaultdict(set)
        self.members, self.entries, self.slots, self.deleters = {}, set(), set(), set()
        self.checked, self.tested, self.used, self.abirecords = set(), set(), set(), set()
        self.reflected, self.dereferenced, self.parts, self.escaped = set(), set(), set(), set()
        self.bodies, self.raii, self.unwrapped, self.ctexts, self.opaque = {}, set(), set(), set(), set()
        self.cdefs, self.csides, self.registrars, self.holders, self.raiitypes = set(), set(), set(), set(), set()
        self.instances, self.lambdas = collections.defaultdict(list), {}
        self.types, self.references = collections.defaultdict(set), collections.defaultdict(set)
        self.instance_types, self.acquired = collections.defaultdict(set), collections.defaultdict(set)
        self.tables, self.fnargs           = collections.defaultdict(set), collections.defaultdict(set)
        self.aliases, self.named           = {}, {}
        self.instantiated, self.patterns   = set(), set()
        self.deduced                       = set()
        self.statements = collections.defaultdict(lambda: collections.defaultdict(set))
        self.registrations = collections.defaultdict(lambda: collections.defaultdict(set))
        for block in blocks:
            self.add(block)
        self.settle()

    def add_parameter(self, block):
        parameter = block['param']
        call      = block.get('pclass') if 'pcall' in block else None
        self.parameters[parameter] = (block['pfn'], block.get('plambda'), call, block.get('pclass'))
        self.instances[parameter].append((block.get('pc'), block.get('precord'), block.get('tparm')))
        if 'pvoid' in block:
            self.opaque.add(parameter)
        if 'praii' in block:
            self.raii.add(block['pfn'])

    def add(self, block):
        if 'param' in block:
            self.add_parameter(block)
        elif 'ret' in block:
            self.returns[block['ret']] = tuple(block.get(name) for name in ('rclass', 'rloc', 'rexport', 'plambda'))
            (self.instantiated if 'rinst' in block else self.patterns).add(block['ret'])
        elif 'forwarded' in block:
            self.forwarded[block['forwarded']].add(block['into'])
        elif 'field' in block:
            self.members[block['field']] = block['fclass']
        elif 'bodied' in block:
            self.bodies[block['bodied']] = block['body']
        elif 'rslot' in block:
            self.acquired[block['rslot']].add(block['acquirer'])
        for name in self.SETS.keys() & block.keys():
            getattr(self, self.SETS[name]).add(block[name])
        self.slots.update(block[name] for name in ('slot', 'lslot') if name in block)
        self.add_details(block)

    def add_return_statement(self, block):
        if 'rdeduced' in block:
            self.deduced.add(block['rfn'])
        self.statements[block['rfn']][block['rstmt']].add((block.get('ftable'), block.get('relayed')))

    def add_details(self, block):
        if 'abitable' in block:
            self.tables[block['abitable']].add(block['type'])
        elif 'fnarg' in block:
            self.fnargs[block['fnarg']].add(block.get('ftable'))
        elif 'rstmt' in block and 'rfn' in block:
            self.add_return_statement(block)
        elif 'alias' in block:
            self.aliases[block['alias']] = block['aliased']
        elif 'type' in block and (typed := block.get('typed') or block.get('rloc')):
            (self.instance_types if 'tinst' in block else self.types)[typed].add(block['type'])
            if 'talias' in block:
                self.named[typed] = block['talias']
        if 'plambda' in block:
            enclosing = (block['lfn'], block.get('lclass'), None) if 'lfn' in block else (None, None, block.get('lvar'))
            self.lambdas[block['plambda']] = enclosing
        if 'referee' in block:
            self.references[block['referee']].add(tuple(block.get(name) for name in SITE))
        for name in ('regfn', 'regvoid'):
            if name in block:
                self.registrations[block['regrecord']][name].add(block[name])

    def settle(self):
        """What needs every unit: a C side declared anywhere, a registrar defined in another unit than its RAII type."""
        declared      = {declared_name(side) for side in self.csides}
        registrars    = {self.function(found) for found in self.registrars}
        self.entries |= {definition for definition in self.cdefs if declared_name(definition) in declared}
        self.slots   |= {slot for slot, acquirers in self.acquired.items()
                         if any(self.function(acquirer) in registrars for acquirer in acquirers)}
        self.escaped |= {argument for argument, tables in self.fnargs.items()
                         if not all(self.abi_functions(table) for table in tables)}

    def function(self, location):
        """A function by its definition's body, so a forward declaration and its definition are one function."""
        return self.bodies.get(location, location) if location is not None else None

    def one_of(self, location, functions):
        return self.function(location) in {self.function(found) for found in functions}

    @functools.cached_property
    def raii_names(self):
        return frozenset(class_name(found) for found in self.raiitypes)

    def pointer(self, location):
        """A pointer anywhere in the canonical type, at any depth, an RAII type's own arguments apart."""
        types = self.types.get(location, set()) | self.instance_types.get(location, set())
        return location in self.holders or any(self.pointer_text(text, location) for text in types)

    def pattern_pointer(self, location):
        return any(self.pointer_text(text, location) for text in self.types.get(location, set()))

    def pointer_text(self, text, location):
        """A pointer in a canonical type's text, an RAII type's arguments and, in the ABI's symbol table, the function
        types its `decltype`s name apart."""
        for function in self.abi_functions(location):
            text = text.replace(function_pointer(function), '')
        return pointer_text(text, self.raii_names)

    def judged_returns(self):
        """Every return, an out-of-line member's instantiation apart when its pattern is judged at its own location."""
        patterns = {self.function(function) for function in self.patterns}
        return {function: found for function, found in self.returns.items()
                if function in self.patterns or self.function(function) not in patterns}

    @functools.cached_property
    def abi_returns(self):
        """Relays with a deduced return type (`-> decltype(auto)`) each of whose returns is a call through the ABI's
        table, or to a function already one; a spelled pointer return is judged whatever it returns."""
        statements = collections.defaultdict(list)
        for function, found in self.statements.items():
            if function in self.deduced:
                statements[self.function(function)] += found.values()
        found = set()
        while grown := {function for function, returned in statements.items() if function not in found
                        and all(any(self.abi_functions(table) or self.function(relayed) in found
                                    for table, relayed in evidence) for evidence in returned)}:
            found |= grown
        return found

    def abi_functions(self, location):
        """The ABI function types a declaration's table is spelled from: `decltype(&abi_function)` in its own type or
        in an alias it is declared through; none for any other declaration, whatever its type."""
        while location is not None and location not in self.tables:
            location = self.named.get(location) or self.aliases.get(location)
        return self.tables.get(location, frozenset())

    def type_of(self, location):
        """The canonical type, a template's as its pattern declares it, so an instantiation cannot change the key."""
        if pattern := self.types.get(location):
            return ' | '.join(sorted(pattern))
        if location in self.returned_types:
            return returned_type(location)
        return spelled_type(location)

    @functools.cached_property
    def returned_types(self):
        return {returned for _, returned, _, _ in self.returns.values()}

    def registered(self):
        """A registration is one callback and the one `void*` it hands back: exactly one of each in its record."""
        return {field for fields in self.registrations.values()
                if len(fields['regfn']) == 1 and len(fields['regvoid']) == 1
                for field in fields['regfn'] | fields['regvoid']}

    def same(self, parameter):
        """A parameter by its function's definition and its name, so a declaration's is its definition's."""
        owner = self.parameters.get(parameter)
        return (self.function(owner[0]), parameter_name(parameter)) if owner else parameter

    @functools.cached_property
    def tests(self):
        return {self.same(parameter) for parameter in self.tested | self.checked}

    @functools.cached_property
    def onward(self):
        found = collections.defaultdict(set)
        for parameter, targets in self.forwarded.items():
            found[self.same(parameter)] |= {self.same(target) for target in targets}
        return found

    def guarded(self, parameter):
        """Tested for null in its own body, or handed straight to a parameter that is, declarations included."""
        seen, pending = set(), [self.same(parameter)]
        while pending:
            if (current := pending.pop()) in self.tests:
                return True
            seen.add(current)
            pending += self.onward.get(current, set()) - seen
        return False

    def unwrapped_use(self, parameter):
        """A use that reads the pointee: any use of a typed pointer; a cast of an opaque one, or its handing to foreign
        code; an opaque `void*` only handed back to project code is never read."""
        return parameter in self.used and (parameter not in self.opaque or parameter in self.unwrapped)

    def transparent(self, parameter):
        """A pointer only by instantiation whose every use hands it to a project function's parameter, judged there."""
        return not self.pattern_pointer(parameter) and parameter not in self.escaped

    def c_pointee(self, parameter):
        """Every instantiation's pointee is `void`, a character, a foreign record or one an ABI typedef names; a
        template parameter qualifies only through its concept, never as a bare `typename`."""
        def c_instance(instance):
            c_type, record, parameter_type = instance
            return c_type is not None or record in self.abirecords or constrained(parameter_type)
        return all(c_instance(instance) for instance in self.instances[parameter])

    @functools.cached_property
    def pointers(self):
        return [(parameter, owner) for parameter, owner in self.parameters.items() if self.pointer(parameter)]

    def judged(self):
        return [(parameter, owner) for parameter, owner in self.pointers if not self.transparent(parameter)]

    def grouped(self):
        """The judged parameters of each function outside a lambda."""
        found = collections.defaultdict(list)
        for parameter, (function, closure, _, _) in self.judged():
            if closure is None:
                found[self.function(function)].append(parameter)
        return found

    def candidates(self, root):
        holders = {self.function(function) for _, (function, _, _, _) in self.judged()
                   if module(pathlib.Path(function.path).relative_to(root)) in ABI_HOLDERS}
        return {function for function, parameters in self.grouped().items()
                if function in holders and all(self.c_pointee(p) and self.guarded(p) for p in parameters)}

    def sites(self, function):
        return set().union(*(found for referee, found in self.references.items() if self.function(referee) == function))

    def called_from(self, function, callers):
        """Every reference to the function sits in an export, a slot or an adapter already rooted in one."""
        sites = self.sites(function)
        return bool(sites) and all(lambda_site in self.slots or self.function(function_site) in callers
                                   for function_site, lambda_site, _ in sites)

    def only_through(self, function, callers):
        """Every reference to the function sits in one of the callers or in itself, in a body or a lambda it holds."""
        sites = self.sites(function)
        return bool(sites) and all(self.function(outer) in callers | {function} for _, _, outer in sites)

    @functools.cached_property
    def trampolines(self):
        """An ABI's slots and the pass-throughs every reference to which sits in one already reached."""
        judged  = {self.function(owner[0]) for _, owner in self.judged()}
        passing = {self.function(owner[0]) for _, owner in self.pointers} - judged
        reached = {self.function(found) for found in self.entries | self.slots}
        while grown := {function for function in passing - reached if self.only_through(function, reached)}:
            reached |= grown
        return reached

    def conversions(self):
        """A slot's pointer turned into a reference: a template reached only through the slot's pass-throughs, each
        pointer one only by instantiation, tested for null and dereferenced."""
        return {function for function, parameters in self.grouped().items()
                if all(not self.pattern_pointer(p) and self.guarded(p) and p in self.dereferenced for p in parameters)
                and self.only_through(function, self.trampolines)}

    def adapters(self, root):
        """Boundary adapters by kind: in a module holding an ABI, every pointer parameter a C type tested for null
        before use, and reached only from the ABI's own signatures or a C string's one conversion to an optional."""
        roots      = {self.function(found) for found in self.entries | self.slots}
        candidates = self.candidates(root)
        found      = set()
        while grown := {function for function in candidates - found if self.called_from(function, found | roots)}:
            found |= grown
        found |= {function for function in candidates if self.one_of(function, self.ctexts)}
        return found | self.conversions()

    def allowed(self, function, closure):
        """A signature an ABI writes: a C export or main, a slot a C table or C call takes, a reflect tag, a function
        an RAII type or a deleter is made of."""
        if closure is not None:
            return closure in self.slots
        return (self.one_of(function, self.entries | self.slots | self.parts | self.raii)
                or self.reflect_tag(function))

    def released(self, function):
        """Part of an RAII type or a deleter, which is only ever handed the live handle it owns."""
        return self.one_of(function, self.parts | self.raii)

    def reflect_tag(self, function):
        """The reflect protocol's shape, which the query matched, and a tag that is only a type: never read."""
        return function in self.reflected and not any(
            parameter in self.used for parameter, (owner, _, _, _) in self.parameters.items() if owner == function)

    def deleter(self, call_class):
        """A unique_ptr deleter's call operator, a class template's keyed by its pattern, where its parameters live."""
        lines = {(found.path, found.line) for found in self.deleters}
        return call_class is not None and (call_class.path, call_class.line) in lines

    def owner(self, function, closure, record):
        """`Class::Function`, or a lambda as `Function::lambda#n` counted in its enclosing function."""
        if closure is None:
            return function_name(function, record)
        enclosing = self.lambdas.get(closure, (None, None, None))
        siblings  = sorted(lambda_ for lambda_ in self.pointer_lambdas if self.lambdas.get(lambda_) == enclosing)
        return f'{enclosing_name(*enclosing)}::lambda#{siblings.index(closure) + 1}'

    @functools.cached_property
    def pointer_lambdas(self):
        """The lambdas a pointer parameter or return sits in, the ones a lambda's key counts."""
        parameters = {lambda_ for _, (_, lambda_, _, _) in self.pointers}
        returned   = {lambda_ for _, returned, _, lambda_ in self.returns.values() if self.pointer(returned)}
        return (parameters | returned) - {None}


def pointer_text(text, raii_names):
    """A canonical type's text names a pointer outside the arguments of an RAII type, which owns its handle; a member
    pointer's `Class::*` is an offset, its function type's parameters are not."""
    for name in raii_names:
        text = strip_arguments(text, name)
    return '*' in text.replace('::*', '')


def function_pointer(function_type):
    """The text of a pointer to a function type: `int (*)(int)` for `int (int)`."""
    split = function_type.index('(')
    return f'{function_type[:split]}(*){function_type[split:]}'


def strip_arguments(text, name):
    """The text with every `name<...>` reduced to `name`."""
    pattern = re.compile(rf'\b{re.escape(name)}<')
    while found := pattern.search(text):
        text = text[:found.end() - 1] + text[closing(text, found.end() - 1):]
    return text


def closing(text, start):
    """The index just past the `>` that closes the `<` at start."""
    depth = 0
    for index in range(start, len(text)):
        depth += {'<': 1, '>': -1}.get(text[index], 0)
        if depth == 0:
            return index + 1
    return len(text)


def source_text(location, lines=1):
    """The source from a location to the end of its line, and the next lines when asked."""
    text = file_lines(location.path)[location.line - 1:]
    return '\n'.join([text[0][location.column - 1:], *text[1:lines]])


def declaration_head(location):
    """A declaration's text up to where the declarator ends."""
    text, depth = source_text(location, lines=3), 0
    for index, character in enumerate(text):
        depth += {'<': 1, '(': 1, '[': 1, '>': -1, ')': -1, ']': -1}.get(character, 0)
        if depth < 0 or (depth == 0 and character in ',=;{'):
            return text[:index]
    return text


def parameter_name(location):
    """The name a parameter declaration at this location declares: its last word before the list goes on."""
    return (re.findall(r'\w+', declaration_head(location)) or ['?'])[-1]


def spelled_type(location):
    """A declaration's type as its source spells it, the name dropped."""
    head = declaration_head(location)
    return ' '.join(head[:head.rfind(parameter_name(location))].split())


def returned_type(location):
    """A return type as its source spells it, up to the body or the end of the declaration."""
    return ' '.join(HEAD_END.split(source_text(location, lines=3), maxsplit=1)[0].split())


def constrained(parameter_type):
    """A template type parameter declared with a concept, `IoBuffer ByteTy`, rather than `typename` or `class`."""
    return parameter_type is not None and not UNCONSTRAINED.match(source_text(parameter_type))


def class_name(record):
    """The name a class head declares, its template head skipped wherever that head sits."""
    text = source_text(record, lines=HEAD_LINES)
    if TEMPLATE_HEAD.match(text):
        text = text[closing(text, text.index('<')):]
    found = CLASS_HEAD.match(text)
    return found[1] if found else '<anonymous>'


def owner_name(record):
    """The class a declaration belongs to, as its head names it; empty outside a class."""
    return f'{class_name(record)}::' if record is not None else ''


def function_name(function, record=None):
    """`Class::Function`, the key a finding keeps so it cannot move to another function in its file."""
    return owner_name(record) + declared_name(function)


def enclosing_name(function, record, variable):
    """What holds a lambda: its function, else the variable it initialises, else its file."""
    if function is not None:
        return function_name(function, record)
    return parameter_name(variable) if variable is not None else '<file>'


def load(root, build):
    return Facts(query_units(root, build))


def finding(root, kind, location, type_text, owner):
    path = pathlib.Path(location.path).relative_to(root)
    return Finding(path, location.line, location.column, kind, type_text, owner)


def parameter_findings(root, facts):
    found, adapters = [], facts.adapters(root)
    for parameter, (function, closure, call, record) in facts.judged():
        allowed   = facts.function(function) in adapters or facts.allowed(function, closure) or facts.deleter(call)
        type_text = facts.type_of(parameter)
        owner     = facts.owner(function, closure, record)
        if not allowed:
            found.append(finding(root, 'parameters', parameter, type_text, owner))
        elif facts.unwrapped_use(parameter) and not facts.guarded(parameter) and not facts.released(function):
            found.append(finding(root, 'unchecked', parameter, type_text, owner))
        if parameter in facts.checked and not allowed:
            found.append(finding(root, 'null checks', parameter, type_text, owner))
    return found


def findings_from(root, facts):
    registered = facts.registered()
    found      = parameter_findings(root, facts)
    found     += [finding(root, 'members', member, facts.type_of(member), class_name(record))
                  for member, record in facts.members.items() if member not in registered and facts.pointer(member)]
    found     += [finding(root, 'returns', function, facts.type_of(returned), facts.owner(function, closure, record))
                  for function, (record, returned, export, closure) in facts.judged_returns().items()
                  if export not in facts.entries and not facts.allowed(function, closure) and facts.pointer(returned)
                  and facts.function(function) not in facts.abi_returns]
    return sorted(set(found))


def build_directory():
    """The gate's build directory: buildutil names it; a lint that guessed would judge another build."""
    if not os.environ.get('BUILDUTIL_BUILD_DIR'):
        raise SystemExit('pointers: BUILDUTIL_BUILD_DIR is not set; run through `./buildutil test`')
    return pathlib.Path(os.environ['BUILDUTIL_BUILD_DIR'])


def findings(root, build=None):
    return findings_from(root, load(root, build or build_directory()))


def module(path):
    """The module a file belongs to, its tests apart: `sdl-rdp/video`, `sdl-rdp/video tests`."""
    parts = path.relative_to('sources').parts
    depth = 2 if parts[0] in ('sdl-rdp', 'SDL3') and len(parts) > 2 else 1
    name  = '/'.join(parts[:depth])
    return f'{name} tests' if shape.test_support(path) else name


def table(found):
    counts = collections.defaultdict(collections.Counter)
    for item in found:
        counts[module(item.path)][item.kind] += 1
    width  = max((len(name) for name in counts), default=6)
    lines  = [f'{"module":<{width}}  ' + '  '.join(f'{kind:>11}' for kind in KINDS)]
    lines += [f'{name:<{width}}  ' + '  '.join(f'{counts[name][kind]:>11}' for kind in KINDS)
              for name in sorted(counts)]
    totals = sum(counts.values(), collections.Counter())
    lines.append(f'{"total":<{width}}  ' + '  '.join(f'{totals[kind]:>11}' for kind in KINDS))
    return lines


class Baseline(NamedTuple):
    """The modules the lint lists as unfinished by count; every other module by finding, so a removed pointer frees no
    room and a finding cannot move to another function."""
    counts:   dict
    findings: collections.Counter


def read_baseline(path):
    """`count<TAB>module<TAB>kind<TAB>n` and `finding<TAB>path<TAB>kind<TAB>owner<TAB>canonical type` lines; a count
    for a module the lint does not list as unfinished fails."""
    rows   = [line.split('\t') for line in path.read_text().splitlines() if line.strip()]
    counts = {(name, kind): int(count) for form, name, kind, count in (row for row in rows if row[0] == 'count')}
    held   = collections.Counter(tuple(row[1:]) for row in rows if row[0] == 'finding')
    if stray := sorted({name for name, _ in counts} - COUNTED):
        raise SystemExit('pointers: held by count but finished: ' + ', '.join(stray))
    return Baseline(counts, held)


def module_counts(found, counted):
    """Findings per (module, kind) for the modules the baseline holds by count."""
    return collections.Counter((module(item.path), item.kind) for item in found if module(item.path) in counted)


def regressions(found, baseline):
    """Any difference from the baseline, either way: a new pointer fails, and so does a stale entry."""
    counts  = module_counts(found, COUNTED)
    held    = collections.Counter(item.key() for item in found if module(item.path) not in COUNTED)
    lines   = [f'{name}: {kind} {counts[(name, kind)]} != {baseline.counts.get((name, kind), 0)}'
               for name, kind in sorted(counts.keys() | baseline.counts.keys())
               if counts[(name, kind)] != baseline.counts.get((name, kind), 0)]
    lines  += [f'{path}: new {kind} ({type_text}) in {owner}'
               for path, kind, owner, type_text in sorted(held - baseline.findings)]
    lines  += [f'{path}: {kind} ({type_text}) in {owner} is gone; drop it from the baseline'
               for path, kind, owner, type_text in sorted(baseline.findings - held)]
    return lines


def baseline_lines(found):
    """The current findings in the baseline's form, the unfinished modules as counts."""
    counts  = module_counts(found, COUNTED)
    lines   = [f'count\t{name}\t{kind}\t{count}' for (name, kind), count in sorted(counts.items())]
    return lines + sorted('\t'.join(('finding', *item.key())) for item in found if module(item.path) not in COUNTED)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--table', action='store_true', help='print the counts by module instead of the findings')
    parser.add_argument('--baseline', type=pathlib.Path, help='fail on any difference from this baseline')
    parser.add_argument('--rebaseline', action='store_true', help='print the findings in the baseline form')
    args  = parser.parse_args(argv)
    found = findings(ROOT)
    if args.rebaseline:
        lines, found = baseline_lines(found), []
    elif args.baseline:
        lines = found = regressions(found, read_baseline(args.baseline))
    else:
        lines = table(found) if args.table else (item.report() for item in found)
    for line in lines:
        print(line)
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
