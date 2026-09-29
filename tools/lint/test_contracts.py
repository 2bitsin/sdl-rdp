"""The contract lint: a condition calls, writes and expands only what the lint can show leaves every state alone."""
import subprocess

import pytest

import contracts


@pytest.fixture
def tree(tmp_path):
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    (tmp_path / 'sources/sdl-rdp/video').mkdir(parents=True)
    return tmp_path


def findings(tree, files):
    for name, text in files.items():
        (tree / 'sources/sdl-rdp/video' / name).write_text(text)
    subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
    return [f'{finding.path.name}:{finding.line} {finding.subject}' for finding in contracts.findings(tree)]


def condition(text, header=''):
    """A file whose one function holds the given contract lines, after the given declarations."""
    return f'{header}auto F(Thing& t, int n, Lock const& l) -> void {{\n{text}}}\n'


def test_a_c_call_in_a_condition_fails(tree):
    text = condition('  Expects(freerdp_settings_set_bool(s, K, true), "set");\n')
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:2 freerdp_settings_set_bool']


def test_a_contract_at_a_files_first_token_is_judged(tree):
    assert findings(tree, {'check.hpp': 'Expects(Mutate(), "changed");\n'}) == ['check.hpp:1 Mutate']


def test_a_hoisted_call_passes(tree):
    text = condition('  auto const set = freerdp_settings_set_bool(s, K, true);\n  Expects(set, "set");\n')
    assert findings(tree, {'a.cpp': text}) == []


def test_ensures_is_judged(tree):
    assert findings(tree, {'a.cpp': condition('  Ensures(Flush(), "flushed");\n')}) == ['a.cpp:2 Flush']


def test_standard_queries_pass(tree):
    text = condition('  Expects(!v.empty() && v.size() > 2, "v");\n  Expects(o.has_value(), "o");\n'
                     '  Expects(std::ranges::any_of(v, [](int x) { return bool(x); }), "any");\n'
                     '  Expects(std::cmp_less(v.size(), 4), "cmp");\n  Expects(sizeof(int) == 4, "size");\n'
                     '  Expects(a <= b && a >= b && a != b && a == b, "compare");\n'
                     '  Expects(std::array<int, 2>{ } == x, "angle");\n')
    assert findings(tree, {'a.cpp': text}) == []


def test_a_call_inside_a_lambda_is_judged(tree):
    text = condition('  Expects(std::ranges::all_of(v, [](int x) { return Poke(x); }), "p");\n')
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:2 Poke']


def test_const_members_pass_and_others_fail(tree):
    header = 'class Store {\npublic:\n  auto Holds(Lock const& held) const -> bool;\n  auto Send(int x) -> bool;\n};\n'
    source = ('#include <sdl-rdp/video/store.hpp>\nauto F(Store& s, Lock const& held) -> void {\n'
              '  Expects(s.Holds(held), "held");\n  Expects(s.Send(1), "sent");\n}\n')
    assert findings(tree, {'store.hpp': header, 'a.cpp': source}) == ['a.cpp:4 Send']


def test_an_arrow_member_call_is_judged(tree):
    header = 'class Store {\npublic:\n  auto Holds() const -> bool;\n  auto Drop() -> bool;\n};\n'
    source = ('#include <sdl-rdp/video/store.hpp>\nauto F(Store* s) -> void {\n'
              '  Expects(s->Holds(), "held");\n  Expects(s->Drop(), "dropped");\n}\n')
    assert findings(tree, {'store.hpp': header, 'a.cpp': source}) == ['a.cpp:4 Drop']


def test_constexpr_is_not_purity_but_consteval_is(tree):
    header = ('constexpr auto Bump() -> bool {\n  return ++g > 0;\n}\n'
              'consteval auto Fixed() -> bool {\n  return true;\n}\n')
    text   = condition('  Expects(Bump(), "b");\n  Expects(Fixed(), "f");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:8 Bump']


def test_a_const_member_returning_void_fails(tree):
    header = 'class Log {\npublic:\n  auto Emit(int x) const -> void;\n};\n'
    text   = condition('  Expects((l.Emit(1), true), "e");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:6 Emit']


@pytest.mark.parametrize('parameter', ['Buffer& out', 'std::span<std::byte> const& bytes', 'std::span<std::byte> bytes',
                                       'std::unique_ptr<Thing> const& owned', 'WaitHandle handle', 'Bio const& bio',
                                       'Thing* const thing', 'std::function<void()> const& call',
                                       'std::invocable auto step', 'std::reference_wrapper<Thing> thing',
                                       'std::optional<std::reference_wrapper<Thing>> thing', 'auto value',
                                       'auto const& value', 'const auto& value'])
def test_a_const_member_writing_through_a_parameter_fails(tree, parameter):
    header = ('using Bio = std::unique_ptr<BIO, Releases<BIO_free>>;\n'
              f'class Reader {{\npublic:\n  auto Fill({parameter}) const -> bool;\n}};\n')
    text   = condition('  Expects(r.Fill(x), "f");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:7 Fill']


def test_a_template_type_parameter_is_writable(tree):
    header = ('template <typename TTy>\nclass Box {\npublic:\n  auto Fill(TTy value) const -> bool;\n'
              '  template <typename UTy>\n  auto Pour(UTy value) const -> bool;\n'
              '  auto Size(std::integral auto count) const -> bool;\n};\n')
    text   = condition('  Expects(b.Fill(x) && b.Pour(x) && b.Size(1), "f");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:10 Fill', 'a.cpp:10 Pour']


@pytest.mark.parametrize('parameter', ['std::ranges::range auto items', 'std::copyable auto item', 'Viewable auto v'])
def test_a_constrained_auto_by_value_is_writable_unless_arithmetic(tree, parameter):
    header = (f'class Box {{\npublic:\n  auto Fill({parameter}) const -> bool;\n'
              '  auto Size(std::floating_point auto x, std::unsigned_integral auto n) const -> bool;\n};\n')
    text   = condition('  Expects(b.Fill(x) && b.Size(1.0, 1U), "f");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:7 Fill']


@pytest.mark.parametrize('parameter', ['Buffer const& in', 'std::span<std::byte const> bytes', 'Thing const* thing',
                                       'std::string_view text', 'int count = 0'])
def test_a_const_member_reading_its_parameter_passes(tree, parameter):
    header = f'class Reader {{\npublic:\n  auto Fill({parameter}) const -> bool;\n}};\n'
    assert findings(tree, {'a.cpp': condition('  Expects(r.Fill(x), "f");\n', header)}) == []


def test_a_non_const_accessor_with_a_const_twin_passes(tree):
    header = 'class Box {\npublic:\n  auto Value() -> Item&;\n  auto Value() const -> Item const&;\n};\n'
    source = '#include <sdl-rdp/video/box.hpp>\nauto F(Box& b) -> void {\n  Expects(b.Value().ok, "v");\n}\n'
    assert findings(tree, {'box.hpp': header, 'a.cpp': source}) == []


def test_a_non_const_twin_must_be_the_reference_half_of_an_accessor_pair(tree):
    header = 'class Queue {\npublic:\n  auto Next() const -> int;\n  auto Next() -> int;\n};\n'
    text   = condition('  Expects(q.Next() > 0, "n");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:7 Next']


@pytest.mark.parametrize('handed', ['Item&', 'Item*', 'Item* const', 'std::optional<std::reference_wrapper<Item>>'])
def test_a_const_member_handing_out_write_access_fails(tree, handed):
    header = f'class Box {{\npublic:\n  auto Value() const -> {handed};\n}};\n'
    assert findings(tree, {'a.cpp': condition('  Expects(b.Value() != nullptr, "v");\n', header)}) == ['a.cpp:6 Value']


@pytest.mark.parametrize('handed', ['Item const&', 'Item const*', 'Item const* const', 'Item'])
def test_a_const_member_handing_out_a_read_passes(tree, handed):
    header = f'class Box {{\npublic:\n  auto Value() const -> {handed};\n}};\n'
    assert findings(tree, {'a.cpp': condition('  Expects(b.Value() != nullptr, "v");\n', header)}) == []


def test_a_const_member_of_a_class_with_a_mutable_member_fails(tree):
    header = ('class Cache {\npublic:\n  auto Hits() const -> int;\n\nprivate:\n  mutable int hits = 0;\n};\n'
              'class Plain {\npublic:\n  auto Size() const -> int;\n  auto Each() const -> bool {\n'
              '    return [n = 0]() mutable { return ++n; }() > 0;\n  }\n};\n')
    text   = condition('  Expects(c.Hits() > 0 && p.Size() > 0, "h");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:16 Hits']


def test_a_twin_in_another_class_does_not_count(tree):
    header = ('class A {\npublic:\n  auto Clear() const -> bool;\n};\n'
              'class B {\npublic:\n  auto Clear() -> bool;\n};\n')
    source = '#include <sdl-rdp/video/ab.hpp>\nauto F(B& b) -> void {\n  Expects(b.Clear(), "c");\n}\n'
    assert findings(tree, {'ab.hpp': header, 'a.cpp': source}) == ['a.cpp:3 Clear']


def test_only_declarations_the_file_sees_count(tree):
    pure   = 'class Store {\npublic:\n  auto Holds(Lock const& held) const -> bool;\n};\n'
    other  = 'class Bench {\npublic:\n  auto Holds(Lock const& held) -> bool;\n};\n'
    source = condition('  Expects(s.Holds(l), "h");\n', '#include <sdl-rdp/video/store.hpp>\n')
    assert findings(tree, {'store.hpp': pure, 'bench.hpp': other, 'a.cpp': source}) == []


def test_the_source_twin_and_transitive_includes_are_seen(tree):
    inner  = 'class Store {\npublic:\n  auto Holds() const -> bool;\n};\n'
    outer  = '#include <sdl-rdp/video/inner.hpp>\n'
    twin   = '#include <sdl-rdp/video/outer.hpp>\nclass Own {\npublic:\n  auto Ready() const -> bool;\n};\n'
    source = condition('  Expects(s.Holds() && o.Ready(), "h");\n')
    files  = {'inner.hpp': inner, 'outer.hpp': outer, 'a.hpp': twin, 'a.cpp': source}
    assert findings(tree, files) == []


def test_a_member_call_reaches_members_of_its_arity_only(tree):
    header = ('class File {\npublic:\n  auto Channel() const -> Link const*;\n  auto Channel(int a) -> Link*;\n};\n'
              'auto Channel(Handle& handle) -> Link*;\n')
    text   = condition('  Expects(f.Channel() != nullptr, "member");\n  Expects(Channel(h) != nullptr, "free");\n',
                       header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:9 Channel']


def test_defaults_and_packs_widen_the_arity(tree):
    header = ('class Store {\npublic:\n  auto Near(int a, int b = 0) const -> bool;\n'
              '  auto All(std::integral auto... values) const -> bool;\n};\n')
    text   = condition('  Expects(s.Near(1) && s.Near(1, 2) && s.All() && s.All(1, 2, 3), "n");\n', header)
    assert findings(tree, {'a.cpp': text}) == []


def test_a_project_declaration_overrides_a_standard_name(tree):
    header = 'class Queue {\npublic:\n  auto empty() -> bool;\n};\n'
    text   = condition('  Expects(q.empty(), "e");\n', header)
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:6 empty']


def test_an_unknown_name_fails(tree):
    text = condition('  Expects(t.Flush(), "flushed");\n  Expects(Helper(), "helped");\n')
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:2 Flush', 'a.cpp:3 Helper']


def test_a_std_alias_reads_as_std(tree):
    text = ('namespace fs = std::filesystem;\nusing Clock = std::chrono::steady_clock;\nauto F(fs::path p) -> void {\n'
            '  Expects(fs::exists(p), "p");\n  Expects(Clock::duration::zero() < Clock::duration{ 1 }, "z");\n'
            '  Expects(other::exists(p), "q");\n}\n')
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:6 exists']


def test_standard_overloads_that_write_fail(tree):
    text = condition('  Expects(std::filesystem::exists(p, error), "e");\n  Expects(future.get() > 0, "g");\n')
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:2 exists', 'a.cpp:3 get']


@pytest.mark.parametrize('written', ['n++', '--n', '(n = 4)', '(n += 1)', '(n <<= 1)', 'new int{ 1 }',
                                     '[&] { return ++n; }()', 't.callbacks[0]()', '(f)()', 'g()()',
                                     'std::ranges::any_of(v, [&](int x) { return ++c > x; })'])
def test_side_effects_that_are_not_calls_fail(tree, written):
    assert findings(tree, {'a.cpp': condition(f'  Expects({written}, "w");\n')}) != []


def test_a_capture_default_is_not_an_assignment(tree):
    text = condition('  Expects(std::ranges::none_of(v, [=](int x) { return x == n; }), "c");\n')
    assert findings(tree, {'a.cpp': text}) == []


def test_a_project_macro_fails(tree):
    text = condition('  Expects(BUMP, "b");\n', '#define BUMP (++n > 0)\n')
    assert findings(tree, {'a.cpp': text}) == ['a.cpp:3 BUMP']


def test_an_operator_bool_that_is_not_const_noexcept_fails(tree):
    header = ('class Good {\npublic:\n  explicit operator bool() const noexcept;\n};\n'
              'class Bad {\npublic:\n  explicit operator bool() const;\n};\n')
    assert findings(tree, {'a.hpp': header}) == ['a.hpp:7 operator bool']


def test_the_text_and_declarations_are_not_conditions(tree):
    text = ('inline auto Expects(bool held, std::string_view text) -> void {\n  Check(held, text);\n}\n'
            'auto F(int x) -> void {\n  Expects(x > 0, Describe(x));\n}\n')
    assert findings(tree, {'a.cpp': text}) == []
