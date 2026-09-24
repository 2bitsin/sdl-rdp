"""Unit tests for the function, column, contract, preprocessor and header measures of shape.py."""

import pytest

import shape

REFERENCES = '  T&& x = a;\n  Foo&& y = b;\n  decltype(a)&& z = a;\n  std::string&& w = "";\n  auto&& v = a;\n'
TRY_BLOCK  = 'void F(int a) try {\n  if (a) ++a;\n} catch (...) {\n  if (a) { if (a) { if (a) ++a; } }\n}\n'
LAMBDA     = ('void F(int a) {\n  auto g = [&](int x, int y, int z, int w, int v, int u) {\n'
              '    if (x) { if (y) { if (z) { ++a; } } }\n  };\n  g(1, 2, 3, 4, 5, 6);\n}\n')
CONSTEXPR  = ('template <class T> void F(T a) {\n  if constexpr (sizeof(T) > 4) {\n'
              '    if constexpr (sizeof(T) > 8) {\n      if (a) ++a;\n    }\n  }\n}\n')
MACROS     = ('#define BEGIN {\n#define END }\nvoid F(int a) BEGIN\n  if (a) ++a;\nEND\n'
              'void G(int a) {\n  if (a) { if (a) { if (a) ++a; } }\n}\n')
DISABLED   = '#if 0\nvoid Hidden(int a, int b, int c, int d, int e, int f) { }\n#endif\nvoid G(int a) { ++a; }\n'
CONTINUED  = '#define OPEN(x) \\\n  { if (x) { \\\n  } \\\n\nvoid G(int a) { ++a; }\n'
UNBALANCED = 'void F(int a) {\n#ifdef A\n  if (a) {\n#else\n  if (!a) {\n#endif\n  }\n}\n'


def write(tmp_path, source, name='sample.cpp'):
    path = tmp_path / name
    path.write_text(source)
    return path


def labels(tmp_path, source, name='sample.cpp'):
    path = write(tmp_path, source, name)
    return [finding.key.split(': ', 1)[1] for finding in shape.source_findings(shape.Source(path))]


def values(tmp_path, source):
    """Return name: (body lines, complexity, parameters, nesting) for every function and lambda."""
    source = shape.Source(write(tmp_path, source))
    functions = list(shape.definitions(source, 0, len(source.tokens)))
    closures = shape.lambdas(source, functions)
    return {function.name: (shape.body_lines(source, function),
                            shape.complexity(source, function, shape.nested_in(function, closures)),
                            shape.parameter_count(source, function.parameters),
                            shape.depth(shape.parse_block(source, function.start, function.end)))
            for function in functions + closures}


def contracts(tmp_path, source):
    return len(shape.compound_contracts(shape.Source(write(tmp_path, source))))


def function(body, parameters='int a'):
    return f'void Sample({parameters}) {{\n{body}}}\n'


def statements(count):
    return '  ++a;\n' * count


def branches(count):
    return '  if (a) ++a;\n' * count


def overlong(prefix, suffix):
    return prefix + 'a' * (shape.COLUMNS + 1 - len(prefix) - len(suffix)) + suffix


def python_labels(tmp_path, source):
    path = tmp_path / 'sample.py'
    path.write_text(source)
    return [finding.key.split(': ', 1)[1] for finding in shape.python_findings(path)]


@pytest.mark.parametrize(('lines', 'expected'), [
    (shape.BODY_LINES_NORM,     []),
    (shape.BODY_LINES_NORM + 1, ['Sample body lines 21 > 20']),
    (shape.BODY_LINES,          ['Sample body lines 40 > 20']),
    (shape.BODY_LINES + 1,      ['Sample body lines 41 > 40'])])
def test_body_lines(tmp_path, lines, expected):
    assert labels(tmp_path, function(statements(lines))) == expected


def test_body_lines_skip_blank_and_comment_lines(tmp_path):
    body = '  ++a;\n\n  // note\n  /* note */\n' * 10 + '  /* c */ ++a; }'
    assert values(tmp_path, f'void F(int a) {{\n{body}\n')['F'][0] == 11


@pytest.mark.parametrize(('count', 'expected'), [(shape.COMPLEXITY - 1, []),
                                                 (shape.COMPLEXITY,     ['Sample complexity 11 > 10'])])
def test_complexity(tmp_path, count, expected):
    assert labels(tmp_path, function(branches(count))) == expected


def test_switch_cases_are_branches(tmp_path):
    cases = ''.join(f'    case {index}: return {index};\n' for index in range(11))
    source = f'int F(int a) {{\n  switch (a) {{\n{cases}    default: return 0;\n  }}\n}}\n'
    assert labels(tmp_path, source) == ['F complexity 12 > 10']


@pytest.mark.parametrize(('count', 'expected'), [(shape.PARAMETERS,     []),
                                                 (shape.PARAMETERS + 1, ['Sample parameters 6 > 5'])])
def test_parameters(tmp_path, count, expected):
    parameters = ', '.join(f'int p{index}' for index in range(count))
    assert labels(tmp_path, function('', parameters)) == expected


@pytest.mark.parametrize(('parameters', 'expected'), [
    ('int a = b > c, int x, int y, int z, int w, int v',              6),
    ('std::map<int, std::pair<int, int>> m, int b',                     2),
    ('std::function<void(int, int)> f, std::unique_ptr<T, D> p',        2),
    ('T&& a, auto&& b, T&&c',                                           3),
    ('bool a = x<y, bool b = z>w, int c, int d, int e, int f',          6)])
def test_parameter_lists(tmp_path, parameters, expected):
    assert values(tmp_path, f'void G({parameters}) {{ }}\n')['G'][2] == expected


def test_nesting_three_fails(tmp_path):
    body = '  if (a) {\n    for (;;) {\n      if (a) ++a;\n    }\n  }\n'
    assert labels(tmp_path, function(body)) == ['Sample nesting 3 > 2']


def test_else_if_chain_keeps_depth(tmp_path):
    body = '  for (;;) {\n    if (a) ++a;\n    else if (a) --a;\n    else a = 0;\n  }\n'
    assert labels(tmp_path, function(body)) == []


def test_attribute_does_not_hide_a_control_statement(tmp_path):
    body = '  [[likely]] if (a) { if (a) { if (a) ++a; } }\n'
    assert labels(tmp_path, function(body)) == ['Sample nesting 3 > 2']


def test_if_constexpr_nests(tmp_path):
    assert labels(tmp_path, CONSTEXPR) == ['F nesting 3 > 2']


def test_references_are_not_branches(tmp_path):
    assert values(tmp_path, 'template <class T> void F(T a) {\n' + REFERENCES + '}\n')['F'][1] == 1


@pytest.mark.parametrize(('condition', 'expected'), [('sizeof(T) > 4 && a', 3),
                                                     ('std::is_integral_v<T> && sizeof(T) > 4', 3),
                                                     ('std::is_same_v<T, int> || a', 3)])
def test_logical_operators_after_a_closer_are_branches(tmp_path, condition, expected):
    source = f'template <class T> void F(T a) {{\n  if constexpr ({condition}) ++a;\n}}\n'
    assert values(tmp_path, source)['F'][1] == expected


def test_shift_is_not_a_template(tmp_path):
    source = 'std::vector<std::vector<int>> F(int b) {\n  if (b >> 1) ++b;\n  return {};\n}\n'
    assert values(tmp_path, source)['F'] == (2, 2, 1, 1)


def test_literals_hold_no_branches(tmp_path):
    body = '  auto s = R"x(a && b || c ? d : e if (x) )x";\n  auto t = "if && ||";\n  auto m = 1\'000\'000;\n'
    assert values(tmp_path, function(body))['Sample'][1] == 1


def test_lambda_is_its_own_function(tmp_path):
    assert labels(tmp_path, LAMBDA) == ['F lambda@2 parameters 6 > 5', 'F lambda@2 nesting 3 > 2']


def test_lambda_at_namespace_scope(tmp_path):
    source = 'auto g = [](int a) {\n  if (a) { if (a) { if (a) ++a; } }\n};\n'
    assert labels(tmp_path, source) == ['lambda@1 nesting 3 > 2']


def test_constructor_initialisers_are_not_bodies(tmp_path):
    source = 'struct S { int a; int b; S(int x); };\nS::S(int x) : a{x}, b{x} {\n  if (x) ++a;\n}\n'
    assert values(tmp_path, source) == {'S::S': (1, 2, 1, 1)}


def test_function_try_block_is_one_function(tmp_path):
    assert values(tmp_path, TRY_BLOCK) == {'F': (4, 6, 1, 3)}


@pytest.mark.parametrize(('source', 'expected'), [
    ('Expects(a\n        && b, "x");', 1), ('Expects(g(a && b), "x");', 1), ('Expects(a > 0 and b > 0, "x");', 1),
    ('Ensures(!(a || b), "x");', 1), ('Expects((a && b), "x");', 1), ('Expects(a or b, "x");', 1),
    ('Expects(a, "x");', 0), ('Expects(a, "x && y");', 0), ('Expects(a, g(b && c));', 0),
    ('Ensures(v == T{a && b}, "y");', 1), ('Expects(g([]{ return a && b; }()), "x");', 0)])
def test_compound_contracts(tmp_path, source, expected):
    assert contracts(tmp_path, function(f'  {source}\n')) == expected


def test_long_code_line_fails(tmp_path):
    assert labels(tmp_path, overlong('int ', ';') + '\n') == ['long lines 1 > 0']


def test_long_string_literal_passes(tmp_path):
    assert labels(tmp_path, f"char const* text =\n{overlong('  \"', '\"')}\n;\n") == []


def test_code_after_a_long_literal_fails(tmp_path):
    line = overlong('  auto s = "', '"') + '; if (a) { ++a; }'
    assert labels(tmp_path, function(line + '\n')) == ['long lines 1 > 0']


@pytest.mark.parametrize(('source', 'expected'), [(MACROS, ['G nesting 3 > 2']), (DISABLED, []), (CONTINUED, [])])
def test_preprocessor(tmp_path, source, expected):
    assert labels(tmp_path, source) == expected


def test_unbalanced_braces_are_reported(tmp_path):
    assert labels(tmp_path, UNBALANCED) == ['unbalanced braces']


@pytest.mark.parametrize(('source', 'key'), [
    ('int YUV(int a, int b, int c, int d, int e, int f) { return a; }\n', 'YUV parameters 6 > 5'),
    ('TEST(Suite, Name) {\n' + branches(shape.COMPLEXITY) + '}\n',     'Suite.Name complexity 11 > 10')])
def test_keys(tmp_path, source, key):
    assert labels(tmp_path, source) == [key]


def test_function_entry_carries_parameter_list_line(tmp_path):
    path = write(tmp_path, '\n' + function('', 'int a, int b, int c, int d, int e, int f'))
    assert [finding.key for finding in shape.source_findings(shape.Source(path))] == [
        f'{shape.file_scope(path)}:2: Sample parameters 6 > 5']


def test_moved_definition_makes_entry_stale(tmp_path, capsys):
    allow = tmp_path / 'allow'
    allow.write_text('# header\nsample.cpp:1: Sample parameters 6 > 5 # size in #19\n')
    moved = shape.finding('sample.cpp:2', shape.Measure(2, 'Sample parameters', 6, shape.PARAMETERS))
    assert shape.check_allow([moved], allow) == 1
    assert f'{allow}:2: stale allow entry: sample.cpp:1: Sample parameters 6 > 5' in capsys.readouterr().out


def test_python_nesting_three_fails(tmp_path):
    source = 'def sample(a):\n    if a:\n        for b in a:\n            if b:\n                pass\n'
    assert python_labels(tmp_path, source) == ['sample nesting 3 > 2']


def test_python_elif_keeps_depth(tmp_path):
    source = 'def sample(a):\n    for b in a:\n        if b:\n            pass\n        elif a:\n            pass\n'
    assert python_labels(tmp_path, source) == []


def test_python_receiver_is_not_a_parameter(tmp_path):
    parameters = ', '.join(f'p{index}' for index in range(shape.PARAMETERS))
    source = f'class Sample:\n    def method(self, {parameters}):\n        pass\n'
    assert python_labels(tmp_path, source) == []


def test_python_body_lines(tmp_path):
    source = 'def sample():\n' + '    pass\n' * (shape.BODY_LINES_NORM + 1)
    assert python_labels(tmp_path, source) == ['sample body lines 21 > 20']


def test_python_long_line_fails(tmp_path):
    assert python_labels(tmp_path, overlong('x = ', '1') + '\n') == ['long lines 1 > 0']


N3 = ('void B(int a) {\n  Expects(std::ranges::all_of(v, [](int x) { return x > 0 && x < 9; }), "x");\n'
      '  if (a and b) ++a;\n  if (a or b) ++a;\n}\n'
      'template <class T> requires std::integral<T> && std::signed_integral<T>\n'
      'void C(T a) noexcept(noexcept(a && a)) {\n'
      '  ++a;\n}\nbool D(int a, int b) { return a<b && b>a; }\nauto E(int a) -> decltype(a && a) { return a; }\n'
      'int F(int a, int b, int c, int d, int e) try { return a; } catch (...) { return 0; }\n'
      'template <class Output, class Map = std::identity> void G(Output o, Map m) { }\n')


def test_review_probes(tmp_path):
    assert values(tmp_path, N3) == {'B': (3, 5, 1, 1), 'C': (1, 1, 1, 0), 'D': (0, 2, 2, 0), 'E': (0, 1, 1, 0),
                                    'F': (0, 2, 5, 0), 'G': (0, 1, 2, 0), 'B lambda@2': (0, 2, 1, 0)}


def test_braces_inside_a_contract_are_not_its_condition(tmp_path):
    assert contracts(tmp_path, N3) == 0


def check(tmp_path, capsys, entry, findings):
    allow = tmp_path / 'allow'
    allow.write_text(entry + '\n')
    return shape.check_allow(findings, allow), capsys.readouterr().out


def long_function(tmp_path):
    return shape.source_findings(shape.Source(write(tmp_path, function(statements(shape.BODY_LINES + 1)))))


def six_parameters(tmp_path):
    parameters = ', '.join(f'int p{index}' for index in range(shape.PARAMETERS + 1))
    return shape.source_findings(shape.Source(write(tmp_path, function('', parameters))))


def test_over_forty_fails_even_when_listed(tmp_path, capsys):
    findings = long_function(tmp_path)
    status, out = check(tmp_path, capsys, f'{findings[0].key} # size in #19', findings)
    assert (status, len(findings)) == (1, 1)
    assert 'allow entry for a hard limit' in out and findings[0].key in out


def test_entry_without_reason_fails(tmp_path, capsys):
    findings = six_parameters(tmp_path)
    status, out = check(tmp_path, capsys, findings[0].key, findings)
    assert status == 1 and 'allow entry without a reason' in out


def test_entry_with_reason_passes(tmp_path, capsys):
    findings = six_parameters(tmp_path)
    assert check(tmp_path, capsys, f'{findings[0].key} # size in #19', findings) == (0, '')


def test_logical_words_are_branches(tmp_path):
    assert shape.BRANCHES >= shape.LOGICAL
    assert values(tmp_path, function('  if (a and b) ++a;\n  if (a or b) ++a;\n'))['Sample'][1] == 5


def test_python_forty_one_lines_is_one_finding(tmp_path):
    source = 'def sample():\n' + '    pass\n' * (shape.BODY_LINES + 1)
    assert python_labels(tmp_path, source) == ['sample body lines 41 > 40']


def test_report_is_the_allow_entry(tmp_path, capsys):
    findings = six_parameters(tmp_path)
    assert check(tmp_path, capsys, '', findings) == (1, f'{shape.file_scope(tmp_path / "sample.cpp")}:1: '
                                                        'Sample parameters 6 > 5\n')


@pytest.mark.parametrize(('source', 'expected'), [
    ('class A {\npublic:\n  void F();\n};\nstruct B {\n  B(int value);\n};\n',
     ['classes with member functions 2 > 1']),
    ('class A {\npublic:\n  void F();\n};\nstruct Point {\n  int x{ };\n  int y{ };\n};\n', []),
    ('class A {\npublic:\n  void F();\n};\nclass Pinned {\nprotected:\n  Pinned() = default;\n'
     '  Pinned(Pinned const&) = delete;\n};\n', []),
    ('class A {\npublic:\n  void F();\nprivate:\n  struct Inner {\n    void G();\n  };\n};\n', []),
    ('template <class T> class A {\npublic:\n  A() { }\n  T F() const { return value; }\n'
     'private:\n  T value;\n};\n', []),
    ('class A {\npublic:\n  int F() const { return value; }\nprivate:\n  int value;\n};\n',
     ['A bodies in header 1 > 0']),
    ('class A {\npublic:\n  constexpr int F() const { return value; }\nprivate:\n  int value;\n};\n', []),
    ('class A {\npublic:\n  template <class T> void F(T) { }\n  void G(auto) { }\n};\n', []),
    ('class A {\n  struct Inner {\n    void G() { }\n  };\n};\n', ['A bodies in header 1 > 0']),
    ('class A {\npublic:\n  void F();\n};\ninline void A::F() { }\n', ['A bodies in header 1 > 0']),
    ('class A {\npublic:\n  template <class T> void F();\n};\ntemplate <> inline void A::F<int>() { }\n',
     ['A bodies in header 1 > 0'])])
def test_header_classes_and_bodies(tmp_path, source, expected):
    assert labels(tmp_path, source, 'sample.hpp') == expected


def test_sources_are_not_measured_as_headers(tmp_path):
    assert labels(tmp_path, 'class A {\n  void F() { }\n};\nclass B {\n  void G() { }\n};\n') == []


@pytest.mark.parametrize(('directory', 'expected'), [('support.test', {'Suite', 'Steps'}), ('support', {'Suite'})])
def test_fixture_helpers_live_in_test_directories(tmp_path, directory, expected):
    (tmp_path / directory).mkdir()
    helper  = write(tmp_path, 'class Steps {\nprotected:\n  void Given();\n};\n', f'{directory}/steps.hpp')
    fixture = write(tmp_path, 'class Suite : public Steps, public Test { };\n', 'suite.cpp')
    assert shape.fixture_classes([shape.Source(helper), shape.Source(fixture)]) == shape.FIXTURE_ROOTS | expected


def leading(tmp_path, source):
    return shape.leading_returns(shape.Source(write(tmp_path, source)))


@pytest.mark.parametrize(('source', 'expected'), [
    pytest.param('void Run(int a);\n', [1], id='void_declaration'),
    pytest.param('class A {\npublic:\n  static bool Ready() const;\n};\n', [3], id='static_member'),
    pytest.param('std::string A::Name() const { return { }; }\n', [1], id='qualified_definition'),
    pytest.param('auto Run(int a) -> void;\n', [], id='trailing_declaration'),
    pytest.param('class Foo {\npublic:\n  explicit Foo(int a);\n  ~Foo();\n};\nFoo::Foo(int a) { }\nFoo::~Foo() { }\n',
                 [], id='constructor_and_destructor'),
    pytest.param('template <class T> Lease<T>::Lease(T& a) : b{ a } { }\ntemplate <class T> Lease<T>::~Lease() { }\n',
                 [], id='class_template_constructor_and_destructor'),
    pytest.param('template <class T> std::string Lease<T>::Name() const { return { }; }\n', [1],
                 id='class_template_member_leading'),
    pytest.param('template <typename T>\n  requires std::integral<T>\nexplicit Foo(T a);\n', [],
                 id='constrained_constructor'),
    pytest.param('auto Run() -> void {\n  Stop(1);\n  int x = Get();\n}\n', [], id='call_on_its_own_line'),
    pytest.param('extern "C" auto sdlrdp_open(int a) -> int { return a; }\nextern "C" {\nauto F() -> int;\n}\n',
                 [], id='extern_c_trailing_definition'),
    pytest.param('using Callback = void (*)(int);\nauto Set(int (*create)(char*)) -> void;\n', [1, 2],
                 id='function_pointer_result_first'),
    pytest.param('using Callback = auto (*)(int) -> void;\nauto Call() -> int {\n  return (*next)(1);\n}\n', [],
                 id='function_pointer_trailing_and_call'),
])
def test_leading_return_types(tmp_path, source, expected):
    assert leading(tmp_path, source) == expected
