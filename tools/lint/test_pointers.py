"""The pointer lint on canonical types: the reviews' evasions fail, the ABI's own signatures pass."""
import collections
import json
import os
import pathlib
import subprocess

import pytest

import pointers

C_HEADER = '''extern "C" {
struct Dev;
struct Table { int (*Init)(struct Dev*); void* user; };
typedef void* HANDLE;
typedef struct { int (*log)(void*, char const*); void* log_user; } config;
int c_register(int (*cb)(void*), void* user);
void c_free(int*);
char const* sdl_export(struct Dev*);
void sdl_close(struct Dev*);
int sdl_chain(struct Dev*);
int sdl_lambda(struct Dev*);
int sdl_marked(struct Dev*);
}
'''
PROBES = '''#include <array>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#include "c.h"
struct Loose { int value; };
namespace probe {
using P = int*;
auto Aliased(P aliased) -> void;
auto Added(std::add_pointer_t<int> added) -> void;
auto Returned() -> int*;
struct Holder { HANDLE event; Table table; int& ref; };
auto Handled(HANDLE handled) -> int { if (!handled) throw 1; return 0; }
auto Init(Dev* overload, int x) -> int { return x; }
auto Init(Dev* slotted) -> int { if (!slotted) return 1; return 0; }
auto Cb(void* registered) -> int { return registered != nullptr; }
auto Helper(int* helper) -> int { return *helper; }
auto Project(int (*passed)(int*)) -> void;
struct Mine { int (*slot)(int*); };
extern "C" auto mine(int (*laundered)(int*)) -> void;
auto Washed(int* washed) -> int { if (!washed) return 0; return *washed; }
template <class T> auto Serve(T* served) -> int { if (!served) return 1; return 0; }
auto Install(Table& t, Mine& m) -> void {
  t.Init = Init;
  m.slot = Helper;
  Project(Helper);
  mine(Washed);
  c_register(Cb, nullptr);
  c_register([](void* lambda_slot) -> int { return lambda_slot == nullptr; }, nullptr);
  auto local = [](int* local_lambda) { return *local_lambda; };
  c_register([](void* forwarding) -> int { return Serve(forwarding); }, nullptr);
  c_register([](void* raw) -> int { return *static_cast<int*>(raw); }, nullptr);
}
struct Frees {
  auto operator()(int* freed) const -> void { if (freed) c_free(freed); }
  auto Other(int* other) const -> void { if (other) c_free(other); }
};
using Owned = std::unique_ptr<int, Frees>;
auto Keep() -> void { Owned const owned{ nullptr }; }
class Registration { int (*_callback)(void*, char const*); void* _user; };
class Smuggling { int (*_hook)(void*); void* _data; void* _smuggled; };
auto Optional(std::optional<int*> optional_param) -> void;
auto Spanned(std::span<int*> span_param) -> void;
auto Arrayed() -> std::array<int*, 3>;
auto Referred(int*& reference_param) -> void;
struct Reflected { friend constexpr auto reflect_scheme(Reflected* reflect_tag); };
enum class Colour { RED };
enum class Hue { WARM };
constexpr auto reflect_scheme([[maybe_unused]] Colour* enum_tag) -> int { return 0; }
constexpr auto reflect_scheme([[maybe_unused]] Loose* loose_tag) -> int { return 0; }
constexpr auto reflect_scheme([[maybe_unused]] Colour* paired_tag, int second) -> int { return second; }
constexpr auto reflect_scheme(Hue* used_tag) -> int { return used_tag == nullptr ? 0 : 1; }
auto reflect_scheme([[maybe_unused]] Reflected* runtime_tag, [[maybe_unused]] Hue hue) -> int { return 0; }
struct Method { constexpr auto reflect_scheme([[maybe_unused]] Method* method_tag) -> int { return 0; } };
constexpr auto other_scheme([[maybe_unused]] Loose* other_tag) -> int { return 0; }
struct Deep {
  std::optional<int*>         optional_member;
  std::vector<int*>           vector_member;
  std::pair<int*, int>        pair_member;
  int*                        array_member[4];
  std::optional<std::vector<P>> nested_member;
};
}
auto main(int argc, char** argv) -> int { return argc; }
'''
ADAPTERS = '''#include <memory>
#include "c.h"
struct Loose { int value; };
auto Checked(Dev* adapted, char const* named) -> int {
  if (!adapted) return -1;
  if (!named) return -1;
  return 0;
}
auto Rooted(Dev* rooted) -> int { if (!rooted) return -1; return Checked(rooted, "x"); }
auto Stray(Dev* stray) -> int { if (!stray) return -1; return 0; }
auto Inner(Dev* inner) -> int { if (!inner) return -1; return 0; }
auto Caller(Dev& dev) -> int { return Stray(&dev); }
template <class T> auto Generic(T* generic) -> int { if (!generic) return -1; return 0; }
auto Project(Loose* project) -> int { if (!project) return -1; return 0; }
extern "C" auto sdl_export(Dev* exported) -> char const* {
  auto text = [](Dev& dev) -> char const* { return "x"; };
  if (!exported) return nullptr;
  Generic(exported);
  Project(nullptr);
  return Checked(exported, "y") ? nullptr : text(*exported);
}
extern "C" auto sdl_close(Dev* closed) -> void { std::unique_ptr<Dev> const owned{ closed }; }
extern "C" auto sdl_chain(Dev* chained) -> int { return Rooted(chained); }
extern "C" auto sdl_lambda(Dev* captured) -> int {
  auto const run = [&] { return Stray(captured) + Inner(captured); };
  return run();
}
#define _Public_(version) __attribute__((visibility("default")))
auto _Public_(7)
    sdl_marked(Dev* marked) -> int { return Rooted(marked); }
auto Unchecked(Dev* loose) -> Dev& { return *loose; }
'''
FACADE = '''#include "c.h"
struct Loose { int value; };
template <class T> auto Tpl(T* tpl) -> int { if (!tpl) return 0; return 1; }
auto Text(char const* text) -> int { if (!text) return 0; return 1; }
auto User(void* user) -> int { if (!user) return 0; return 1; }
auto Record(Loose* record) -> int { if (!record) return 0; return 1; }
extern "C" auto sdl_export(Dev* exported) -> char const*;
auto Use(Dev* dev) -> char const* { return sdl_export(dev); }
'''
PROBE   = 'sources/sdl-rdp/video/probe.cpp'
ADAPTER = 'sources/sdl-rdp/backend/adapter.cpp'
FACADES = 'sources/sdl-rdp/freerdp-facade/facade.cpp'
FILES   = {PROBE: PROBES, ADAPTER: ADAPTERS, FACADES: FACADE}


def locate(text, needle):
    return next(number for number, line in enumerate(text.splitlines(), 1) if needle in line)


def tree(root, files, compiled):
    """A git tree holding the files, with a compile database of the compiled ones."""
    (root / 'include').mkdir()
    (root / 'include/c.h').write_text(C_HEADER)
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text)
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
    build = root / '_build/probe'
    build.mkdir(parents=True)
    database = [{'directory': str(root), 'file': str(root / name),
                 'arguments': ['clang++', '-std=c++23', f'-I{root}/include', '-c', str(root / name)]}
                for name in compiled]
    (build / 'compile_commands.json').write_text(json.dumps(database))
    return build


@pytest.fixture(scope='module')
def found(tmp_path_factory):
    root  = tmp_path_factory.mktemp('tree')
    build = tree(root, FILES, FILES)
    return {(str(item.path), item.line, item.kind) for item in pointers.findings(root, build)}


def at(found, name, needle, kind):
    return (name, locate(FILES[name], needle), kind) in found


@pytest.mark.parametrize('needle, kind', [
    ('Aliased(P aliased)', 'parameters'),
    ('std::add_pointer_t<int> added', 'parameters'),
    ('auto Returned() -> int*', 'returns'),
    ('HANDLE event; Table table', 'members'),
    ('Handled(HANDLE handled)', 'parameters'),
    ('Init(Dev* overload, int x)', 'parameters'),
    ('Helper(int* helper)', 'parameters'),
    ('Project(int (*passed)(int*))', 'parameters'),
    ('int* local_lambda', 'parameters'),
])
def test_the_first_reviews_evasions_are_seen(found, needle, kind):
    assert at(found, PROBE, needle, kind)


@pytest.mark.parametrize('needle, kind', [
    ('optional_param', 'parameters'),
    ('span_param', 'parameters'),
    ('auto Arrayed()', 'returns'),
    ('reference_param', 'parameters'),
    ('optional_member', 'members'),
    ('vector_member', 'members'),
    ('pair_member', 'members'),
    ('array_member', 'members'),
    ('nested_member', 'members'),
])
def test_a_pointer_anywhere_in_the_canonical_type_counts(found, needle, kind):
    assert at(found, PROBE, needle, kind)


@pytest.mark.parametrize('needle', ['template <class T> auto Tpl', 'Text(char const* text)', 'User(void* user)',
                                    'Record(Loose* record)', 'Use(Dev* dev)'])
def test_a_module_holding_no_abi_has_no_adapters(found, needle):
    assert at(found, FACADES, needle, 'parameters')


@pytest.mark.parametrize('needle', ['Generic(T* generic)', 'Project(Loose* project)', 'Stray(Dev* stray)',
                                    'Inner(Dev* inner)'])
def test_an_adapter_needs_a_c_pointee_and_only_abi_callers(found, needle):
    assert at(found, ADAPTER, needle, 'parameters')


@pytest.mark.parametrize('needle', ['Checked(Dev* adapted', 'Rooted(Dev* rooted)'])
def test_an_adapter_reached_from_an_export_passes(found, needle):
    assert not at(found, ADAPTER, needle, 'parameters')


def test_an_export_mark_is_not_the_exports_name(found):
    assert not at(found, ADAPTER, 'Dev* marked', 'parameters')


def test_an_attribute_string_does_not_end_the_declaration_head(tmp_path):
    (tmp_path / 'a.cpp').write_text('[[deprecated("a;b{")]] auto _Public_(7)\n    exported(int x) -> int;\n')
    assert pointers.declared_name([str(tmp_path / 'a.cpp'), 1, 1]) == 'exported'


def test_a_project_extern_c_declaration_is_no_c_side(found):
    assert at(found, PROBE, 'Washed(int* washed)', 'parameters')


def test_a_registration_is_exactly_one_callback_and_one_user_pointer(found):
    assert not at(found, PROBE, 'class Registration', 'members')
    assert at(found, PROBE, 'class Smuggling', 'members')


def test_a_deleter_exempts_only_its_call_operator(found):
    assert not at(found, PROBE, 'int* freed', 'parameters')
    assert at(found, PROBE, 'int* other', 'parameters')


@pytest.mark.parametrize('needle', ['Cb(void* registered)', 'void* lambda_slot', 'char** argv'])
def test_slots_and_main_are_the_abis_signatures(found, needle):
    assert not at(found, PROBE, needle, 'parameters')


def test_the_reflect_entry_is_the_signature_the_reflect_vocabulary_writes(found):
    assert not at(found, PROBE, 'reflect_tag', 'parameters')
    assert not at(found, PROBE, 'enum_tag', 'parameters')


@pytest.mark.parametrize('needle', ['loose_tag', 'paired_tag', 'used_tag', 'runtime_tag', 'method_tag', 'other_tag'])
def test_a_reflect_name_off_the_protocols_shape_is_a_pointer(found, needle):
    assert at(found, PROBE, needle, 'parameters')


def test_a_slot_pointer_is_tested_in_the_slot_or_in_the_function_it_goes_to(found):
    assert not at(found, PROBE, 'void* forwarding', 'unchecked')
    assert at(found, PROBE, 'void* raw', 'unchecked')


def test_a_slot_is_its_definition_not_its_name(found):
    assert not at(found, PROBE, 'auto Init(Dev* slotted) -> int { if', 'parameters')


def test_an_adapter_that_does_not_test_fails(found):
    assert at(found, ADAPTER, 'Unchecked(Dev* loose)', 'parameters')


def test_an_export_returns_through_its_lambda_and_adopts_into_ownership(found):
    assert not at(found, ADAPTER, 'auto text = [](Dev& dev)', 'returns')
    assert not at(found, ADAPTER, 'sdl_close(Dev* closed)', 'unchecked')


def test_a_tracked_unit_outside_the_database_fails(tmp_path):
    files = {PROBE: PROBES, 'sources/sdl-rdp/video/missing.cpp': 'int x;\n', 'sources/sdl-rdp/link/wire.win32.cpp': ''}
    build = tree(tmp_path, files, [PROBE])
    with pytest.raises(SystemExit, match='missing.cpp'):
        pointers.findings(tmp_path, build)


def test_a_query_clang_query_rejects_fails(tmp_path, monkeypatch):
    monkeypatch.setattr(pointers, 'QUERIES', pointers.QUERIES + 'match functionDecl()\n  .bind("dropped")\n')
    build = tree(tmp_path, {PROBE: PROBES}, [PROBE])
    with pytest.raises(SystemExit, match='unknown command'):
        pointers.findings(tmp_path, build)


def test_the_database_comes_from_the_gate(monkeypatch):
    monkeypatch.delenv('BUILDUTIL_BUILD_DIR', raising=False)
    with pytest.raises(SystemExit, match='BUILDUTIL_BUILD_DIR'):
        pointers.build_directory()


def finding(path, kind, type_text):
    return pointers.Finding(pathlib.Path(path), 1, 1, kind, type_text, '')


def test_a_finished_module_is_held_by_finding_so_a_removal_admits_nothing():
    baseline = pointers.Baseline({}, collections.Counter([('sources/sdl-rdp/link/a.hpp', 'returns', 'void *')] * 2))
    found    = [finding('sources/sdl-rdp/link/a.hpp', 'returns', 'void *'),
                finding('sources/sdl-rdp/link/b.hpp', 'parameters', 'int *')]
    assert pointers.regressions(found, baseline) == [
        'sources/sdl-rdp/link/b.hpp: new parameters (int *)',
        'sources/sdl-rdp/link/a.hpp: returns (void *) is gone; drop it from the baseline']


def test_a_part_two_module_is_held_by_count():
    baseline = pointers.Baseline({('sample', 'parameters'): 1}, collections.Counter())
    found    = [finding('sources/sample/a.cpp', 'parameters', 'int *'),
                finding('sources/sample/b.cpp', 'parameters', 'char *')]
    assert pointers.regressions(found, baseline) == ['sample: parameters 2 != 1']


def test_the_baseline_reads_both_forms(tmp_path):
    path = tmp_path / 'baseline'
    path.write_text('count\tsdl-rdp/SDL3\tparameters\t3\nfinding\tsources/sdl-rdp/link/a.hpp\treturns\tvoid *\n')
    baseline = pointers.read_baseline(path)
    assert baseline.counts == {('sdl-rdp/SDL3', 'parameters'): 3}
    assert baseline.findings == collections.Counter([('sources/sdl-rdp/link/a.hpp', 'returns', 'void *')])


def test_table_counts_by_module_with_tests_apart():
    found = [finding('sources/sdl-rdp/video/a.cpp', 'members', 'int *'),
             finding('sources/sdl-rdp/video/a.test.cpp', 'members', 'int *')]
    assert [line.split()[:2] for line in pointers.table(found)[1:3]] == [['sdl-rdp/video', '0'],
                                                                      ['sdl-rdp/video', 'tests']]


def test_a_recorded_input_that_is_gone_leaves_no_cache_key(tmp_path):
    (tmp_path / 'unit.cpp').write_text('')
    (tmp_path / 'kept.hpp').write_text('')
    unit = {'directory': str(tmp_path), 'file': 'unit.cpp', 'arguments': ['clang++']}
    assert pointers.unit_key(unit, 'q', ['kept.hpp'])
    assert pointers.unit_key(unit, 'q', ['kept.hpp', 'moved.hpp']) is None


def test_a_lint_source_change_invalidates_every_key(tmp_path, monkeypatch):
    assert pathlib.Path(pointers.shape.__file__) in pointers.LINT_SOURCES
    sources = (tmp_path / 'pointers.py', tmp_path / 'shape.py')
    for source in sources:
        source.write_text('MARKS = ()\n')
    (tmp_path / 'unit.cpp').write_text('')
    monkeypatch.setattr(pointers, 'LINT_SOURCES', sources)
    monkeypatch.setattr(pointers, 'dependencies', lambda build: {})
    monkeypatch.setattr(pointers, 'preprocessed_inputs', lambda unit: [])
    units = [{'directory': str(tmp_path), 'file': 'unit.cpp', 'arguments': ['clang++']}]
    before = pointers.cached_keys(units, pointers.lint_text('q'), tmp_path)
    sources[1].write_text("MARKS = ('_Public_',)\n")
    assert pointers.cached_keys(units, pointers.lint_text('q'), tmp_path) != before


def bench_build(root, database_age, bench_age):
    """A build tree with benches off whose own database and bench database have the given ages in seconds."""
    build = root / 'build'
    (build / 'pointers' / 'benches').mkdir(parents=True)
    (build / 'CMakeCache.txt').write_text('CMAKE_COMMAND:INTERNAL=cmake\nCMAKE_GENERATOR:INTERNAL=Ninja\n'
                                          'BUILD_BENCHMARKING:BOOL=OFF\n')
    for path, age in ((build / 'compile_commands.json', database_age),
                      (build / 'pointers' / 'benches' / 'compile_commands.json', bench_age)):
        if age is not None:
            path.write_text('[]')
            os.utime(path, (path.stat().st_mtime - age, path.stat().st_mtime - age))
    os.utime(build / 'CMakeCache.txt', (0, 0))
    return build


@pytest.mark.parametrize('database_age, bench_age, reconfigured',
                         [(10, 100, True), (100, 10, False), (None, 10, False)])
def test_the_bench_database_follows_the_builds_own(tmp_path, monkeypatch, database_age, bench_age, reconfigured):
    commands = []
    monkeypatch.setattr(pointers.subprocess, 'run',
                        lambda command, **_: commands.append(command) or subprocess.CompletedProcess(command, 0))
    pointers.bench_database(tmp_path, bench_build(tmp_path, database_age, bench_age))
    assert bool(commands) == reconfigured
