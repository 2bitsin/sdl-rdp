"""Exercise column alignment and preservation of C and C++ source."""
import pathlib
import re

import pytest

import columns


def run(capsys, *arguments):
    code = columns.main([str(argument) for argument in arguments])
    printed = capsys.readouterr()
    return code, printed.out, printed.err


def align(text):
    return columns.align(text).text


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
        'template <class ValueTy>\nclass Held {\n  Owner& _owner;\n  ValueTy _value;\n};\n',
        'template <class ValueTy>\nclass Held {\n  Owner&  _owner;\n  ValueTy _value;\n};\n',
        id='template_head_line_opens_its_class',
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
        '  auto Name(int x) -> Ret;\n  [[nodiscard]] auto Longer() -> bool;\n'
        '  // continuation\n  auto Last() -> int;\n',
        '  auto               Name(int x) -> Ret;\n'
        '  [[nodiscard]] auto Longer()    -> bool;\n'
        '  // continuation\n'
        '  auto Last() -> int;\n',
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
        'std::pair<int,int> b{ };\n',
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
        'int a{ 1 };\n'
        '// stays\n'
        'int longer{ 22 };\n',
        id='comment_only_ends_group',
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
    result = columns.align('int a{1};\n' + line + 'int bb{22};\n')
    assert result.text == 'int a { 1  };\n' + line + 'int bb{ 22 };\n'
    assert result.groups == 1
    assert result.exceptions == [columns.Overflow(2, line.rstrip(), len(line.rstrip()))]


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
  std::optional<driver_event> connection;
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
  std::chrono::nanoseconds                 graphics_ready_time    { };
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU         graphics_qoe           { };
  std::optional<driver_event>              connection;
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


def test_check_lists_every_changed_file(tmp_path, capsys):
    paths = [tmp_path / 'one.c', tmp_path / 'two.hpp']
    for path in paths:
        path.write_text('int a{};\n')
    assert run(capsys, '--check', tmp_path) == (1, ''.join(f'{path}\n' for path in paths), '')
    assert all(path.read_text() == 'int a{};\n' for path in paths)
    for path in paths:
        path.write_text(align(path.read_text()))
    assert run(capsys, '--check', tmp_path) == (0, '', '')


@pytest.mark.parametrize('filename', ['columns.py', 'test_columns.py'])
def test_check_enforces_python_column_limit(tmp_path, filename, capsys):
    (tmp_path / 'columns.py').write_text(pathlib.Path(columns.__file__).read_text())
    (tmp_path / 'test_columns.py').write_text('')
    target = tmp_path / filename
    with target.open('a') as stream:
        stream.write('\n#' + 'x' * columns.COLUMN_LIMIT + '\n')
    number = len(target.read_text().splitlines())
    assert run(capsys, '--self-check', tmp_path) == (1, f'{target}:{number}: 121 columns > 120\n', '')


@pytest.mark.parametrize(('source', 'expected'), [
    ('operator bool() const;\n', 'operator bool() const;\n'),
    ('explicit operator unsigned long() const;\n', 'explicit operator unsigned long() const;\n'),
    ('int operator[](int n);\n', 'int operator[](int n);\n'),
    ('void* operator new[](size_t size);\n', 'void* operator new[](size_t size);\n'),
    ('Thing& operator = (Thing const&) = delete;\n', 'Thing& operator = (Thing const&) = delete;\n'),
])
def test_all_operator_declarations(source, expected):
    item = columns.parse(source.rstrip(), columns.mask(source.rstrip()))
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
        'auto t = f(g(x) ? a : b, c);\n',
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
        '                          unsigned max);\n'
        '  bool (*ready)          (Handle*);\n',
    ),
    (
        'struct Peer {\n  Peer(Handle accepted);\n  ~Peer();\n  void Start();\n'
        '  bool Ready(int first,\n             int second) const;\n  unsigned Count();\n};\n',
        'struct Peer {\n           Peer(Handle accepted);\n           ~Peer();\n  void     Start();\n'
        '  bool     Ready(int first,\n                 int second) const;\n  unsigned Count();\n};\n',
    ),
], ids=['mixed_empty_and_equals', 'table_and_bitfield', 'last_initialiser_has_body',
        'ternary_untouched', 'constructor_list', 'multiline_function_pointer', 'constructors_and_members'])
def test_review_round_three(source, expected):
    assert align(source) == expected
    assert align(expected) == expected


def test_overflow_collapses_padding_and_check_reports_it(tmp_path, capsys):
    value = 'x' * 110
    source = '  int   huge    =   ' + value + ';\n'
    expected = '  int huge = ' + value + ';\n'
    result = columns.align(source)
    assert result.text == expected
    assert result.exceptions == [columns.Overflow(1, expected.rstrip(), len(expected.rstrip()))]
    assert align(expected) == expected
    path = tmp_path / 'overflow.cpp'
    path.write_text(source)
    assert run(capsys, '--check', path) == (1, f'{path}\n', '')
    assert path.read_text() == source
    path.write_text(expected)
    assert run(capsys, '--check', path) == (0, '', '')


@pytest.mark.parametrize(('source', 'expected'), [
    (
        'struct Peer {\n  Peer();\n'
        '  unsigned Count(int first,\n                 int second);\n  // member declarations\n  bool Ready();\n};\n',
        'struct Peer {\n           Peer();\n'
        '  unsigned Count(int first,\n                 int second);\n  // member declarations\n  bool Ready();\n};\n',
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


@pytest.mark.parametrize(('source', 'expected'), [
    pytest.param(
        'virtual void Short() override;\n'
        'virtual bool Longer(int x) const noexcept final;\n'
        'void Abstract() = 0;\n',
        'virtual void Short()                      override;\n'
        'virtual bool Longer(int x) const noexcept final;\n'
        'void         Abstract()                   = 0;\n',
        id='trailing_specifiers',
    ),
    pytest.param(
        'Foo(Foo&&) noexcept = default;\n'
        'Foo(Foo const&) = delete;\n'
        '~Foo() = default;\n',
        'Foo(Foo&&) noexcept = default;\n'
        'Foo(Foo const&)     = delete;\n'
        '~Foo()              = default;\n',
        id='standalone_special_members',
    ),
    pytest.param(
        'void Start();\n'
        'Type const& Name() const { return x; }\n'
        'bool Ready() const;\n',
        'void        Start();\n'
        'Type const& Name() const { return x; }\n'
        'bool        Ready() const;\n',
        id='inline_bodies_in_function_run',
    ),
    pytest.param(
        'int x;\n'
        'using Callback = void (*)();\n'
        'struct Impl;\n'
        'enum class Mode { One, Two };\n'
        'std::unique_ptr<Impl> impl;\n',
        'int                   x;\n'
        'using Callback = void (*)();\n'
        'struct Impl;\n'
        'enum class Mode{ One, Two };\n'
        'std::unique_ptr<Impl> impl;\n',
        id='alias_forward_and_enum_skipped_in_declarations',
    ),
    pytest.param(
        'struct X* const x = get();\n'
        'struct Longer* value = NULL;\n'
        'unsigned n = 1;\n',
        'struct X* const x     = get();\n'
        'struct Longer*  value = NULL;\n'
        'unsigned        n     = 1;\n',
        id='c_elaborated_types',
    ),
    pytest.param(
        'bool ready = false;\n'
        'std::function<void()> finalizing;\n'
        'unsigned desktops = 0;\n',
        'bool                  ready      = false;\n'
        'std::function<void()> finalizing;\n'
        'unsigned              desktops   = 0;\n',
        id='function_template_member',
    ),
    pytest.param(
        'int x = 1;\n'
        'auto const& [left, right] = pair;\n'
        'long value = 2;\n',
        'int         x             = 1;\n'
        'auto const& [left, right] = pair;\n'
        'long        value         = 2;\n',
        id='structured_bindings',
    ),
    pytest.param(
        'long first = 1;\n'
        'auto result =\n'
        '    Compute();\n'
        'int last = 2;\n'
        '\n'
        'auto entry = cast(load(\n'
        '    "plugin", flags));\n'
        'long another = 3;\n',
        'long first  = 1;\n'
        'auto result =\n'
        '    Compute();\n'
        'int  last   = 2;\n'
        '\n'
        'auto entry   = cast(load(\n'
        '    "plugin", flags));\n'
        'long another = 3;\n',
        id='multiline_equals_heads',
    ),
    pytest.param(
        'struct Thing {\n'
        '  Thing(Thing const&) = delete;\n'
        '  Thing(int value)\n'
        '      : value{ value } { }\n'
        '  ~Thing();\n'
        '  Thing& operator = (Thing&&) = delete;\n'
        '  int Get() const { return value; }\n'
        '};\n',
        'struct Thing {\n'
        '         Thing(Thing const&)  = delete;\n'
        '         Thing(int value)\n'
        '      : value{ value } { }\n'
        '         ~Thing();\n'
        '  Thing& operator = (Thing&&) = delete;\n'
        '  int    Get() const { return value; }\n'
        '};\n',
        id='constructor_multiline_initializer',
    ),
    pytest.param(
        '  .rate = 48000,\n'
        '  .channels = 2,\n'
        '  .data = nullptr };\n',
        '  .rate     = 48000,\n'
        '  .channels = 2,\n'
        '  .data     = nullptr };\n',
        id='designated_initializers',
    ),
    pytest.param(
        '  { "a", 1 },\n'
        '  { "longer", 222 },\n'
        '  { "z", 3 }\n',
        '  { "a"     , 1   },\n'
        '  { "longer", 222 },\n'
        '  { "z"     , 3   }\n',
        id='literal_table_rows',
    ),
    pytest.param(
        'MethodFill::Shared();\n'
        'x = ns::Make();\n'
        'longer = other::Make();\n',
        'MethodFill::Shared();\n'
        'x      = ns::Make();\n'
        'longer = other::Make();\n',
        id='qualified_names',
    ),
    pytest.param(
        'int a{1};\n'
        '/* keep   this\n'
        ' * block   intact */\n'
        'long longer{22};\n',
        'int a{ 1 };\n'
        '/* keep   this\n'
        ' * block   intact */\n'
        'long longer{ 22 };\n',
        id='comment_block_ends_run',
    ),
    pytest.param(
        'if  (ready)   Invoke("keep   this",  \' \');\n'
        'return   value; // keep   comment\n'
        'auto fn = [ = ](auto& x) { return x; };\n',
        'if (ready) Invoke("keep   this", \' \');\n'
        'return value; // keep   comment\n'
        'auto fn = [ = ](auto& x) { return x; };\n',
        id='stale_unparsed_padding',
    ),
    pytest.param(
        '  GameSession(GameSession const&)                      = delete;\n'
        '  GameSession(GameSession&&)                           = delete;\n'
        '  auto operator=(GameSession const&) -> GameSession&   = delete;\n'
        '  auto operator=(GameSession&&) -> GameSession&        = delete;\n'
        '  ~GameSession()                                       = default;\n',
        '       GameSession(GameSession const&)               = delete;\n'
        '       GameSession(GameSession&&)                    = delete;\n'
        '  auto operator=(GameSession const&) -> GameSession& = delete;\n'
        '  auto operator=(GameSession&&)      -> GameSession& = delete;\n'
        '       ~GameSession()                                = default;\n',
        id='scooby_game_session_specifiers',
    ),
])
def test_review_round_four(source, expected):
    assert align(source) == expected
    assert align(expected) == expected


def test_check_reports_unparsed_stale_padding(tmp_path, capsys):
    path = tmp_path / 'stale.cpp'
    path.write_text('return   value; // keep   comment\n')
    assert run(capsys, '--check', path) == (1, f'{path}\n', '')
    assert align(path.read_text()) == 'return value; // keep   comment\n'


def test_writer_reports_whether_it_wrote(tmp_path):
    path = tmp_path / 'one.cpp'
    original = 'int x{};\n'
    path.write_text(original)
    result = columns.align(original)
    assert columns.write_if_changed(path, original, result)
    assert path.read_text() == 'int x{ };\n'
    assert not columns.write_if_changed(path, path.read_text(), result)


def test_overflow_preserves_multiline_raw_literal():
    expected = 'auto text = R"x(keep   these ' + 'a' * 125 + '\nand   these)x";\n'
    result = columns.align(expected)
    assert result.text == expected
    assert result.exceptions == [columns.Overflow(1, expected.splitlines()[0], len(expected.splitlines()[0]))]
    assert align(result.text) == expected


def test_specifiers_after_noexcept_trailing_return():
    source = 'auto F() noexcept -> bool override;\nauto Longer() -> Ret final;\n'
    expected = 'auto F() noexcept -> bool override;\nauto Longer()     -> Ret  final;\n'
    assert align(source) == expected
    assert align(expected) == expected


ENCODER = '''\
class Encoder {
public:
  Encoder();
  Encoder(Encoder const&) = delete;
  Encoder(Encoder&&)      = delete;
  ~Encoder();
  Encoder& operator = (Encoder const&) = delete;
  Encoder& operator = (Encoder&&)      = delete;
  static bool Available();
  static std::string UnavailableReason();
  bool Open(unsigned width, unsigned height, unsigned bitrate, unsigned fps);
  std::span<BYTE const> Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr, std::vector<BYTE>& encoded);
  void Close();
  bool IsOpen() const;
  bool TooSmall() const;
  std::string const& Error() const;
  EncodingTimes const& Timing() const { return times; }

private:
  EncodingTimes         times;
  struct                Impl;
  std::unique_ptr<Impl> impl;
};
'''

ENCODER_EXPECTED = '''\
class Encoder {
public:
                        Encoder();
                        Encoder(Encoder const&)     = delete;
                        Encoder(Encoder&&)          = delete;
                        ~Encoder();
  Encoder&              operator = (Encoder const&) = delete;
  Encoder&              operator = (Encoder&&)      = delete;
  static bool           Available();
  static std::string    UnavailableReason();
  bool                  Open(unsigned width, unsigned height, unsigned bitrate, unsigned fps);
  std::span<BYTE const> Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr, std::vector<BYTE>& encoded);
  void                  Close();
  bool                  IsOpen() const;
  bool                  TooSmall() const;
  std::string const&    Error() const;
  EncodingTimes const&  Timing() const { return times; }

private:
  EncodingTimes         times;
  struct Impl;
  std::unique_ptr<Impl> impl;
};
'''

DRIVE = '''\
class DriveChannel : public std::enable_shared_from_this<DriveChannel> {
public:
  DriveChannel(DriveChannel const&) = delete;
  DriveChannel(DriveChannel&&)      = delete;
  explicit DriveChannel(Peer& /*value*/);
  ~DriveChannel();
  DriveChannel& operator = (DriveChannel const&) = delete;
  DriveChannel& operator = (DriveChannel&&)      = delete;
  bool Open();
  bool Pump(std::span<HANDLE const> signaled);
  HANDLE Event() const { return event; }
  void Disconnect();
  void Abort(std::string const& /*cause*/);
  int List(driver_drive* /*out*/, unsigned /*max*/);
  unsigned Device(unsigned id);
  std::shared_ptr<DriveRequest> Send(unsigned drive, unsigned file, unsigned major, DrivePacket const& body,
                                     unsigned minor = 0);
  DrivePacket Wait(std::shared_ptr<DriveRequest> const& /*request*/, std::string const& path, bool end = false);
  size_t WaitAny(std::span<Slot const> /*slots*/);
  void Warn(std::string const& /*cause*/) const;

'''

DRIVE_EXPECTED = '''\
class DriveChannel : public std::enable_shared_from_this<DriveChannel> {
public:
                                DriveChannel(DriveChannel const&) = delete;
                                DriveChannel(DriveChannel&&)      = delete;
  explicit                      DriveChannel(Peer& /*value*/);
                                ~DriveChannel();
  DriveChannel&                 operator = (DriveChannel const&)  = delete;
  DriveChannel&                 operator = (DriveChannel&&)       = delete;
  bool                          Open();
  bool                          Pump(std::span<HANDLE const> signaled);
  HANDLE                        Event() const { return event; }
  void                          Disconnect();
  void                          Abort(std::string const& /*cause*/);
  int                           List(driver_drive* /*out*/, unsigned /*max*/);
  unsigned                      Device(unsigned id);
  std::shared_ptr<DriveRequest> Send(unsigned drive, unsigned file, unsigned major, DrivePacket const& body,
                                     unsigned minor = 0);
  DrivePacket Wait(std::shared_ptr<DriveRequest> const& /*request*/, std::string const& path, bool end = false);
  size_t                        WaitAny(std::span<Slot const> /*slots*/);
  void                          Warn(std::string const& /*cause*/) const;
'''

AUDIO = '''\
  sound->num_server_formats = 2;
  // mstsc plays 48 kHz at its 44.1 kHz device rate (measured 2026-09-23).
  sound->server_formats[0]           = { .wFormatTag      = WAVE_FORMAT_PCM,
                                         .nChannels       = 2,
                                         .nSamplesPerSec  = 44100,
                                         .nAvgBytesPerSec = 176400,
                                         .nBlockAlign     = 4,
                                         .wBitsPerSample  = 16,
                                         .cbSize          = 0,
                                         .data            = nullptr };
  sound->server_formats[1]           = { .wFormatTag      = WAVE_FORMAT_PCM,
                                         .nChannels       = 2,
                                         .nSamplesPerSec  = 48000,
                                         .nAvgBytesPerSec = 192000,
                                         .nBlockAlign     = 4,
                                         .wBitsPerSample  = 16,
                                         .cbSize          = 0,
                                         .data            = nullptr };
  sound->src_format                  = &sound->server_formats[0];
  sound->data                        = this;
  sound->rdpcontext                  = context;
  sound->use_dynamic_virtual_channel = FALSE;
  sound->latency                     = 10;
'''

AUDIO_EXPECTED = '''\
  sound->num_server_formats = 2;
  // mstsc plays 48 kHz at its 44.1 kHz device rate (measured 2026-09-23).
  sound->server_formats[0]           = { .wFormatTag      = WAVE_FORMAT_PCM,
                                         .nChannels       = 2,
                                         .nSamplesPerSec  = 44100,
                                         .nAvgBytesPerSec = 176400,
                                         .nBlockAlign     = 4,
                                         .wBitsPerSample  = 16,
                                         .cbSize          = 0,
                                         .data            = nullptr };
  sound->server_formats[1]           = { .wFormatTag      = WAVE_FORMAT_PCM,
                                         .nChannels       = 2,
                                         .nSamplesPerSec  = 48000,
                                         .nAvgBytesPerSec = 192000,
                                         .nBlockAlign     = 4,
                                         .wBitsPerSample  = 16,
                                         .cbSize          = 0,
                                         .data            = nullptr };
  sound->src_format                  = &sound->server_formats[0];
  sound->data                        = this;
  sound->rdpcontext                  = context;
  sound->use_dynamic_virtual_channel = FALSE;
  sound->latency                     = 10;
'''

DRIVENAME = '''\
    bool const wide = drive_version >= DRIVE_CAPABILITY_VERSION_02 && bytes.size() >= 2 && bytes.size() % 2 == 0 &&
                      bytes[bytes.size() - 2] == 0;
    auto format = wide ? oxbox::utilities::TextFormat{ .encoding = oxbox::utilities::Encoding::UTF16,
                                                       .order    = std::endian::little }
                       : oxbox::utilities::TextFormat{};
    auto label  = TranscodeRange<std::string>(std::as_bytes(bytes.first(bytes.size() - (wide ? 2 : 1))), format, {});
'''

DRIVENAME_EXPECTED = '''\
    bool const wide   = drive_version >= DRIVE_CAPABILITY_VERSION_02 && bytes.size() >= 2 && bytes.size() % 2 == 0 &&
                        bytes[bytes.size() - 2] == 0;
    auto       format = wide ? oxbox::utilities::TextFormat{ .encoding = oxbox::utilities::Encoding::UTF16,
                                                             .order    = std::endian::little }
                             : oxbox::utilities::TextFormat{ };
    auto label = TranscodeRange<std::string>(std::as_bytes(bytes.first(bytes.size() - (wide ? 2 : 1))), format, { });
'''

INPUT = '''\
    self.owner.Push({ .type = DRIVER_KEY,
                      .key  = { .scancode = code,
                                .extended = !!(flags & KBD_FLAGS_EXTENDED),
                                .down     = !(flags & KBD_FLAGS_RELEASE) } });
'''

INPUT_EXPECTED = '''\
    self.owner.Push({ .type = DRIVER_KEY,
                      .key  = { .scancode = code,
                                .extended = !!(flags & KBD_FLAGS_EXTENDED),
                                .down     = !(flags & KBD_FLAGS_RELEASE) } });
'''

AUTH = '''\
  PlainPassword const plain{ password };
  bool const accepted = config.verify ? config.verify(config.auth_user, domain, user, plain.Text()) != 0
                                      : driver_verify_pair(&config, domain, user, plain.Text()) != 0;
'''

AUTH_EXPECTED = '''\
  PlainPassword const plain    { password };
  bool const          accepted = config.verify ? config.verify(config.auth_user, domain, user, plain.Text()) != 0
                                               : driver_verify_pair(&config, domain, user, plain.Text()) != 0;
'''

LOCALS = '''\
inline std::vector<BYTE> SoundFormatReply(SoundCapture const& capture) {
  auto supported = SupportedSoundFormats(capture);
  std::vector<BYTE> bytes(24 + (supported.size() * 18));
  wStream output{};
  auto* out = Stream_StaticInit(&output, bytes.data(), bytes.size());
'''

LOCALS_EXPECTED = '''\
inline std::vector<BYTE> SoundFormatReply(SoundCapture const& capture) {
  auto              supported = SupportedSoundFormats(capture);
  std::vector<BYTE> bytes(24 + (supported.size() * 18));
  wStream           output    { };
  auto*             out       = Stream_StaticInit(&output, bytes.data(), bytes.size());
'''

BRACEHEAD = '''\
    auto confirmation = capture.pending[index];
    std::array<BYTE, 8> bytes{
      5, 0, 4, 0, BYTE(confirmation.timestamp), BYTE(confirmation.timestamp >> 8), confirmation.block, 0
    };
'''

BRACEHEAD_EXPECTED = '''\
    auto                confirmation = capture.pending[index];
    std::array<BYTE, 8> bytes        {
      5, 0, 4, 0, BYTE(confirmation.timestamp), BYTE(confirmation.timestamp >> 8), confirmation.block, 0
    };
'''

LAMBDA = '''\
  static std::string const reason = [] {
    Impl probe;
    auto available = probe.Load();
    auto error     = probe.error;
    probe.Close();
    return available ? std::string{} : error;
  }();
'''

LAMBDA_EXPECTED = '''\
  static std::string const reason = [] {
    Impl probe;
    auto available = probe.Load();
    auto error     = probe.error;
    probe.Close();
    return available ? std::string{ } : error;
  }();
'''

LAMBDA2 = '''\
  auto* file     = held_file;
  auto& observer = *this->observer;
  auto stat      = std::async(std::launch::async, [&] {
    driver_stat info{};
    auto result = driver_drive_fstat(handle.get(), file, &info);
    return std::pair(result, std::string(driver_last_error()));
  });
  ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 1; }));
'''

LAMBDA2_EXPECTED = '''\
  auto* file     = held_file;
  auto& observer = *this->observer;
  auto  stat     = std::async(std::launch::async, [&] {
    driver_stat info   { };
    auto        result = driver_drive_fstat(handle.get(), file, &info);
    return std::pair(result, std::string(driver_last_error()));
  });
  ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 1; }));
'''

DAMAGE = '''\
    auto bytes = client.Received();
    std::array<driver_rect, 2> damage{ { { .x = 0, .y = 0, .w = 8, .h = 8 },
                                         { .x = 1016, .y = 760, .w = 8, .h = 8 } } };
'''

DAMAGE_EXPECTED = '''\
    auto                       bytes  = client.Received();
    std::array<driver_rect, 2> damage { { { .x = 0   , .y = 0  , .w = 8, .h = 8 },
                                          { .x = 1016, .y = 760, .w = 8, .h = 8 } } };
'''

STATE = '''\
  static BOOL Keyboard(rdpInput* input, UINT16 flags, UINT8 code);
  static BOOL Mouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  static BOOL ExtendedMouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  void RecordAcknowledgements(std::deque<Pending>::iterator const& last, Clock::time_point now);
  enum class EncodeState { Idle, Legacy, Graphics, LegacyReady };
  void TransitionEncode(EncodeState next);
  bool PrepareFrame();
  EncodeState encode_state{ EncodeState::Idle };
  LegacyFrame legacy      {};
'''

STATE_EXPECTED = '''\
  static BOOL Keyboard(rdpInput* input, UINT16 flags, UINT8 code);
  static BOOL Mouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  static BOOL ExtendedMouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  void        RecordAcknowledgements(std::deque<Pending>::iterator const& last, Clock::time_point now);
  enum class EncodeState{ Idle, Legacy, Graphics, LegacyReady };
  void        TransitionEncode(EncodeState next);
  bool        PrepareFrame();
  EncodeState encode_state{ EncodeState::Idle };
  LegacyFrame legacy      { };
'''


@pytest.mark.parametrize(('source', 'expected'), [
    pytest.param(ENCODER, ENCODER_EXPECTED, id='item2_encoder_inline_body_tail'),
    pytest.param(DRIVE, DRIVE_EXPECTED, id='item2_drive_channel_inline_body_tail'),
    pytest.param(AUDIO, AUDIO_EXPECTED, id='item3_audio_continuations'),
    pytest.param(DRIVENAME, DRIVENAME_EXPECTED, id='item3_drive_name_continuations'),
    pytest.param(INPUT, INPUT_EXPECTED, id='item3_input_continuations'),
    pytest.param(AUTH, AUTH_EXPECTED, id='item3_auth_continuations'),
    pytest.param(LOCALS, LOCALS_EXPECTED, id='item5_paren_initialised_locals'),
    pytest.param(BRACEHEAD, BRACEHEAD_EXPECTED, id='item6_multiline_brace_head'),
    pytest.param(LAMBDA, LAMBDA_EXPECTED, id='item6_lambda_body'),
    pytest.param(LAMBDA2, LAMBDA2_EXPECTED, id='item6_lambda_body_nested'),
    pytest.param(DAMAGE, DAMAGE_EXPECTED, id='item6_damage_run'),
    pytest.param(STATE, STATE_EXPECTED, id='item7_state_kinds'),
    pytest.param(
        '       GameSession(std::filesystem::path const& assets,\n'
        '                   cartridge::Scenario scenario, std::optional<int> room,\n'
        '                   std::optional<GameState> const& state = std::nullopt);\n'
        '       GameSession(GameSession const&) = delete;\n'
        '       GameSession(GameSession&&) = delete;\n'
        '  auto operator=(GameSession const&) -> GameSession& = delete;\n'
        '  auto operator=(GameSession&&) -> GameSession& = delete;\n'
        '       ~GameSession() = default;\n',
        '       GameSession(std::filesystem::path const& assets,\n'
        '                   cartridge::Scenario scenario, std::optional<int> room,\n'
        '                   std::optional<GameState> const& state = std::nullopt);\n'
        '       GameSession(GameSession const&)               = delete;\n'
        '       GameSession(GameSession&&)                    = delete;\n'
        '  auto operator=(GameSession const&) -> GameSession& = delete;\n'
        '  auto operator=(GameSession&&)      -> GameSession& = delete;\n'
        '       ~GameSession()                                = default;\n',
        id='item1_specifier_column_ignores_long_constructor',
    ),
    pytest.param(
        '  Machine(Machine const&) = delete;\n'
        '  Machine(Machine&&) noexcept = default;\n'
        '  auto operator=(Machine const&) -> Machine& = delete;\n'
        '  auto operator=(Machine&&) noexcept -> Machine& = delete;\n'
        '  ~Machine() = default;\n',
        '       Machine(Machine const&)                   = delete;\n'
        '       Machine(Machine&&) noexcept               = default;\n'
        '  auto operator=(Machine const&)     -> Machine& = delete;\n'
        '  auto operator=(Machine&&) noexcept -> Machine& = delete;\n'
        '       ~Machine()                                = default;\n',
        id='item1_noexcept_default_in_delete_column',
    ),
    pytest.param(
        'inline constexpr UINT32 CompatibleRate = 44100;\nclass Host;\ninline constexpr UINT32 NativeRate = 48000;\n'
        'class Peer;\nenum class RowOrder{ TopDown, BottomUp };\nclass Scaler {\n',
        'inline constexpr UINT32 CompatibleRate = 44100;\nclass Host;\n'
        'inline constexpr UINT32 NativeRate     = 48000;\n'
        'class Peer;\nenum class RowOrder{ TopDown, BottomUp };\nclass Scaler {\n',
        id='item7_forward_declaration_among_constants',
    ),
    pytest.param(
        '  using Clock = std::chrono::steady_clock;\n  AudioChannel(AudioChannel const&) = delete;\n'
        '  explicit AudioChannel(Peer& owner);\n  using Handle = void*;\n  bool Initialize();\n',
        '  using Clock  = std::chrono::steady_clock;\n           AudioChannel(AudioChannel const&) = delete;\n'
        '  explicit AudioChannel(Peer& owner);\n  using Handle = void*;\n  bool     Initialize();\n',
        id='item7_alias_among_functions',
    ),
    pytest.param(
        '#define  PADDED   1\nint a{1};\n#define  WIDE(x) \\\n    int    x\nlong longer{22};\n',
        '#define  PADDED   1\nint a{ 1 };\n#define  WIDE(x) \\\n    int    x\nlong longer{ 22 };\n',
        id='item4_preprocessor_lines_untouched',
    ),
])
def test_review_round_five(source, expected):
    assert align(source) == expected
    assert align(expected) == expected


@pytest.mark.parametrize(('source', 'excluded'), [
    pytest.param(DRIVE, 130, id='item2_drive_wait'),
    pytest.param(DRIVENAME, 124, id='item3_drive_name_label'),
])
def test_round_five_overflow_widths(source, excluded):
    assert [overflow.width for overflow in columns.align(source).exceptions] == [excluded]


def test_overflow_setter_is_excluded_and_others_stay_aligned():
    long_line = 'int count = ' + 'x' * 80 + ';'
    source    = f'std::unordered_map<std::string, std::vector<int>> table;\n{long_line}\nint b = 2;\n'
    result    = columns.align(source)
    assert result.text.splitlines()[1:] == [long_line, 'int' + ' ' * 47 + 'b     = 2;']
    assert result.exceptions == [columns.Overflow(2, long_line, 139)]


def test_column_setter_within_limit_is_excluded_when_it_pushes_the_run_over():
    wide   = '  auto Wait(' + 'x' * 90 + ') -> bool;'
    source = f'  auto operator = (Channel const&) -> Channel& = delete;\n{wide}\n  auto Open() -> bool;\n'
    result = columns.align(source)
    assert result.text.splitlines() == ['  auto operator = (Channel const&) -> Channel& = delete;', wide,
                                        '  auto Open()                      -> bool;']
    assert result.exceptions == [columns.ExcludedForColumns(2, wide, len(wide))]


def test_one_line_body_counts_toward_the_limit():
    lines  = ['auto Unlock(Mutex& mutex) noexcept -> void { Release(mutex); }',
              'auto Observe(' + 'x' * 85 + ') -> Registration;']
    result = columns.align('\n'.join(lines) + '\n')
    assert result.text.splitlines() == lines
    assert result.exceptions == [columns.ExcludedForColumns(2, lines[1], len(lines[1]))]


@pytest.mark.parametrize('operator', ['==', '!=', '<='])
def test_comparison_at_continuation_end(operator):
    source = f'return b && d &&\n    x * d {operator}\n    y * b;\n'
    assert align(source) == source


@pytest.mark.parametrize('body_start', ['\n  ', ' '])
def test_return_comparison_in_function(body_start):
    source = 'bool Equal() {' + body_start + 'return x * d ==\n      y * b;\n}\n'
    assert align(source) == source


@pytest.mark.parametrize('value', ['socket', '0'])
def test_call_assignment_keeps_statement_indent(value):
    source = 'void Set() {\n  Slot(bio) = ' + value + ';\n}\n'
    assert align(source) == source


def test_empty_braces_ignore_peer_value_width():
    source = 'Thing first{ some_long_value };\nThing other{ };\n'
    assert align(source) == source


def collapsed_outside_literals(text):
    pieces, start = [], 0
    for match in re.finditer(columns.PROTECTED.pattern.split('|//', 1)[0], text, re.S):
        pieces.append(''.join(text[start:match.start()].split()))
        pieces.append(match[0])
        start = match.end()
    return ''.join(pieces) + ''.join(text[start:].split())


def test_sources_preserve_non_whitespace_text():
    root = pathlib.Path(__file__).resolve().parents[2]
    paths = columns.source_files([root / 'sources'])
    assert paths, 'source files must be available to the gate'
    for path in paths:
        before = path.read_text()
        assert collapsed_outside_literals(before) == collapsed_outside_literals(align(before)), str(path)


@pytest.mark.parametrize('operator', ['==', '!=', '<=', '>=', '+=', '-=', '*=', '/=', '%=',
                                     '&=', '|=', '^=', '<<=', '>>=', '<=>'])
def test_operator_is_not_a_declaration_equals(operator):
    source = f'x * d {operator};'
    assert columns.parse_declaration(source, columns.mask(source)) is None


@pytest.mark.parametrize('head', ['return b && d &&', 'Consume(', 'return'])
def test_unclosed_expression_cannot_start_declaration(head):
    source = f'{head}\n    x * d =\n    y * b);\n'
    assert align(source) == source


@pytest.mark.parametrize('literal', ['"a  b"', "' '", 'R"tag(a  b)tag"'])
def test_token_invariant_preserves_literal_whitespace(literal):
    assert collapsed_outside_literals('  ' + literal + '  ') == literal


def test_scope_head_is_not_an_expression_continuation():
    source = 'class Peer\n{\n  int a;\n  long b;\n};\n'
    expected = 'class Peer\n{\n  int  a;\n  long b;\n};\n'
    assert align(source) == expected


def test_inline_body_past_the_limit_leaves_the_run():
    head      = 'class Session {\npublic:\n  [[nodiscard]] SessionLock Lock();\n'
    signature = '  void ForEach(PeersLock const& held, std::invocable<Peer&> auto visit)'
    wide      = signature + ' { _peers.ForEach(held, visit); }\n'
    source    = head + wide + '  void Reap();\n};\n'
    assert align(source) == head + wide + '  void                      Reap();\n};\n'


def test_qualified_class_keeps_its_constructor_indent():
    before = 'void Install() {\n  Hook();\n}\n'
    source = before + 'class Hook::Installation {\npublic:\n  Installation(int x);\n  ~Installation();\n};\n'
    assert align(source) == source


def test_qualified_class_constructor_without_destructor_is_a_member():
    source = 'class Hook::Installation {\npublic:\n  Installation(int x);\n  auto Run() -> bool;\n};\n'
    expected = 'class Hook::Installation {\npublic:\n       Installation(int x);\n  auto Run() -> bool;\n};\n'
    assert align(source) == expected


def test_template_class_constructor_keeps_its_indent_on_a_second_pass():
    source = ('template <class P> class Channel final : public Base {\npublic:\n  Channel(Link& link) noexcept;\n'
              '  auto Open() -> bool;\n  auto Activate() -> bool override;\n};\n')
    once = align(source)
    assert once.startswith('template <class P> class Channel final : public Base {\npublic:\n       Channel(')
    assert align(once) == once


def test_default_joins_the_case_run():
    cases  = 'switch (phase) {\ncase Phase::DOWN: return 1;\n'
    source = cases + 'case Phase::UP: return 2;\ndefault: return 0;\n}\n'
    assert align(source) == cases + 'case Phase::UP:   return 2;\ndefault:          return 0;\n}\n'


def test_qualified_declarations_align_and_definitions_do_not():
    source = ('template <> auto P::Open(Link& link) -> Context;\n'
              'template <> auto P::Service(Context const& context) -> bool;\n'
              'auto P::Reject() -> void { }\n'
              'auto P::Layout(Pdu const& pdu) -> int {\n')
    assert align(source) == ('template <> auto P::Open(Link& link)                -> Context;\n'
                             'template <> auto P::Service(Context const& context) -> bool;\n'
                             'auto P::Reject() -> void { }\n'
                             'auto P::Layout(Pdu const& pdu) -> int {\n')


CALLS_IN_BODIES = '''auto GeneralCapability(DrivePacket& packet) -> void {
  DrivePacket body;
  body.Write(std::uint32_t{ 0 });
  Capability(packet, CapabilityType::General, CapabilityVersion::V2, body);
}
auto DriveCapability(DrivePacket& packet) -> void {
  Capability(packet, CapabilityType::Drive, CapabilityVersion::V2, { });
}
'''


@pytest.mark.parametrize('source', [CALLS_IN_BODIES, CALLS_IN_BODIES.replace('CapabilityType::', 'Capability::')],
                         ids=['scope_prefixed_by_the_name', 'scope_named_by_the_name'])
def test_a_call_in_a_body_is_never_grouped_with_the_function_heads(source):
    assert align(source) == source


def test_a_group_never_spans_a_change_in_brace_depth():
    physical = columns.physical_lines('int a{ 1 };\nint bb{ 22 };\n')
    logicals = columns.logical_streams(columns.line_segments(physical, {}), {})[None]
    items    = columns.stream_items(physical, None, logicals, set())
    assert columns.stream_groups(logicals, items, [0, 0]) == [[0, 1]]
    assert columns.stream_groups(logicals, items, [0, 1]) == [[0], [1]]


def test_a_call_is_a_function_declaration_only_when_its_name_recurs_as_a_word():
    def call(signature):
        fields = columns.FunctionFields._make(dict.fromkeys(columns.FunctionFields._fields, '').values())
        return fields._replace(signature=signature)
    assert not columns.is_function_declaration(call('Capability(packet, CapabilityType::General)'), set())
    assert columns.is_function_declaration(call('Capability(Capability const& other)'), set())
    assert not columns.is_function_declaration(call('Base::operator[](index)'), set())
