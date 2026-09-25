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
int sdl_bytes(void*);
int sdl_forward(struct Dev*);
void c_property(char const* name, void* value);
void c_keep(void* value);
int c_relay(void* (*cb)(void*), void* user);
int c_read(void const* data);
struct Pair { int (*on)(struct Dev*, int*); };
void sdl_close(struct Dev*);
int sdl_chain(struct Dev*);
int sdl_lambda(struct Dev*);
int sdl_marked(struct Dev*);
typedef int (*Setter)(int (*)(struct Dev*));
Setter c_lookup(void);
struct Entry { int (*Open)(int (*cb)(struct Dev*)); };
int sdl_passes(int (*cb)(struct Dev*));
int sdl_calls(int (*cb)(struct Dev*));
int sdl_stores(int (*cb)(struct Dev*));
int sdl_assigns(int (*cb)(struct Dev*));
int sdl_fills(int (*cb)(struct Dev*));
}
'''
ABI_HEADER = '''extern "C" {
struct Dev;
int abi_open(struct Dev* dev, char const* name);
}
'''
LIB_HEADER = '''#include <functional>
namespace lib {
struct Bench {
  auto Apply(std::function<void(Bench*)> const& each) -> Bench*;
  auto Each(void (*each)(Bench*)) -> Bench*;
};
}
'''
PROBES = '''#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#include <tuple>
#include "c.h"
#include "lib.h"
#include <sdl-rdp/abi/abi.h>
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
template <class T> auto Carry(T carried) -> int { return *carried; }
auto Abbrev(auto abbreviated) -> int { return *abbreviated; }
template <class... ArgsTy> auto Pack(ArgsTy... packed) -> int { return (*packed + ...); }
template <class T> auto Relay(T relayed) -> void { c_free(relayed); }
auto Early(Dev* early) -> int;
auto Once(lib::Bench* once) -> void;
auto Each(lib::Bench* each) -> void;
auto FreeThing(int* thing) -> void { c_free(thing); }
template <auto... RELEASES> struct Releasing {
  template <class ValueTy> auto operator()(ValueTy* released) const -> void { (..., RELEASES(released)); }
};
using Thing = std::unique_ptr<int, Releasing<FreeThing>>;
auto Acquire(int size) -> int*;
template <class ValueTy, auto ACQUIRE, auto RELEASE> class Wrap {
public:
  template <class... ArgsTy> explicit Wrap(ArgsTy... args) : _wrapped{ ACQUIRE(args...) } { }
  Wrap(Wrap const&) = delete;
  ~Wrap() { RELEASE(_wrapped); }
private:
  ValueTy _wrapped;
};
auto Register(int (*callback)(void*)) -> int* { c_register(callback, nullptr); return nullptr; }
auto Keeps(int (*callback)(void*)) -> int*;
auto Hooked(void* hooked) -> int { if (!hooked) return 0; return 1; }
auto Kept(void* kept) -> int { if (!kept) return 0; return 1; }
auto Laundered(Dev* laundered) -> int { if (!laundered) return 0; return 1; }
template <class T> auto Quiet(T quiet) -> std::size_t { return std::strlen(quiet); }
template <class T> auto ByRefQuiet(T& by_reference) -> std::size_t { return std::strlen(by_reference); }
template <class T> auto Spanning(T spanned, std::size_t n) -> std::size_t { return std::span{ spanned, n }.size(); }
auto Landing(int* landing) -> int { return *landing; }
template <class T> auto Forward(T forwarded) -> int { return Landing(forwarded); }
auto Expects(bool condition, char const* text) -> void;
template <class ArgTy> auto Converted(ArgTy converted) -> decltype(auto) {
  Expects(converted != nullptr, "the slot's argument is supplied");
  return *converted;
}
template <class... ArgsTy> auto Passed(ArgsTy... passing) -> int { return (Converted(passing) + ...); }
template <class ContextTy, class... ArgsTy> auto Trampoline(ContextTy* context, ArgsTy... args) -> int {
  if (!context) return 0;
  return [&] { return Passed(args...); }();
}
template <class ArgTy> auto Stranded(ArgTy stranded) -> decltype(auto) {
  Expects(stranded != nullptr, "the argument is supplied");
  return *stranded;
}
auto Direct(int& value) -> int { return Stranded(&value); }
auto Lambdas() -> int {
  auto const first  = [](int* first_lambda) { return *first_lambda; };
  auto const second = [](int* second_lambda) { return *second_lambda; };
  return first(nullptr) + second(nullptr);
}
template <class ValueTy,
          class OtherTy>
class Headed {
  ValueTy* headed_member;
};
auto Stored(Dev* stored) -> int { if (!stored) return 0; return 1; }
template <class... ArgsTy> auto PackQuiet(ArgsTy... pack_quiet) -> std::size_t { return std::strlen(pack_quiet...[0]); }
template <class... ArgsTy> auto PackLanding(ArgsTy... pack_landing) -> int { return Landing(pack_landing...[0]); }
template <class T> auto Made() -> T { return T{ }; }
struct Pmf { auto F(int x) -> int { return x; } };
struct HasPmf {
  int (Pmf::*pointer_method)(int*);
  int (Pmf::*plain_method)(int);
  int Pmf::*data_offset;
};
auto TakesFunction(std::function<int(int*)> const& taken_function) -> int;
auto MakesFunction() -> std::function<void(int*)>;
struct Owner { Wrap<int*, Acquire, c_free> owned_handle; std::optional<Wrap<int*, Acquire, c_free>> maybe_handle; };
auto Instantiate(int* value, Table& t, lib::Bench& bench) -> int {
  Relay(value);
  t.Init = Early;
  bench.Apply(Once);
  bench.Each(Each);
  Thing const thing{ nullptr };
  Wrap<int*, Register, c_free> const hook{ Hooked };
  Wrap<int*, Keeps, c_free> const kept{ Kept };
  c_property("stored", reinterpret_cast<void*>(Stored));
  c_keep(reinterpret_cast<void*>(Laundered));
  c_relay([](void* handed) -> void* { return handed; }, nullptr);
  c_register([](void* copied) -> int { return c_read(copied); }, nullptr);
  Pair pair{ Trampoline<Dev, int*> };
  std::array<char, 4> text{ };
  char*               letters = text.data();
  return Carry(value) + Forward(value) + Direct(*value) + static_cast<int>(Quiet(text.data()) + ByRefQuiet(letters)
         + Spanning(value, 1) + PackQuiet(text.data())) + Lambdas() + pair.on(nullptr, nullptr) + Abbrev(value)
         + Pack(value, value) + PackLanding(value) + static_cast<int>(Made<int*>() == nullptr);
}
auto Early(Dev* early) -> int { if (!early) return 1; return 0; }
auto Once(lib::Bench* once) -> void { if (!once) return; }
auto Each(lib::Bench* each) -> void { if (!each) return; }
template <class T> using O = std::optional<T>;
class Calls {
public:
  template <class... ArgsTy> auto Call(ArgsTy&&... call_args) const -> int {
    return std::get<0>(_symbols)(std::forward<ArgsTy>(call_args)...);
  }
  template <class... ArgsTy> auto Other(ArgsTy&&... other_args) const -> int {
    return _other(std::forward<ArgsTy>(other_args)...);
  }
private:
  std::tuple<decltype(&abi_open)> _symbols;
  std::tuple<int (*)(int*)>       _other_table;
  int (*_other)(int*);
};
template <auto ACQUIRE> struct Checking {
  template <class... ArgsTy> auto operator()(ArgsTy&&... acquire_args) const -> int* {
    return ACQUIRE(std::forward<ArgsTy>(acquire_args)...);
  }
};
auto Borrow(int* from) -> int*;
auto UseCalls(Calls const& calls, Dev& dev, int& value) -> int {
  Wrap<int*, Checking<Borrow>{ }, c_free> const checked{ &value };
  return calls.Call(&dev, "x") + calls.Other(&value);
}
struct Deep {
  std::optional<std::vector<std::pair<int, std::span<int*>>>> deeper_member;
  std::optional<std::optional<std::optional<std::optional<std::optional<std::vector<int*>>>>>> deepest_member;
  O<O<O<O<O<O<O<O<O<O<O<std::vector<int*>>>>>>>>>>>> twelve_deep_member;
  std::function<void(int*)>   function_member;
  std::optional<int*>         optional_member;
  std::vector<int*>           vector_member;
  std::pair<int*, int>        pair_member;
  int*                        array_member[4];
  std::optional<std::vector<P>> nested_member;
};
}
auto main(int argc, char** argv) -> int { return argc; }
'''
ADAPTERS = '''#include <concepts>
#include <memory>
#include <optional>
#include <string>
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
template <class T> concept Opaque = std::same_as<T, void> || std::same_as<T, void const>;
template <Opaque ByteTy> auto Bytes(ByteTy* bytes) -> int { if (!bytes) return -1; return 0; }
template <typename ByteTy> auto Loose(ByteTy* loosely) -> int { if (!loosely) return -1; return 0; }
auto Project(Loose* project) -> int { if (!project) return -1; return 0; }
extern "C" auto sdl_export(Dev* exported) -> char const* {
  auto text = [](Dev& dev) -> char const* { return "x"; };
  if (!exported) return nullptr;
  Generic(exported);
  Project(nullptr);
  return Checked(exported, "y") ? nullptr : text(*exported);
}
auto Checks(Dev* checks) -> int;
extern "C" auto sdl_forward(Dev* forwarded) -> int { return Checks(forwarded); }
auto Checks(Dev* checks) -> int { if (!checks) return -1; return 0; }
auto Named(char const* named_text) -> std::optional<std::string> {
  if (!named_text) return std::nullopt;
  return std::string{ named_text };
}
auto Reader() -> bool { return Named("x").has_value(); }
extern "C" auto sdl_bytes(void* data) -> int { return Bytes(data) + Loose(data); }
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
FACADE = '''#include <optional>
#include <string>
#include "c.h"
struct Loose { int value; };
template <class T> auto Tpl(T* tpl) -> int { if (!tpl) return 0; return 1; }
auto Text(char const* text) -> int { if (!text) return 0; return 1; }
auto User(void* user) -> int { if (!user) return 0; return 1; }
auto Optional(char const* optional_text) -> std::optional<std::string> {
  if (!optional_text) return std::nullopt;
  return std::string{ optional_text };
}
auto Record(Loose* record) -> int { if (!record) return 0; return 1; }
extern "C" auto sdl_export(Dev* exported) -> char const*;
auto Use(Dev* dev) -> char const* { return sdl_export(dev); }
'''
GETTERS = '''#include <sdl-rdp/abi/backend.h>
#include <tuple>
#include <vector>
namespace probe {
struct Getters {
  char const* (*name)();
  unsigned (*count)();
  std::vector<char const* (*)()> names;
  void (*closer)(sdlrdp_handle*);
  int* control;
};
auto TakeName(char const* (*taken_name)()) -> int;
struct Catalog {
  using Symbols = std::tuple<decltype(&sdlrdp_last_error), decltype(&sdlrdp_version), decltype(&sdlrdp_close)>;
};
using CatalogSymbols = Catalog::Symbols;
struct Table {
  CatalogSymbols const catalog_symbols;
};
auto Loaded() -> CatalogSymbols;
class Session {
public:
  template <class... ArgsTy> auto Call(ArgsTy... session_args) const -> decltype(auto) {
    return std::get<0>(_symbols)(session_args...);
  }
  auto Error() const -> decltype(auto) { return Call(); }
  auto Other() const -> decltype(auto) { return _other(); }
private:
  CatalogSymbols _symbols;
  char const* (*_other)();
};
auto UseSession(Session const& session) -> bool { return session.Error() == session.Other(); }
struct Symbols {
  CatalogSymbols symbols;
};
auto SpelledError(Symbols const& table) -> char const* { return std::get<0>(table.symbols)(); }
auto SpelledRelay(Symbols const& table) -> char const* { return SpelledError(table); }
}
'''
SHAPES = '''#include <cstddef>
#include <new>
#include "c.h"
extern "C" auto transcribed_alloc(std::size_t size) noexcept -> void*;
extern "C" auto transcribed_free(void* block) noexcept -> void;
extern "C" auto project_defined(int* defined_param) -> int*;
namespace shapes {
struct Restores {
  Table& table;
  int (*restored)(Dev*);
  ~Restores() { table.Init = restored; }
};
struct Reads {
  explicit Reads(Table& t) : initialized(t.Init) { assigned = t.Init; }
  int (*initialized)(Dev*);
  int (*assigned)(Dev*) = nullptr;
};
struct Untouched { int (*untouched)(Dev*); };
struct Extended {
  Table prefix_table;
  int   extra;
};
auto Of(Table& table) -> Extended& { return *reinterpret_cast<Extended*>(&table); }
struct Composed { Table composed_table; };
struct Trailing {
  int   extra;
  Table trailing_table;
};
auto Of(Table& table) -> Trailing& { return *reinterpret_cast<Trailing*>(&table); }
struct Routed { inline static thread_local Routed* routed_static = nullptr; };
Dev* namespace_global = nullptr;
char const* const constant_text = "text";
#define WRITES_STATIC(name) Dev* name##_written = nullptr
WRITES_STATIC(macro);
template <class T> struct Lease {
  T& held;
  auto operator->() const -> T* { return &held; }
  auto Get() const -> T* { return &held; }
};
auto Hand(Dev* handed_through) -> int { return handed_through == nullptr; }
auto Direct(Dev* handed_directly) -> int { return handed_directly == nullptr; }
auto ThroughReference() -> int {
  static decltype(c_register)& registered = c_register;
  return registered([](void* by_reference_lambda) -> int { return by_reference_lambda == nullptr; }, nullptr);
}
auto Taking(int (*taken)(Dev*)) -> int { return taken(nullptr); }
auto Local(Dev* handed_locally) -> int { return handed_locally == nullptr; }
auto ThroughProject() -> int {
  auto* const project = &Taking;
  return project(Local);
}
auto Indirect() -> void {
  auto* const next = c_lookup();
  next(Hand);
}
auto ViaField(Entry& entry) -> int {
  return entry.Open([](Dev* field_lambda) -> int { return field_lambda == nullptr; });
}
struct SavedOpen {
  explicit SavedOpen(Entry& e) : open(e.Open) { }
  auto Call() const -> int { return open([](Dev* saved_lambda) -> int { return saved_lambda == nullptr; }); }
  int (*open)(int (*)(Dev*));
};
}
extern "C" auto sdl_passes(int (*passed_cb)(Dev*)) -> int { return c_lookup()(passed_cb); }
extern "C" auto sdl_calls(int (*called_cb)(Dev*)) -> int { return called_cb(nullptr); }
struct Stored {
  int (*callback)(Dev*);
  void* user;
};
extern "C" auto sdl_stores(int (*stored_cb)(Dev*)) -> int {
  Stored const record{ stored_cb, nullptr };
  return record.callback(nullptr);
}
extern "C" auto sdl_assigns(int (*assigned_cb)(Dev*)) -> int {
  Stored record{ };
  record.callback = assigned_cb;
  return record.callback(nullptr);
}
extern "C" auto sdl_fills(int (*filled_cb)(Dev*)) -> int {
  Table table{ };
  table.Init = filled_cb;
  return c_register(nullptr, &table);
}
auto operator new(std::size_t size) -> void* { return transcribed_alloc(size); }
auto operator delete(void* released_block) noexcept -> void { transcribed_free(released_block); }
'''
DEFINED = 'extern "C" auto project_defined(int* defined_param) -> int* { return defined_param; }\n'
PROBE   = 'sources/sdl-rdp/video/probe.cpp'
ADAPTER = 'sources/sdl-rdp/backend/adapter.cpp'
FACADES = 'sources/sdl-rdp/freerdp-facade/facade.cpp'
GETTER  = 'sources/sdl-rdp/video/getters.cpp'
FILES   = {PROBE: PROBES, ADAPTER: ADAPTERS, FACADES: FACADE, GETTER: GETTERS}


def locate(text, needle):
    return next(number for number, line in enumerate(text.splitlines(), 1) if needle in line)


def tree(root, files, compiled):
    """A git tree holding the files, with a compile database of the compiled ones."""
    (root / 'include').mkdir()
    (root / 'include/c.h').write_text(C_HEADER)
    (root / 'include/lib.h').write_text(LIB_HEADER)
    (root / 'sources/sdl-rdp/abi').mkdir(parents=True)
    (root / 'sources/sdl-rdp/abi/abi.h').write_text(ABI_HEADER)
    (root / 'sources/sdl-rdp/abi/backend.h').write_text((pointers.ROOT / 'sources/sdl-rdp/abi/backend.h').read_text())
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text)
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
    build = root / '_build/probe'
    build.mkdir(parents=True)
    database = [{'directory': str(root), 'file': str(root / name),
                 'arguments': ['clang++', '-std=c++26', f'-I{root}/include', f'-I{root}/sources', '-c',
                               str(root / name)]}
                for name in compiled]
    (build / 'compile_commands.json').write_text(json.dumps(database))
    return build


@pytest.fixture(scope='module')
def listed(tmp_path_factory):
    root  = tmp_path_factory.mktemp('tree')
    build = tree(root, FILES, FILES)
    return pointers.findings(root, build)


@pytest.fixture(scope='module')
def found(listed):
    return {(str(item.path), item.line, item.kind) for item in listed}


@pytest.fixture(scope='module')
def keyed(listed):
    return {(str(item.path), item.line, item.owner): item for item in listed}


def keyed_at(keyed, needle):
    return next(owner for path, line, owner in keyed if path == PROBE and line == locate(PROBES, needle))


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
                                    'Optional(char const* optional_text)',
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


@pytest.mark.parametrize('needle', ['T carried', 'auto abbreviated', 'ArgsTy... packed'])
def test_a_pointer_through_a_template_parameter_is_seen(found, needle):
    assert at(found, PROBE, needle, 'parameters')


@pytest.mark.parametrize('needle', ['T relayed', 'T quiet', 'T& by_reference', 'T spanned'])
def test_a_template_pointer_reaching_foreign_code_is_judged_whether_or_not_it_is_dereferenced(found, needle):
    assert at(found, PROBE, needle, 'parameters')


def test_a_template_parameter_handed_to_a_project_function_is_judged_where_it_lands(found):
    assert not at(found, PROBE, 'T forwarded', 'parameters')
    assert at(found, PROBE, 'Landing(int* landing)', 'parameters')


def test_a_pack_indexed_pointer_is_seen_through_its_index(found):
    assert at(found, PROBE, 'ArgsTy... pack_quiet', 'parameters')
    assert not at(found, PROBE, 'ArgsTy... pack_landing', 'parameters')


def test_a_template_return_is_judged_per_instantiation(found):
    assert at(found, PROBE, 'auto Made() -> T', 'returns')


@pytest.mark.parametrize('needle, seen', [('pointer_method', True), ('plain_method', False), ('data_offset', False)])
def test_a_member_function_pointer_with_a_pointer_parameter_is_seen(found, needle, seen):
    assert at(found, PROBE, needle, 'members') == seen


@pytest.mark.parametrize('needle, kind', [('taken_function', 'parameters'), ('MakesFunction()', 'returns')])
def test_a_function_type_in_a_signature_is_one_finding(listed, needle, kind):
    line = locate(PROBES, needle)
    assert [item.kind for item in listed if str(item.path) == PROBE and item.line == line] == [kind]


@pytest.mark.parametrize('needle, kind', [('(*name)()', 'members'), ('(*count)()', 'members'), ('names;', 'members'),
                                          ('(*closer)', 'members'), ('control;', 'members'),
                                          ('taken_name', 'parameters')])
def test_a_pointer_of_an_abi_functions_type_outside_its_table_is_a_finding(found, needle, kind):
    assert at(found, GETTER, needle, kind)


def test_the_abis_table_through_its_aliases_is_the_abi(found):
    assert not at(found, GETTER, 'catalog_symbols;', 'members')
    assert not at(found, GETTER, 'auto Loaded()', 'returns')
    assert not at(found, GETTER, 'CatalogSymbols _symbols;', 'members')


def test_a_return_of_a_call_through_the_abis_table_is_the_abi(found):
    assert not at(found, GETTER, 'auto Call(ArgsTy... session_args)', 'returns')
    assert not at(found, GETTER, 'auto Error()', 'returns')
    assert at(found, GETTER, 'auto Other()', 'returns')
    assert at(found, GETTER, '(*_other)();', 'members')


@pytest.mark.parametrize('needle', ['auto SpelledError(', 'auto SpelledRelay('])
def test_a_spelled_pointer_return_is_judged_whatever_it_relays(found, needle):
    assert at(found, GETTER, needle, 'returns')


def test_a_concept_constrained_void_pointee_is_a_c_adapter(found):
    assert not at(found, ADAPTER, 'ByteTy* bytes', 'parameters')
    assert at(found, ADAPTER, 'ByteTy* loosely', 'parameters')


@pytest.mark.parametrize('needle', ['deeper_member', 'deepest_member', 'twelve_deep_member'])
def test_a_pointer_at_any_depth_is_seen(found, needle):
    assert at(found, PROBE, needle, 'members')


def test_a_pointer_in_a_function_type_held_as_a_member_is_seen(found, needle='function_member'):
    assert at(found, PROBE, needle, 'members')


@pytest.mark.parametrize('needle', ['auto Early(Dev* early) -> int;', 'auto Early(Dev* early) -> int {',
                                    'auto Once(lib::Bench* once) -> void;', 'auto Once(lib::Bench* once) -> void {',
                                    'auto Each(lib::Bench* each) -> void;'])
def test_a_forward_declared_slot_is_one_slot_with_its_definition(found, needle):
    assert not at(found, PROBE, needle, 'parameters')


def test_a_function_a_deleter_releases_with_is_part_of_the_deleter(found):
    assert not at(found, PROBE, 'FreeThing(int* thing)', 'parameters')
    assert not at(found, PROBE, 'FreeThing(int* thing)', 'unchecked')
    assert not at(found, PROBE, 'ValueTy* released', 'parameters')


def test_an_raii_type_over_a_c_handle_is_not_a_pointer(found):
    assert not at(found, PROBE, 'owned_handle', 'members')
    assert not at(found, PROBE, 'ValueTy _wrapped', 'members')
    assert not at(found, PROBE, 'auto Acquire(int size)', 'returns')


def test_a_c_strings_conversion_to_an_optional_string_is_the_one_adapter_any_caller_may_use(found):
    assert not at(found, ADAPTER, 'Named(char const* named_text)', 'parameters')


def test_an_opaque_pointer_only_handed_back_is_never_read(found):
    assert not at(found, PROBE, 'void* handed', 'unchecked')


def test_an_opaque_pointer_handed_to_foreign_code_is_read(found):
    assert at(found, PROBE, 'void* copied', 'unchecked')


@pytest.mark.parametrize('needle', ['Hooked(void* hooked)', 'Stored(Dev* stored)'])
def test_a_callback_registered_through_an_raii_type_or_as_an_opaque_value_is_a_slot(found, needle):
    assert not at(found, PROBE, needle, 'parameters')


@pytest.mark.parametrize('needle', ['Kept(void* kept)', 'Laundered(Dev* laundered)'])
def test_a_callback_handed_to_a_receiver_that_registers_nothing_is_no_slot(found, needle):
    assert at(found, PROBE, needle, 'parameters')


def test_a_slots_pointer_checked_and_continued_as_a_reference_through_its_pass_throughs_is_the_slots(found):
    assert not at(found, PROBE, 'ArgTy converted', 'parameters')
    assert not at(found, PROBE, 'ArgTy converted', 'null checks')
    assert not at(found, PROBE, 'ArgsTy... passing', 'parameters')


def test_a_checked_conversion_reached_from_project_code_is_a_pointer(found):
    assert at(found, PROBE, 'ArgTy stranded', 'parameters')
    assert at(found, PROBE, 'ArgTy stranded', 'null checks')


def test_every_finding_names_its_owner_and_a_lambda_is_counted_in_its_function(keyed):
    owners = {owner for _, _, owner in keyed}
    assert '' not in owners
    assert not any(owner.startswith('?') or '::?' in owner for owner in owners)
    assert keyed_at(keyed, 'int* first_lambda') == 'Lambdas::lambda#1'
    assert keyed_at(keyed, 'int* second_lambda') == 'Lambdas::lambda#2'
    assert keyed_at(keyed, 'ValueTy* headed_member') == 'Headed'


def test_a_finding_moved_to_another_lambda_in_its_file_is_new(keyed):
    first, second = (next(item for item in keyed.values() if item.owner == f'Lambdas::lambda#{n}') for n in (1, 2))
    baseline = collections.Counter([first.key()])
    assert pointers.regressions([second], baseline) == [
        f'{second.path}: new parameters (int *) in Lambdas::lambda#2',
        f'{first.path}: parameters (int *) in Lambdas::lambda#1 is gone; drop it from the baseline']


def test_a_test_in_a_later_definition_guards_the_parameter_handed_to_its_declaration(found):
    assert not at(found, ADAPTER, 'sdl_forward(Dev* forwarded)', 'unchecked')


def test_the_abis_function_table_and_a_call_through_it_are_the_abi(found):
    assert not at(found, PROBE, '_symbols;', 'members')
    assert not at(found, PROBE, 'call_args', 'parameters')
    assert at(found, PROBE, '_other_table;', 'members')
    assert at(found, PROBE, 'other_args', 'parameters')


def test_an_acquiring_functor_an_raii_type_holds_is_part_of_it(found):
    assert not at(found, PROBE, 'acquire_args', 'parameters')


@pytest.fixture(scope='module')
def shaped(tmp_path_factory):
    root  = tmp_path_factory.mktemp('shapes')
    files = {'sources/sdl-rdp/video/shapes.cpp': SHAPES, 'sources/sdl-rdp/video/defined.cpp': DEFINED}
    build = tree(root, files, files)
    return {(str(item.path), item.line, item.kind) for item in pointers.findings(root, build)}


def shape_at(shaped, needle, kind):
    return ('sources/sdl-rdp/video/shapes.cpp', locate(SHAPES, needle), kind) in shaped


@pytest.mark.parametrize('needle', ['int (*restored)(Dev*)', 'int (*initialized)(Dev*)', 'int (*assigned)(Dev*)'])
def test_a_function_pointer_member_a_c_table_slot_fills_or_is_restored_from_is_its_saved_original(shaped, needle):
    assert not shape_at(shaped, needle, 'members')


def test_a_function_pointer_member_that_touches_no_c_table_stays_a_pointer(shaped):
    assert shape_at(shaped, 'int (*untouched)(Dev*)', 'members')


def test_a_c_record_a_project_record_extends_by_prefix_is_the_layout_the_abi_allocates(shaped):
    assert not shape_at(shaped, 'Table prefix_table', 'members')
    assert shape_at(shaped, 'Table composed_table', 'members')
    assert shape_at(shaped, 'Table trailing_table', 'members')


@pytest.mark.parametrize('needle', ['Routed* routed_static', 'Dev* namespace_global', 'WRITES_STATIC(macro)'])
def test_a_mutable_pointer_with_static_storage_is_a_member(shaped, needle):
    assert shape_at(shaped, needle, 'members')


def test_a_constant_pointer_is_no_state(shaped):
    assert not shape_at(shaped, 'char const* const constant_text', 'members')


def test_a_boundary_function_pointer_is_read_only_when_called(shaped):
    assert not shape_at(shaped, 'int (*passed_cb)(Dev*)', 'unchecked')
    assert shape_at(shaped, 'int (*called_cb)(Dev*)', 'unchecked')


@pytest.mark.parametrize('needle', ['int (*stored_cb)(Dev*)', 'int (*assigned_cb)(Dev*)'])
def test_a_boundary_function_pointer_stored_in_a_project_record_is_read(shaped, needle):
    assert shape_at(shaped, needle, 'unchecked')


def test_a_boundary_function_pointer_filled_into_a_c_record_is_handed_on(shaped):
    assert not shape_at(shaped, 'int (*filled_cb)(Dev*)', 'unchecked')


def test_operator_arrow_is_the_signature_the_language_writes(shaped):
    assert not shape_at(shaped, 'auto operator->()', 'returns')
    assert shape_at(shaped, 'auto Get() const -> T*', 'returns')


@pytest.mark.parametrize('needle, kind', [('auto operator new', 'returns'), ('void* released_block', 'parameters'),
                                          ('void* released_block', 'unchecked')])
def test_a_replaced_allocation_function_is_the_signature_the_standard_writes(shaped, needle, kind):
    assert not shape_at(shaped, needle, kind)


def test_a_foreign_declaration_transcribed_is_its_librarys_signature(shaped):
    assert not shape_at(shaped, 'transcribed_alloc(std::size_t size)', 'returns')
    assert not shape_at(shaped, 'transcribed_free(void* block)', 'parameters')


def test_an_extern_c_declaration_project_code_defines_is_no_foreign_signature(shaped):
    assert shape_at(shaped, 'project_defined(int* defined_param) -> int*;', 'parameters')


@pytest.mark.parametrize('needle', ['Dev* field_lambda', 'Dev* saved_lambda'])
def test_a_lambda_handed_through_a_c_table_field_or_its_saved_original_is_a_slot(shaped, needle):
    assert not shape_at(shaped, needle, 'parameters')
    assert not shape_at(shaped, 'int (*open)(int (*)(Dev*))', 'members')


def test_a_function_handed_through_an_indirect_c_call_is_a_slot(shaped):
    assert not shape_at(shaped, 'Hand(Dev* handed_through)', 'parameters')
    assert shape_at(shaped, 'Direct(Dev* handed_directly)', 'parameters')
    assert shape_at(shaped, 'Local(Dev* handed_locally)', 'parameters')
    assert not shape_at(shaped, 'void* by_reference_lambda', 'parameters')


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


def finding(path, kind, type_text, owner=''):
    return pointers.Finding(pathlib.Path(path), 1, 1, kind, type_text, owner)


def test_a_finished_module_is_held_by_finding_so_a_removal_admits_nothing():
    held     = ('sources/sdl-rdp/link/a.hpp', 'returns', 'A', 'void *')
    baseline = collections.Counter([held] * 2)
    found    = [finding('sources/sdl-rdp/link/a.hpp', 'returns', 'void *', 'A'),
                finding('sources/sdl-rdp/link/b.hpp', 'parameters', 'int *', 'B')]
    assert pointers.regressions(found, baseline) == [
        'sources/sdl-rdp/link/b.hpp: new parameters (int *) in B',
        'sources/sdl-rdp/link/a.hpp: returns (void *) in A is gone; drop it from the baseline']


def test_a_finding_cannot_move_to_another_function_in_its_file():
    held     = ('sources/sdl-rdp/link/a.cpp', 'parameters', 'Old', 'void *')
    baseline = collections.Counter([held])
    found    = [finding('sources/sdl-rdp/link/a.cpp', 'parameters', 'void *', 'New')]
    assert pointers.regressions(found, baseline) == [
        'sources/sdl-rdp/link/a.cpp: new parameters (void *) in New',
        'sources/sdl-rdp/link/a.cpp: parameters (void *) in Old is gone; drop it from the baseline']


@pytest.mark.parametrize('line', ['count\tsdl-rdp/integration tests\tparameters\t3',
                                  'count\tsdl-rdp/video\tparameters\t0',
                                  'finding\tsources/sdl-rdp/link/a.hpp\treturns\tvoid *'])
def test_the_baseline_refuses_any_line_but_a_finding(tmp_path, line):
    path = tmp_path / 'baseline'
    path.write_text(f'{line}\n')
    with pytest.raises(SystemExit, match='not a finding line'):
        pointers.read_baseline(path)


def test_the_baseline_reads_findings_by_owner(tmp_path):
    path = tmp_path / 'baseline'
    path.write_text('finding\tsources/sdl-rdp/link/a.hpp\treturns\tA::B\tvoid *\n')
    assert pointers.read_baseline(path) == collections.Counter([('sources/sdl-rdp/link/a.hpp', 'returns', 'A::B',
                                                                 'void *')])


def test_a_test_module_is_held_by_finding_like_any_other():
    found = [finding('sources/sdl-rdp/integration/a.cpp', 'parameters', 'int *', 'A')]
    assert pointers.regressions(found, collections.Counter()) == [
        'sources/sdl-rdp/integration/a.cpp: new parameters (int *) in A']


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


def test_a_cached_match_is_keyed_on_the_queries_and_the_tool_not_the_judgement(tmp_path, monkeypatch):
    (tmp_path / 'unit.cpp').write_text('')
    monkeypatch.setattr(pointers, 'dependencies', lambda build: {})
    monkeypatch.setattr(pointers, 'preprocessed_inputs', lambda unit: [])
    units = [{'directory': str(tmp_path), 'file': 'unit.cpp', 'arguments': ['clang++']}]
    tool  = pointers.clang_query()
    keys  = pointers.cached_keys(units, pointers.lint_text('q', tool), tmp_path)
    assert pointers.cached_keys(units, pointers.lint_text('q', tool), tmp_path) == keys
    assert pointers.cached_keys(units, pointers.lint_text('r', tool), tmp_path) != keys
    assert pathlib.Path(pointers.__file__).read_text() not in pointers.lint_text('q', tool)


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
