"""The cast lint: named casts only, functional and C-style casts to a scalar type fail everywhere."""
import subprocess

import pytest

import casts


@pytest.fixture
def tree(tmp_path):
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    (tmp_path / 'test_package').mkdir()
    (tmp_path / 'sources/sdl-rdp/video').mkdir(parents=True)
    return tmp_path


def findings(tree, text, name='sources/sdl-rdp/video/a.cpp'):
    (tree / name).write_text(text)
    subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
    return [f'{finding.path.name}:{finding.line} {finding.cast}' for finding in casts.findings(tree)]


def test_functional_casts_to_fundamental_types_fail(tree):
    text = 'auto a = int(x);\nauto b = double(y) / 2;\nf(bool(m & k), float(z));\nauto c = char(o);\n'
    assert findings(tree, text) == ['a.cpp:1 int', 'a.cpp:2 double', 'a.cpp:3 bool', 'a.cpp:3 float', 'a.cpp:4 char']


def test_functional_casts_to_std_scalars_fail(tree):
    text = 'auto a = std::uint32_t(n);\nauto b = std::size_t(m);\nauto c = std::byte(o);\n'
    assert findings(tree, text) == ['a.cpp:1 std::uint32_t', 'a.cpp:2 std::size_t', 'a.cpp:3 std::byte']


def test_c_style_casts_fail(tree):
    text = 'auto a = (int)x;\nauto b = (std::uint8_t)(y);\n#define NOTHING ((void)0)\nreturn (double)-z;\n'
    assert findings(tree, text) == ['a.cpp:1 int', 'a.cpp:2 std::uint8_t', 'a.cpp:3 void', 'a.cpp:4 double']


def test_named_and_braced_conversions_pass(tree):
    text = ('auto a = static_cast<int>(x);\nauto b = int{ y };\nauto c = Narrowed<int>(z);\n'
            'auto d = std::uint32_t{ n };\nauto e = (m & k) != 0;\n')
    assert findings(tree, text) == []


def test_function_types_and_conversion_operators_pass(tree):
    text = ('std::function<int()> a;\nstd::function<void(std::string_view)> b;\nexplicit operator bool() const;\n'
            'using Callback = bool(SDLCALL*)(void* user);\nauto c = sizeof(int);\nauto f(int) -> void;\n')
    assert findings(tree, text) == []


def test_comments_and_literals_pass(tree):
    assert findings(tree, '// int(x)\nauto a = "double(y)";\n/* (int)z */\n') == []


def test_tests_and_the_package_test_are_scanned(tree):
    assert findings(tree, 'auto a = int(x);\n', 'test_package/smoke.cpp') == ['smoke.cpp:1 int']


def test_template_parameter_types_fail(tree):
    text = ('template <auto FAILURE, class ResultTy> auto F() -> ResultTy {\n'
            'return Contained(ResultTy(FAILURE), body);\nauto b = ResultTy{ x };\n}\n')
    assert findings(tree, text) == ['a.cpp:2 ResultTy']


def test_template_parameters_are_read_from_the_head(tree):
    text = ('template <class Result, std::unsigned_integral Flags, auto VALUE, std::size_t N, auto... CALLS>\n'
            'auto F(Result idle) -> Result {\n(CALLS(idle), ...);\n'
            'return ready ? Result(action()) : Flags(VALUE(N));\n}\n')
    assert findings(tree, text) == ['a.cpp:4 Result', 'a.cpp:4 Flags']


def test_aliases_of_the_file_fail(tree):
    text = 'using Milliseconds = std::chrono::duration<double, std::milli>;\nauto a = Milliseconds(total);\n'
    assert findings(tree, text) == ['a.cpp:2 Milliseconds']


def test_construction_from_several_arguments_passes(tree):
    text = ('using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;\n'
            'auto a = Stream(open(), SDL_CloseIO);\n')
    assert findings(tree, text) == []


def test_const_qualified_c_casts_fail(tree):
    text = 'auto a = (const int)x;\nauto b = (int const)y;\n'
    assert findings(tree, text) == ['a.cpp:1 const int', 'a.cpp:2 int const']


def test_c_casts_after_a_condition_fail(tree):
    assert findings(tree, 'if (c) (void)f();\nwhile (d) (void)g();\n') == ['a.cpp:1 void', 'a.cpp:2 void']


def test_trait_casts_fail(tree):
    assert findings(tree, 'auto a = std::underlying_type_t<Mode>(mode);\n') == ['a.cpp:1 std::underlying_type_t<Mode>']


def test_decltype_casts_fail(tree):
    assert findings(tree, 'auto a = decltype(y)(z);\ndecltype(y) b = z;\n') == ['a.cpp:1 decltype(y)']


def test_pointer_c_casts_fail(tree):
    text = 'auto a = (char*)p;\nauto b = (Frame const*)q;\n'
    assert findings(tree, text) == ['a.cpp:1 char*', 'a.cpp:2 Frame const*']


def test_enum_c_casts_fail(tree):
    assert findings(tree, 'auto a = (Mode)x;\nauto b = (sdl::Mode)3;\n') == ['a.cpp:1 Mode', 'a.cpp:2 sdl::Mode']


def test_parenthesised_expressions_and_declarators_pass(tree):
    text = ('auto a = (b) - c;\nauto d = (*fp)(x);\nif (ready) Reject();\n'
            'using Open = Stream*(SDLCALL*)(char const*);\n'
            '#if defined(SDL_PLATFORM_WINDOWS)\n#include "x.h"\n#endif\nauto e = (f) and g;\nreturn (h) ? i : j;\n')
    assert findings(tree, text) == []


def test_function_types_in_template_arguments_pass(tree):
    assert findings(tree, 'std::pair<int, void(int)> a;\nstd::map<Key, bool(Value const&)> b;\n') == []


def test_base_initialisers_pass(tree):
    text = ('template <class BaseTy> class D : public BaseTy {\n'
            '  explicit D(int v) : BaseTy(v) { }\n  D(int v, int w) noexcept : BaseTy(v), w(w) { }\n};\n')
    assert findings(tree, text) == []


def test_multiword_types_are_spelled_with_spaces(tree):
    assert findings(tree, 'auto a = (unsigned char)x;\n') == ['a.cpp:1 unsigned char']


def test_casts_after_a_less_than_comparison_fail(tree):
    text = 'if (a < int(b)) { }\nauto c = a < double(b) ? 1 : 2;\n'
    assert findings(tree, text) == ['a.cpp:1 int', 'a.cpp:2 double']
