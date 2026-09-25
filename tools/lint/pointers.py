#!/usr/bin/env python3
"""Refuse pointers outside the signatures an ABI writes, judged by clang on canonical types."""
import argparse
import collections
import concurrent.futures
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
BINDING         = re.compile(r'^(/[^:]+):(\d+):(\d+): note: "(\w+)" binds here')
PRINTED         = re.compile(r'^Binding for "(\w+)":$')
DECLARED        = re.compile(r'([A-Za-z_]\w*)\s*\(')
NOT_NAMES       = re.compile(r'\[\[.*?\]\]|"[^"]*"')
NOT_DECLARED    = frozenset(('decltype', 'alignas', '__attribute__', '__declspec'))
GCC_ONLY        = ('-fconstexpr-ops-limit=',)
PLATFORM_TAGS   = frozenset(('win32', 'linux', 'macos', 'emscripten', 'posix', 'apple', 'native'))
LIVE_PLATFORMS  = frozenset(('native', 'posix', 'linux'))
CARRIED_ENTRIES = re.compile(r'^(BUILD_TESTING|CMAKE_BUILD_TYPE|CMAKE_TOOLCHAIN_FILE|BUILDUTIL_\w+|\w+_OPTION_\w+)$')
DEEP            = '''type(anyOf({inner}, referenceType(pointee({inner})), arrayType(hasElementType({inner})),
    recordType(hasDeclaration(classTemplateSpecializationDecl(hasAnyTemplateArgument(refersToType({inner})))))))'''
QUERIES         = r'''
set traversal AsIs
set output diag
set bind-root false
let own allOf(unless(isExpansionInSystemHeader()), isExpansionInFileMatching("^{sources}"),
              unless(isExpansionInFileMatching("^{abi}")))
let foreign anyOf(unless(isExpansionInFileMatching("^{sources}")), isExpansionInFileMatching("^{abi}"))
let ptr hasCanonicalType(pointerType())
let t0 pointerType()
let t1 {deep1}
let t2 {deep2}
let t3 {deep3}
let anyptr hasCanonicalType(t3)
let crecord recordDecl(unless(hasAncestor(namespaceDecl())), foreign)
let owner hasAncestor(functionDecl(unless(isImplicit()),
                                   optionally(cxxMethodDecl(ofClass(cxxRecordDecl().bind("pclass")))),
                                   optionally(
                                       cxxMethodDecl(hasOverloadedOperatorName("()")).bind("pcall"))).bind("pfn"))
let closure optionally(hasAncestor(lambdaExpr().bind("plambda")))
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
               optionally(hasAncestor(lambdaExpr().bind("sitelambda"))))
let pfield allOf(own, unless(isImplicit()), anyOf(hasType(anyptr), hasType(holder)))
let pret allOf(own, unless(isImplicit()), unless(isInstantiated()), returns(anyptr))
match parmVarDecl(own, unless(isInstantiated()), hasType(anyptr), owner, closure).bind("param")
match fieldDecl(pfield).bind("field")
match functionDecl(pret, optionally(hasReturnTypeLoc(typeLoc().bind("rloc"))),
                   optionally(cxxMethodDecl(ofClass(cxxRecordDecl().bind("rclass")))),
                   optionally(hasAncestor(functionDecl(isExternC()).bind("rexport")))).bind("ret")
match functionDecl(own, isMain()).bind("entry")
match functionDecl(own, isDefinition(), isExternC()).bind("cdef")
match binaryOperator(own, hasOperatorName("="), hasLHS(memberExpr(member(fieldDecl(hasParent(crecord))))),
                     hasRHS(flow))
match callExpr(own, callee(functionDecl(isExternC(), foreign)), forEachArgumentWithParam(flow, parmVarDecl()))
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
match cxxConstructExpr(own, hasDeclaration(cxxConstructorDecl(ofClass(matchesName("^::std::unique_ptr$")))),
                       hasArgument(0, ignoringParenImpCasts(declRefExpr(to(pparam.bind("adopted"))))))
match callExpr(own, forEachArgumentWithParam(ignoringParenImpCasts(declRefExpr(to(pparam.bind("forwarded")))),
                                             parmVarDecl().bind("into")))
match parmVarDecl(own, unless(isInstantiated()), cpointee).bind("cparam")
match parmVarDecl(own, unless(isInstantiated()), hasType(hasCanonicalType(pointerType(pointee(hasDeclaration(
    recordDecl(unless(hasAncestor(namespaceDecl()))).bind("precord"))))))).bind("recparam")
match typedefNameDecl(foreign, hasType(hasCanonicalType(recordType(hasDeclaration(recordDecl(own).bind("abirecord"))))))
match declRefExpr(own, to(functionDecl(hasAnyParameter(pparam)).bind("referee")), site)
match fieldDecl(own, hasType(fnptr), hasParent(recordDecl().bind("regrecord"))).bind("regfn")
match fieldDecl(own, hasType(voidptr), hasParent(recordDecl().bind("regrecord"))).bind("regvoid")
enable output print
let typed hasType(hasCanonicalType(qualType().bind("type")))
match parmVarDecl(own, unless(isInstantiated()), hasType(anyptr), typed).bind("typed")
match fieldDecl(pfield, typed).bind("typed")
match functionDecl(pret, hasReturnTypeLoc(typeLoc().bind("rloc")), returns(hasCanonicalType(qualType().bind("type"))))
'''
C_SIDE          = '''set traversal AsIs
set output diag
set bind-root false
match functionDecl(isExternC(), anyOf(unless(isExpansionInFileMatching("^{sources}")),
                                      isExpansionInFileMatching("^{abi}")), hasAnyName({names})).bind("cside")
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
    where:  str

    def report(self):
        return f'{self.path}:{self.line}:{self.column}: {KIND_TEXT[self.kind]} ({self.type}) {self.where}'

    def key(self):
        return (str(self.path), self.kind, self.type)


KIND_TEXT = {'parameters':  'pointer parameter',
             'members':     'pointer member',
             'returns':     'pointer return',
             'null checks': 'null-check contract on a parameter outside the ABI boundary',
             'unchecked':   'boundary pointer used without a null test'}


def query_text(root):
    sources = re.escape(f'{root}/sources/')
    abi     = re.escape(f'{root}/sources/sdl-rdp/abi/')
    deep    = {'deep1': DEEP.format(inner='t0'), 'deep2': DEEP.format(inner='t1'), 'deep3': DEEP.format(inner='t2')}
    text    = QUERIES.replace('{sources}', sources).replace('{abi}', abi)
    for name, value in deep.items():
        text = text.replace('{' + name + '}', value)
    return text


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
    database = target / 'compile_commands.json'
    if database.exists() and database.stat().st_mtime >= (build / 'CMakeCache.txt').stat().st_mtime:
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
    """A digest of the command, the queries and every input's size and time; no recorded inputs, no key."""
    if inputs is None:
        return None
    digest = hashlib.sha256(json.dumps(unit['arguments']).encode() + queries.encode())
    for path in [unit['file'], *inputs]:
        status = (pathlib.Path(unit['directory']) / path).stat()
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


def declared_name(location):
    """The name a function declaration at this location declares: the first word called, attributes aside."""
    line = pathlib.Path(location[0]).read_text(errors='replace').splitlines()[location[1] - 1][location[2] - 1:]
    return next(word for word in DECLARED.findall(NOT_NAMES.sub(' ', line)) if word not in NOT_DECLARED)


def query(tool, database, queries, unit):
    run = subprocess.run([tool, '-p', str(database), '-f', str(queries), unit['file']], capture_output=True, text=True)
    if run.returncode or ('error:' in run.stderr and 'Match #' not in run.stdout):
        raise SystemExit(f'pointers: clang could not parse {unit["file"]}:\n{run.stdout[-1000:]}{run.stderr[-2000:]}')
    return matches(run.stdout)


def run_unit(tool, work, root, unit):
    """The unit's matches; an `extern "C"` definition is an entry only where the C side declares its name."""
    found = query(tool, work, work / 'queries.txt', unit)
    names = sorted({declared_name(block['cdef']) for block in found if 'cdef' in block})
    if names:
        sides = work / f'c-side-{hashlib.sha256(unit["file"].encode()).hexdigest()[:16]}.txt'
        sides.write_text(C_SIDE.replace('{sources}', re.escape(f'{root}/sources/'))
                         .replace('{abi}', re.escape(f'{root}/sources/sdl-rdp/abi/'))
                         .replace('{names}', ', '.join(f'"{name}"' for name in names)))
        declared = {declared_name(block['cside']) for block in query(tool, work, sides, unit)}
        sides.unlink()
        found   += [{'entry': block['cdef']} for block in found
                    if 'cdef' in block and declared_name(block['cdef']) in declared]
    return found


def cached_keys(units, text, build):
    inputs = dependencies(build)
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 1) as pool:
        listed = pool.map(lambda unit: inputs.get(object_of(unit)) or preprocessed_inputs(unit), units)
        return {unit['file']: unit_key(unit, text, recorded) for unit, recorded in zip(units, listed)}


def query_units(root, build):
    """Every translation unit's matches, parsed again only where the unit or one of its inputs changed."""
    work = build / 'pointers'
    work.mkdir(exist_ok=True)
    units, text = translation_units(root, build), query_text(root)
    (work / 'compile_commands.json').write_text(json.dumps(units))
    (work / 'queries.txt').write_text(text)
    cache_path = work / 'cache.json'
    cache      = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    keys, tool = cached_keys(units, text + C_SIDE, build), clang_query()
    stale = [unit for unit in units
             if not keys[unit['file']] or cache.get(unit['file'], {}).get('key') != keys[unit['file']]]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 1) as pool:
        for unit, found in zip(stale, pool.map(lambda unit: run_unit(tool, work, root, unit), stale)):
            cache[unit['file']] = {'key': keys[unit['file']], 'matches': found}
    cache = {unit['file']: cache[unit['file']] for unit in units}
    cache_path.write_text(json.dumps(cache))
    return [{name: value if name == 'type' else Location(*value) for name, value in block.items()}
            for entry in cache.values() for block in entry['matches']]


class Facts:
    """What the matches say, deduplicated across translation units by declaration location."""

    SETS = {'field': 'members', 'entry': 'entries', 'deleter': 'deleters', 'checked': 'checked', 'tested': 'tested',
            'adopted': 'tested', 'used': 'used', 'cparam': 'cparams', 'abirecord': 'abirecords'}

    def __init__(self, blocks):
        self.parameters, self.returns, self.forwarded = {}, {}, collections.defaultdict(set)
        self.members, self.entries, self.slots, self.deleters, self.exported = set(), set(), set(), set(), set()
        self.checked, self.tested, self.used, self.cparams, self.abirecords = set(), set(), set(), set(), set()
        self.types, self.records, self.references = collections.defaultdict(set), {}, collections.defaultdict(set)
        self.registrations = collections.defaultdict(lambda: collections.defaultdict(set))
        for block in blocks:
            self.add(block)

    def add(self, block):
        if 'param' in block:
            call = block.get('pclass') if 'pcall' in block else None
            self.parameters[block['param']] = (block['pfn'], block.get('plambda'), call)
        elif 'ret' in block:
            self.returns[block['ret']] = (block.get('rclass'), block.get('rloc'), block.get('rexport'))
        elif 'forwarded' in block:
            self.forwarded[block['forwarded']].add(block['into'])
        for name in self.SETS.keys() & block.keys():
            getattr(self, self.SETS[name]).add(block[name])
        self.slots.update(block[name] for name in ('slot', 'lslot') if name in block)
        self.add_details(block)

    def add_details(self, block):
        if 'type' in block and (typed := block.get('typed') or block.get('rloc')):
            self.types[typed].add(block['type'])
        if 'recparam' in block:
            self.records[block['recparam']] = block['precord']
        if 'referee' in block:
            self.references[block['referee']].add((block.get('sitefn'), block.get('sitelambda')))
        for name in ('regfn', 'regvoid'):
            if name in block:
                self.registrations[block['regrecord']][name].add(block[name])

    def type_of(self, location):
        """The canonical type, every instantiation's where a template's member is typed per instantiation."""
        return ' | '.join(sorted(self.types.get(location, set()))) or '?'

    def registered(self):
        """A registration is one callback and the one `void*` it hands back: exactly one of each in its record."""
        return {field for fields in self.registrations.values()
                if len(fields['regfn']) == 1 and len(fields['regvoid']) == 1
                for field in fields['regfn'] | fields['regvoid']}

    def guarded(self, parameter, seen=frozenset()):
        """Tested for null in its own body, or handed straight to a parameter that is."""
        if parameter in self.tested or parameter in self.checked:
            return True
        onward = self.forwarded.get(parameter, set()) - seen
        return any(self.guarded(into, seen | {parameter}) for into in onward)

    def c_pointee(self, parameter):
        """`void`, a character, a record declared outside the project, or the record an ABI typedef names."""
        return parameter in self.cparams or self.records.get(parameter) in self.abirecords

    def candidates(self, root):
        grouped = collections.defaultdict(list)
        for parameter, (function, closure, _) in self.parameters.items():
            grouped[(function, closure)].append(parameter)
        return {function for (function, closure), parameters in grouped.items()
                if closure is None and module(pathlib.Path(function.path).relative_to(root)) in ABI_HOLDERS
                and all(self.c_pointee(p) and self.guarded(p) for p in parameters)}

    def called_from(self, function, callers):
        """Every reference to the function sits in an export, a slot or an adapter already rooted in one."""
        sites = self.references.get(function, set())
        return bool(sites) and all(lambda_site in self.slots or function_site in callers
                                   for function_site, lambda_site in sites)

    def adapters(self, root):
        """Boundary adapters by kind: in a module holding an ABI, reached only from the ABI's own signatures, every
        pointer parameter a C type tested for null before use."""
        candidates, found = self.candidates(root), set()
        while grown := {function for function in candidates - found
                        if self.called_from(function, found | self.entries | self.slots)}:
            found |= grown
        return found

    def allowed(self, function, closure):
        """A signature an ABI writes: a C export or main, a slot a C table or C call takes."""
        if closure is not None:
            return closure in self.slots
        return function in self.entries or function in self.slots

    def deleter(self, call_class):
        """A unique_ptr deleter's call operator, a class template's keyed by its pattern, where its parameters live."""
        lines = {(found.path, found.line) for found in self.deleters}
        return call_class is not None and (call_class.path, call_class.line) in lines


def load(root, build):
    return Facts(query_units(root, build))


def finding(root, kind, location, type_text, where):
    path = pathlib.Path(location.path).relative_to(root)
    return Finding(path, location.line, location.column, kind, type_text, where)


def parameter_findings(root, facts):
    found, adapters = [], facts.adapters(root)
    for parameter, (function, closure, call) in facts.parameters.items():
        allowed   = function in adapters or facts.allowed(function, closure) or facts.deleter(call)
        type_text = facts.type_of(parameter)
        if not allowed:
            found.append(finding(root, 'parameters', parameter, type_text, f'of the function at line {function.line}'))
        elif parameter in facts.used and not facts.guarded(parameter):
            found.append(finding(root, 'unchecked', parameter, type_text, f'of the function at line {function.line}'))
        if parameter in facts.checked and not allowed:
            found.append(finding(root, 'null checks', parameter, type_text, 'is a reference by its own contract'))
    return found


def findings_from(root, facts):
    registered = facts.registered()
    found      = parameter_findings(root, facts)
    found     += [finding(root, 'members', member, facts.type_of(member), 'names a pointer or a C struct')
                  for member in facts.members if member not in registered]
    found     += [finding(root, 'returns', function, facts.type_of(returned), 'hands a pointer out')
                  for function, (_, returned, export) in facts.returns.items()
                  if export not in facts.entries and not facts.allowed(function, None)]
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
    """Part-2 modules by count; every finished module by finding, so a removed pointer frees no room."""
    counts:   dict
    findings: collections.Counter


def read_baseline(path):
    """`count<TAB>module<TAB>kind<TAB>n` and `finding<TAB>path<TAB>kind<TAB>canonical type` lines."""
    rows = [line.split('\t') for line in path.read_text().splitlines() if line.strip()]
    counts = {(first, kind): int(last) for form, first, kind, last in rows if form == 'count'}
    held = collections.Counter((first, kind, last) for form, first, kind, last in rows if form == 'finding')
    return Baseline(counts, held)


def counted_modules(baseline):
    return {name for name, _ in baseline.counts}


def module_counts(found, counted):
    """Findings per (module, kind) for the modules the baseline holds by count."""
    return collections.Counter((module(item.path), item.kind) for item in found if module(item.path) in counted)


def regressions(found, baseline):
    """Any difference from the baseline, either way: a new pointer fails, and so does a stale entry."""
    counted = counted_modules(baseline)
    counts  = module_counts(found, counted)
    held    = collections.Counter(item.key() for item in found if module(item.path) not in counted)
    lines   = [f'{name}: {kind} {counts[(name, kind)]} != {baseline.counts.get((name, kind), 0)}'
               for name, kind in sorted(counts.keys() | baseline.counts.keys())
               if counts[(name, kind)] != baseline.counts.get((name, kind), 0)]
    lines  += [f'{path}: new {kind} ({type_text})' for path, kind, type_text in sorted(held - baseline.findings)]
    lines  += [f'{path}: {kind} ({type_text}) is gone; drop it from the baseline'
               for path, kind, type_text in sorted(baseline.findings - held)]
    return lines


def baseline_lines(found, baseline):
    """The current findings in the baseline's form, the counted modules kept as counts."""
    counted = counted_modules(baseline)
    counts  = module_counts(found, counted)
    lines   = [f'count\t{name}\t{kind}\t{count}' for (name, kind), count in sorted(counts.items())]
    return lines + sorted('\t'.join(('finding', *item.key())) for item in found if module(item.path) not in counted)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--table', action='store_true', help='print the counts by module instead of the findings')
    parser.add_argument('--baseline', type=pathlib.Path, help='fail on any difference from this baseline')
    parser.add_argument('--rebaseline', action='store_true', help='print the findings in the baseline form')
    args  = parser.parse_args(argv)
    found = findings(ROOT)
    if args.baseline and args.rebaseline:
        lines, found = baseline_lines(found, read_baseline(args.baseline)), []
    elif args.baseline:
        lines = found = regressions(found, read_baseline(args.baseline))
    else:
        lines = table(found) if args.table else (item.report() for item in found)
    for line in lines:
        print(line)
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
