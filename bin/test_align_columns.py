"""Exercise column alignment and preservation of C and C++ source."""
import importlib.util
import pathlib
import subprocess
import sys

import pytest

SPEC = importlib.util.spec_from_file_location('align_columns', pathlib.Path(__file__).with_name('align-columns.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
align = MODULE.align


@pytest.mark.parametrize(('source', 'expected'), [
    pytest.param(
        'int a{1};\n'
        'long longer{22};\n',
        'int  a      { 1  };\n'
        'long longer { 22 };\n',
        id='declaration_value_and_closer_columns',
    ),
    pytest.param(
        'int x{};\n'
        'int y{  };\n',
        'int x { };\n'
        'int y { };\n',
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
        'static constexpr std::vector<int> a    { 1 };\n'
        'volatile int*                     b    { 2 };\n'
        'int                               c[2] { 3 };\n',
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
        ': x      { 1  },\n'
        ', longer { 22 }\n',
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
        'std::pair<int,int> a { 1,2 };\n'
        'std::pair<int,int> b {     };\n',
        id='top_level_commas_only',
    ),
    pytest.param(
        'int a{1}; // a\n'
        'int longer{22}; // b\n',
        'int a      { 1  };  // a\n'
        'int longer { 22 };  // b\n',
        id='trailing_comment_column',
    ),
    pytest.param(
        'int a{1};\n'
        '// stays\n'
        'int longer{22};\n',
        'int a      { 1  };\n'
        '// stays\n'
        'int longer { 22 };\n',
        id='comment_only_continues_group',
    ),
    pytest.param(
        'int a{1};\n'
        'private:\n'
        'int longer{22};\n',
        'int a { 1 };\n'
        'private:\n'
        'int longer { 22 };\n',
        id='access_specifier_ends_group',
    ),
    pytest.param(
        'int a{1};\n'
        '\n'
        'int longer{22};\n',
        'int a { 1 };\n'
        '\n'
        'int longer { 22 };\n',
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
        'class Child : public Base, public Other {};\n',
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
    stats = {}
    assert align('int a{1};\n' + line + 'int bb{22};\n', stats) == 'int a  { 1  };\n' + line + 'int bb { 22 };\n'
    assert stats == {'groups': 1, 'exceptions': [(2, line.rstrip())]}


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


def test_peer_idempotence():
    formatted = align(PEER)
    assert align(formatted) == formatted
    assert 'Region' in formatted
    assert 'snapshot_height' in formatted
    assert '{}' not in formatted


def test_check_lists_every_changed_file(tmp_path):
    paths = [tmp_path / 'one.c', tmp_path / 'two.hpp']
    for path in paths:
        path.write_text('int a{};\n')
    command = [sys.executable, str(pathlib.Path(MODULE.__file__)), '--check', str(tmp_path)]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 1
    assert set(result.stdout.splitlines()) == set(map(str, paths))
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
    result = subprocess.run([sys.executable, str(tool), '--check', str(tmp_path)], capture_output=True, text=True)
    assert result.returncode == 1
    assert f'{target}:' in result.stdout
    assert '121 columns > 120' in result.stdout
