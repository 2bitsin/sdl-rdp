"""Exercise column alignment and preservation of C and C++ source."""
import importlib.util
import pathlib
import subprocess
import sys

SPEC = importlib.util.spec_from_file_location('align_columns', pathlib.Path(__file__).with_name('align-columns.py'))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
align = MODULE.align


def test_declarations():
    assert align('int a{1};\nlong longer{22};\n') == 'int  a      { 1  };\nlong longer { 22 };\n'
    assert align('int x{};\nint y{  };\n') == 'int x { };\nint y { };\n'
    assert align('int x;\nlong longer = 2;\n') == 'int  x;\nlong longer = 2;\n'
    assert align('static constexpr std::vector<int> a{1};\nvolatile int* b{2};\nint c[2]{3};\n') == (
        'static constexpr std::vector<int> a    { 1 };\n'
        'volatile int*                     b    { 2 };\n'
        'int                               c[2] { 3 };\n')


def test_other_kinds():
    assert align('x = 1;\nlonger += 2;\n') == 'x      =  1;\nlonger += 2;\n'
    assert align(': x{1},\n, longer{22}\n') == ': x      { 1  },\n, longer { 22 }\n'
    assert align('case A: f();\ncase LONG: g();\n') == 'case A:    f();\ncase LONG: g();\n'
    assert align('A = 1,\nLONG = 2,\nEND,\n') == 'A    = 1,\nLONG = 2,\nEND,\n'


def test_split():
    assert align('int a = 0, b = 1;\n') == 'int a = 0;\nint b = 1;\n'
    assert align('Region dirty, sending;\n') == 'Region dirty;\nRegion sending;\n'
    assert align('std::pair<int,int> a{1,2}, b{};\n') == 'std::pair<int,int> a { 1,2 };\nstd::pair<int,int> b {     };\n'


def test_comments():
    assert align('int a{1}; // a\nint longer{22}; // b\n') == 'int a      { 1  };  // a\nint longer { 22 };  // b\n'
    assert align('int a{1};\n// stays\nint longer{22};\n') == 'int a      { 1  };\n// stays\nint longer { 22 };\n'


def test_boundaries():
    assert align('int a{1};\nprivate:\nint longer{22};\n') == 'int a { 1 };\nprivate:\nint longer { 22 };\n'
    assert align('int a{1};\n\nint longer{22};\n') == 'int a { 1 };\n\nint longer { 22 };\n'


def test_protected():
    source = '#define X int a{}; \\\nint b{};\n/* int a{};\nint b{}; */\n'
    assert align(source) == source
    source = 'auto s = "a = {  }";\nauto c = \'{\';\nauto r = R"xx(a = {  })xx";\n'
    assert align(source) == source
    source = 'auto s = R"xx(\nint a{};\n)xx";\n'
    assert align(source) == source


def test_limit():
    line = 'int enormous = ' + 'x' * 110 + ';\n'
    stats = {}
    assert align('int a{1};\n' + line + 'int bb{22};\n', stats) == 'int a  { 1  };\n' + line + 'int bb { 22 };\n'
    assert len(stats['exceptions']) == 1


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


def test_declarator_types_and_scopes():
    assert align('int* a, b; // end\n') == 'int* a;\nint  b;  // end\n'
    assert align('int a, *b;\n') == 'int   a;\nint * b;\n'
    source = 'class Child : public Base, public Other {};\n'
    assert align(source) == source
    assert align('int a, b; // end\n') == 'int a;\nint b;  // end\n'


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


def test_inline_control_statements():
    source = '  if (index >= 0 && values[index]) return values[index];\n'
    assert align(source) == source
    assert align('  if (ready) x = 1;\n') == '  if (ready) x = 1;\n'
    assert align('call(value = 1);\nlonger = 2;\n') == 'call(value = 1);\nlonger = 2;\n'
