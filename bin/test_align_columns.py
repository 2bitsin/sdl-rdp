"""Exercise column alignment and preservation of C and C++ source."""
import importlib.util
import pathlib
import subprocess
import sys

import pytest

SPEC   = importlib.util.spec_from_file_location('align_columns', pathlib.Path(__file__).with_name('align-columns.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def align(text):
    return MODULE.align(text).text


@pytest.mark.parametrize(('source', 'expected'), [
    pytest.param(
        'auto const* message =\n    code == FAILED ? "failed" : "disconnected";\n',
        'auto const* message =\n    code == FAILED ? "failed" : "disconnected";\n',
        id='comparison_continuation_is_not_an_enumerator',
    ),
    pytest.param(
        'std::unique_ptr<Handle, decltype(&close)> handle{nullptr, close};\n',
        'std::unique_ptr<Handle, decltype(&close)> handle{ nullptr, close };\n',
        id='decltype_template_is_not_a_function_declaration',
    ),
    pytest.param(
        'if (count < 0 && errno == EINTR) continue;\n'
        'if (value == other) return value;\n'
        'while (count == 0) wait();\n'
        'utilities::Expects(count >= 0, "text read succeeds");\n',
        'if (count < 0 && errno == EINTR) continue;\n'
        'if (value == other) return value;\n'
        'while (count == 0) wait();\n'
        'utilities::Expects(count >= 0, "text read succeeds");\n',
        id='conditions_and_qualified_calls_are_not_declarations',
    ),
    pytest.param(
        'result =\n  compute();\n',
        'result =\n  compute();\n',
        id='multiline_assignment_is_not_an_enumerator',
    ),
    pytest.param(
        ': alpha{ a }, beta{ 2 }\n',
        ': alpha{ a }, beta{ 2 }\n',
        id='one_line_initialiser_list',
    ),
    pytest.param(
        'Thing() : alpha{ nested{ 1, 2 } }, beta{ 2 } {}\n',
        'Thing() : alpha{ nested{ 1, 2 } }, beta{ 2 } { }\n',
        id='balanced_initialiser_list_with_body',
    ),
    pytest.param(
        ': alpha{ a },\n  beta{ 2 },\n  gamma{ 333 }\n',
        ': alpha{ a   },\n  beta { 2   },\n  gamma{ 333 }\n',
        id='trailing_comma_initialisers_different_indent',
    ),
    pytest.param(
        '  : alpha{ a },\n    beta{ 2 },\n    longest{ 333 }\n',
        '  : alpha  { a   },\n    beta   { 2   },\n    longest{ 333 }\n',
        id='trailing_comma_initialisers',
    ),
    pytest.param(
        'int (*fp)(int) = nullptr;\nunsigned (*longer)(void) = nullptr;\n',
        'int      (*fp)    (int)  = nullptr;\nunsigned (*longer)(void) = nullptr;\n',
        id='function_pointer_initialisers',
    ),
    pytest.param(
        'char const* (*last_error)(void);\nunsigned (*version)(void);\nint (*wait)(int);\n',
        'char const* (*last_error)(void);\nunsigned    (*version)   (void);\nint         (*wait)      (int);\n',
        id='function_pointer_declarations',
    ),
    pytest.param(
        '  auto Name(int x) -> Ret;\n  // continuation\n  [[nodiscard]] auto Longer() -> bool;\n',
        '  auto               Name(int x) -> Ret;\n  // continuation\n'
        '  [[nodiscard]] auto Longer()    -> bool;\n',
        id='trailing_return_member_run',
    ),
    pytest.param(
        '  static int A();\n  virtual void Longer(int x);\n  constexpr bool B() const;\n'
        '  explicit Thing(int x);\n',
        '  static int     A();\n  virtual void   Longer(int x);\n'
        '  constexpr bool B() const;\n  explicit       Thing(int x);\n',
        id='ordinary_member_run',
    ),
    pytest.param(
        'int rate{60};\nint x = 0;\nint longer = some_very_long_expression;\n',
        'int rate   { 60 };\nint x      = 0;\nint longer = some_very_long_expression;\n',
        id='closer_width_uses_only_braces',
    ),
    pytest.param(
        'A = 1,\nLAST = 22\n',
        'A    = 1,\nLAST = 22\n',
        id='last_enumerator_without_comma',
    ),
    pytest.param(
        "int x = 1'000;\nlong longer = 2'000;\nchar c = 'x';\n",
        "int  x      = 1'000;\nlong longer = 2'000;\nchar c      = 'x';\n",
        id='digit_separators_do_not_mask_lines',
    ),
    pytest.param(
        'Thing& operator=(Thing const&) = delete;\nint operator()(int x) const;\n'
        'bool operator==(Thing const&) const;\n',
        'Thing& operator=(Thing const&) = delete;\nint    operator()(int x) const;\n'
        'bool   operator==(Thing const&) const;\n',
        id='operators_are_functions',
    ),
    pytest.param(
        'bool f : 1;\nalignas(16) char buffer[64];\nint x{2}; /* note */\nint longer{3}; // end\n',
        'bool             f          : 1;\nalignas(16) char buffer[64];\n'
        'int              x          { 2 };  /* note */\nint              longer     { 3 };  // end\n',
        id='bitfield_alignas_and_block_comment',
    ),
    pytest.param(
        'void Empty() {}\nvoid Defaults(Value value = {});\nreturn {};\n',
        'void Empty() { }\nvoid Defaults(Value value = { });\nreturn { };\n',
        id='empty_bodies_defaults_and_returns',
    ),
    pytest.param(
        'case A: f(); // one\ncase LONG: g(); /* two */\n',
        'case A:    f();  // one\ncase LONG: g();  /* two */\n',
        id='case_trailing_comments',
    ),
    pytest.param(
        'int a{1};\n'
        'long longer{22};\n',
        'int  a     { 1  };\n'
        'long longer{ 22 };\n',
        id='declaration_value_and_closer_columns',
    ),
    pytest.param(
        'int x{};\n'
        'int y{  };\n',
        'int x{ };\n'
        'int y{ };\n',
        id='empty_braces',
    ),
    pytest.param(
        'int x;\n'
        'long longer = 2;\n',
        'int  x;\n'
        'long longer = 2;\n',
        id='mixed_declaration_shapes',
    ),
    pytest.param(
        'static constexpr std::vector<int> a{1};\n'
        'volatile int* b{2};\n'
        'int c[2]{3};\n',
        'static constexpr std::vector<int> a   { 1 };\n'
        'volatile int*                     b   { 2 };\n'
        'int                               c[2]{ 3 };\n',
        id='qualified_template_pointer_array_types',
    ),
    pytest.param(
        'x = 1;\n'
        'longer += 2;\n',
        'x      =  1;\n'
        'longer += 2;\n',
        id='assignment_columns',
    ),
    pytest.param(
        ': x{1},\n'
        ', longer{22}\n',
        ': x     { 1  },\n'
        ', longer{ 22 }\n',
        id='initialiser_columns',
    ),
    pytest.param(
        'case A: f();\n'
        'case LONG: g();\n',
        'case A:    f();\n'
        'case LONG: g();\n',
        id='case_columns',
    ),
    pytest.param(
        'A = 1,\n'
        'LONG = 2,\n'
        'END,\n',
        'A    = 1,\n'
        'LONG = 2,\n'
        'END,\n',
        id='enumerator_columns',
    ),
    pytest.param(
        'int a = 0, b = 1;\n',
        'int a = 0;\n'
        'int b = 1;\n',
        id='split_initialised_declarators',
    ),
    pytest.param(
        'Region dirty, sending;\n',
        'Region dirty;\n'
        'Region sending;\n',
        id='split_plain_declarators',
    ),
    pytest.param(
        'std::pair<int,int> a{1,2}, b{};\n',
        'std::pair<int,int> a{ 1,2 };\n'
        'std::pair<int,int> b{     };\n',
        id='top_level_commas_only',
    ),
    pytest.param(
        'int a{1}; // a\n'
        'int longer{22}; // b\n',
        'int a     { 1  };  // a\n'
        'int longer{ 22 };  // b\n',
        id='trailing_comment_column',
    ),
    pytest.param(
        'int a{1};\n'
        '// stays\n'
        'int longer{22};\n',
        'int a     { 1  };\n'
        '// stays\n'
        'int longer{ 22 };\n',
        id='comment_only_continues_group',
    ),
    pytest.param(
        'int a{1};\n'
        'private:\n'
        'int longer{22};\n',
        'int a{ 1 };\n'
        'private:\n'
        'int longer{ 22 };\n',
        id='access_specifier_ends_group',
    ),
    pytest.param(
        'int a{1};\n'
        '\n'
        'int longer{22};\n',
        'int a{ 1 };\n'
        '\n'
        'int longer{ 22 };\n',
        id='blank_line_ends_group',
    ),
    pytest.param(
        '#define X int a{}; \\\n'
        'int b{};\n'
        '/* int a{};\n'
        'int b{}; */\n',
        '#define X int a{}; \\\n'
        'int b{};\n'
        '/* int a{};\n'
        'int b{}; */\n',
        id='preprocessor_and_block_comment_untouched',
    ),
    pytest.param(
        'auto s = "a = {  }";\n'
        "auto c = '{';\n"
        'auto r = R"xx(a = {  })xx";\n',
        'auto s = "a = {  }";\n'
        "auto c = '{';\n"
        'auto r = R"xx(a = {  })xx";\n',
        id='literals_untouched',
    ),
    pytest.param(
        'auto s = R"xx(\n'
        'int a{};\n'
        ')xx";\n',
        'auto s = R"xx(\n'
        'int a{};\n'
        ')xx";\n',
        id='multiline_raw_string_untouched',
    ),
    pytest.param(
        'int* a, b; // end\n',
        'int* a;\n'
        'int  b;  // end\n',
        id='pointer_belongs_to_declarator',
    ),
    pytest.param(
        'int a, *b;\n',
        'int   a;\n'
        'int * b;\n',
        id='pointer_on_later_declarator',
    ),
    pytest.param(
        'class Child : public Base, public Other {};\n',
        'class Child : public Base, public Other { };\n',
        id='inheritance_untouched',
    ),
    pytest.param(
        'int a, b; // end\n',
        'int a;\n'
        'int b;  // end\n',
        id='split_keeps_trailing_comment',
    ),
    pytest.param(
        '  if (index >= 0 && values[index]) return values[index];\n',
        '  if (index >= 0 && values[index]) return values[index];\n',
        id='inline_return_untouched',
    ),
    pytest.param(
        '  if (ready) x = 1;\n',
        '  if (ready) x = 1;\n',
        id='inline_assignment_untouched',
    ),
    pytest.param(
        'call(value = 1);\n'
        'longer = 2;\n',
        'call(value = 1);\n'
        'longer = 2;\n',
        id='call_argument_assignment_untouched',
    ),
])
def test_alignment_rule(source, expected):
    assert align(source) == expected
    assert align(expected) == expected


def test_limit():
    line = 'int enormous = ' + 'x' * 110 + ';\n'
    result = MODULE.align('int a{1};\n' + line + 'int bb{22};\n')
    assert result.text == 'int a { 1  };\n' + line + 'int bb{ 22 };\n'
    assert result.groups == 1
    assert result.exceptions == [(2, line.rstrip())]


PEER = '''
  // RDP has no acknowledgement deadline; one second bounds a viewer-tolerable frozen picture.
  static constexpr auto     AcknowledgementTimeout = std::chrono::seconds(1);
  static constexpr unsigned FrameWindow            = 2;
  static constexpr DWORD    AppendedHandleCount    = 5 + Input::MaxHandles;
  std::unique_ptr<GfxChannel> gfx;
  bool                  gfx_attempted          = false;
  UINT32                gfx_id                 = UINT32_MAX;
  static constexpr auto GraphicsConnectionWait = std::chrono::seconds(3);
  Clock::time_point activated_at;
  std::chrono::nanoseconds         graphics_ready_time{};
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU graphics_qoe       {};
  std::optional<sdlrdp_event> connection;
  bool sound_attempted = false;
  std::unique_ptr<AudioChannel> sound;
  PeerHandle                    client;
  int socket_descriptor = client->sockfd;
  State&    owner;
  WakeEvent wake;
  DWORD handle_count{ 0 };
  std::vector<std::string> trace_pending;
  Region dirty, sending;
  std::shared_ptr<std::vector<BYTE> const> snapshot;
  unsigned snapshot_width = 0, snapshot_height = 0;
  std::vector<Column> scale_columns;
  int scale_x = -1, scale_width = 0;
  unsigned scale_source = 0;
  uint64_t sequence = 0, acknowledged = 0;
  UINT32 frame_id = 0;
  std::deque<Pending> pending;

  uint64_t avc_frames = 0;
  std::chrono::nanoseconds avc_convert{}, avc_upload{}, avc_encode{};
  uint64_t acks_timed_out = 0;
  uint64_t frames_sent = 0, frames_coalesced = 0, dirty_presents = 0, ack_count = 0, ack_over_100ms = 0;
  std::chrono::nanoseconds encoded_at_start{}, encode_total{}, encode_max{}, ack_total{}, ack_max{};
  unsigned screen_width = 0, screen_height = 0;
  bool ack_enabled = false, suppressed = false;
  HANDLE channels = nullptr;
  std::unique_ptr<DispServerContext, Releases<disp_server_context_free>> disp;
  std::unique_ptr<ClipboardChannel> clipboard;
  std::shared_ptr<DriveChannel>     drive;
  UINT32 display_id = UINT32_MAX;
  bool disp_open = false, resizing = false;
'''


PEER_EXPECTED = '''
  // RDP has no acknowledgement deadline; one second bounds a viewer-tolerable frozen picture.
  static constexpr auto                    AcknowledgementTimeout = std::chrono::seconds(1);
  static constexpr unsigned                FrameWindow            = 2;
  static constexpr DWORD                   AppendedHandleCount    = 5 + Input::MaxHandles;
  std::unique_ptr<GfxChannel>              gfx;
  bool                                     gfx_attempted          = false;
  UINT32                                   gfx_id                 = UINT32_MAX;
  static constexpr auto                    GraphicsConnectionWait = std::chrono::seconds(3);
  Clock::time_point                        activated_at;
  std::chrono::nanoseconds                 graphics_ready_time    {   };
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU         graphics_qoe           {   };
  std::optional<sdlrdp_event>              connection;
  bool                                     sound_attempted        = false;
  std::unique_ptr<AudioChannel>            sound;
  PeerHandle                               client;
  int                                      socket_descriptor      = client->sockfd;
  State&                                   owner;
  WakeEvent                                wake;
  DWORD                                    handle_count           { 0 };
  std::vector<std::string>                 trace_pending;
  Region                                   dirty;
  Region                                   sending;
  std::shared_ptr<std::vector<BYTE> const> snapshot;
  unsigned                                 snapshot_width         = 0;
  unsigned                                 snapshot_height        = 0;
  std::vector<Column>                      scale_columns;
  int                                      scale_x                = -1;
  int                                      scale_width            = 0;
  unsigned                                 scale_source           = 0;
  uint64_t                                 sequence               = 0;
  uint64_t                                 acknowledged           = 0;
  UINT32                                   frame_id               = 0;
  std::deque<Pending>                      pending;

  uint64_t                                                               avc_frames       = 0;
  std::chrono::nanoseconds                                               avc_convert      { };
  std::chrono::nanoseconds                                               avc_upload       { };
  std::chrono::nanoseconds                                               avc_encode       { };
  uint64_t                                                               acks_timed_out   = 0;
  uint64_t                                                               frames_sent      = 0;
  uint64_t                                                               frames_coalesced = 0;
  uint64_t                                                               dirty_presents   = 0;
  uint64_t                                                               ack_count        = 0;
  uint64_t                                                               ack_over_100ms   = 0;
  std::chrono::nanoseconds                                               encoded_at_start { };
  std::chrono::nanoseconds                                               encode_total     { };
  std::chrono::nanoseconds                                               encode_max       { };
  std::chrono::nanoseconds                                               ack_total        { };
  std::chrono::nanoseconds                                               ack_max          { };
  unsigned                                                               screen_width     = 0;
  unsigned                                                               screen_height    = 0;
  bool                                                                   ack_enabled      = false;
  bool                                                                   suppressed       = false;
  HANDLE                                                                 channels         = nullptr;
  std::unique_ptr<DispServerContext, Releases<disp_server_context_free>> disp;
  std::unique_ptr<ClipboardChannel>                                      clipboard;
  std::shared_ptr<DriveChannel>                                          drive;
  UINT32                                                                 display_id       = UINT32_MAX;
  bool                                                                   disp_open        = false;
  bool                                                                   resizing         = false;
'''


def test_peer_idempotence():
    assert align(PEER) == PEER_EXPECTED
    assert align(PEER_EXPECTED) == PEER_EXPECTED


def test_check_lists_every_changed_file(tmp_path):
    paths = [tmp_path / 'one.c', tmp_path / 'two.hpp']
    for path in paths:
        path.write_text('int a{};\n')
    command = [sys.executable, str(pathlib.Path(MODULE.__file__)), '--check', str(tmp_path)]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 1
    assert result.stdout == ''.join(f'{path}\n' for path in paths)
    assert result.stderr == ''
    assert all(path.read_text() == 'int a{};\n' for path in paths)
    for path in paths:
        path.write_text(align(path.read_text()))
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0
    assert not result.stdout


@pytest.mark.parametrize('filename', ['align-columns.py', 'test_align_columns.py'])
def test_check_enforces_python_column_limit(tmp_path, filename):
    tool = tmp_path / 'align-columns.py'
    tool.write_text(pathlib.Path(MODULE.__file__).read_text())
    test = tmp_path / 'test_align_columns.py'
    test.write_text('')
    target = tmp_path / filename
    with target.open('a') as stream:
        stream.write('\n#' + 'x' * MODULE.COLUMN_LIMIT + '\n')
    result = subprocess.run([sys.executable, str(tool), '--self-check', str(tmp_path)], capture_output=True, text=True)
    assert result.returncode == 1
    number = len(target.read_text().splitlines())
    assert result.stdout == f'{target}:{number}: 121 columns > 120\n'
    assert result.stderr == ''


def test_pytest_guard_skips_without_pytest(tmp_path):
    environment = tmp_path / 'python'
    subprocess.run([sys.executable, '-m', 'venv', '--without-pip', str(environment)], check=True)
    runner = pathlib.Path(__file__).with_name('test-align-columns.sh')
    result = subprocess.run([str(runner), str(environment / 'bin/python')], capture_output=True, text=True)
    assert result.returncode == 77
    assert result.stdout == 'align-columns-test skipped: pytest is not importable\n'
    assert result.stderr == ''


@pytest.mark.parametrize(('source', 'expected'), [
    ('operator bool() const;\n', 'operator bool() const;\n'),
    ('explicit operator unsigned long() const;\n', 'explicit operator unsigned long() const;\n'),
    ('int operator[](int n);\n', 'int operator[](int n);\n'),
    ('void* operator new[](size_t size);\n', 'void* operator new[](size_t size);\n'),
    ('Thing& operator = (Thing const&) = delete;\n', 'Thing& operator =(Thing const&) = delete;\n'),
])
def test_all_operator_declarations(source, expected):
    item = MODULE.parse(source.rstrip(), MODULE.mask(source.rstrip()))
    assert item.kind == 'function'
    assert align(source) == expected
    assert align(expected) == expected


@pytest.mark.parametrize(('source', 'expected'), [
    (
        'int a{};\nint c = 5;\n',
        'int a { };\nint c = 5;\n',
    ),
    (
        'int table{ 1, 2 };\nint x : 3;\nint uninitialised;\n',
        'int table         { 1, 2 };\nint x             : 3;\nint uninitialised;\n',
    ),
    (
        '  : alpha{ 1 },\n    beta{ 22 },\n    gamma_long{ 3 } {}\n',
        '  : alpha     { 1  },\n    beta      { 22 },\n    gamma_long{ 3  } { }\n',
    ),
    (
        'auto t = f(g(x) ? a : b,   c);\n',
        'auto t = f(g(x) ? a : b,   c);\n',
    ),
    (
        'Thing() : alpha{1},   beta{22} {}\n',
        'Thing() : alpha{ 1 }, beta{ 22 } { }\n',
    ),
    (
        '  void (*close)(Handle*);\n'
        '  int (*drive_enumerate)(Handle*, unsigned drive,\n'
        '                         unsigned max);\n'
        '  bool (*ready)(Handle*);\n',
        '  void (*close)          (Handle*);\n'
        '  int  (*drive_enumerate)(Handle*, unsigned drive,\n'
        '                         unsigned max);\n'
        '  bool (*ready)          (Handle*);\n',
    ),
    (
        'struct Peer {\n  Peer(Handle accepted);\n  ~Peer();\n  void Start();\n'
        '  bool Ready(int first,\n             int second) const;\n  unsigned Count();\n};\n',
        'struct Peer {\n           Peer(Handle accepted);\n           ~Peer();\n  void     Start();\n'
        '  bool     Ready(int first,\n             int second) const;\n  unsigned Count();\n};\n',
    ),
], ids=['mixed_empty_and_equals', 'table_and_bitfield', 'last_initialiser_has_body',
        'ternary_untouched', 'constructor_list', 'multiline_function_pointer', 'constructors_and_members'])
def test_review_round_three(source, expected):
    assert align(source) == expected
    assert align(expected) == expected


def test_overflow_collapses_padding_and_check_reports_it(tmp_path):
    value = 'x' * 110
    source = '  int   huge    =   ' + value + ';\n'
    expected = '  int huge = ' + value + ';\n'
    result = MODULE.align(source)
    assert result.text == expected
    assert result.exceptions == [MODULE.Overflow(1, expected.rstrip())]
    assert align(expected) == expected
    path = tmp_path / 'overflow.cpp'
    path.write_text(source)
    command = [sys.executable, MODULE.__file__, '--check', str(path)]
    checked = subprocess.run(command, capture_output=True, text=True)
    assert (checked.returncode, checked.stdout, checked.stderr) == (1, f'{path}\n', '')
    assert path.read_text() == source
    path.write_text(expected)
    checked = subprocess.run(command, capture_output=True, text=True)
    assert (checked.returncode, checked.stdout, checked.stderr) == (0, '', '')


@pytest.mark.parametrize(('source', 'expected'), [
    (
        'struct Peer {\n  Peer();\n  // member declarations\n'
        '  unsigned Count(int first,\n                 int second);\n};\n',
        'struct Peer {\n           Peer();\n  // member declarations\n'
        '  unsigned Count(int first,\n                 int second);\n};\n',
    ),
    (
        'int    long_name =   f("keep   spaces",   ' + 'x' * 110 + '); // keep   comment\n',
        'int long_name = f("keep   spaces", ' + 'x' * 110 + '); // keep   comment\n',
    ),
], ids=['constructor_before_multiline_member', 'overflow_preserves_protected_spacing'])
def test_continuation_and_protection(source, expected):
    assert align(source) == expected
    assert align(expected) == expected


def test_uppercase_calls_keep_control_flow_indentation():
    source = ('void Check() {\n'
              '  Open(AUTH_TLS);\n'
              '  Client client(port(), false);\n'
              '  for (unsigned i = 0; i < 10; ++i)\n'
              '    Attempt("alice", "wrong-secret", false);\n'
              '  Attempt("alice", "correct-secret", true);\n'
              '}\n')
    assert align(source) == source
    assert align(align(source)) == source
