#include <sdl-rdp-backend.so/_detail/headless-client.hpp>
#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>
#include <oxbox/platform/scratch-area.hpp>
#include <gtest/gtest.h>
#include <freerdp/input.h>
#include <freerdp/update.h>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <SDL3/SDL.h>
#include <sstream>
#include <spawn.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace {
using Headless::Client;
using Headless::Clock;
using utilities::Expects;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

fs::path BuildRoot() {
  auto path = std::getenv("PATH");
  Expects(path != nullptr, "ctest supplies PATH");
  for (auto part : std::string_view(path) | std::views::split(':')) {
    auto directory = fs::path(std::string_view(part));
    if (fs::is_regular_file(directory / "sdl-rdp-sample")) return directory.parent_path();
  }
  Expects(false, "built sample exists on ctest PATH");
  return {};
}

pid_t Spawn(std::vector<std::string> arguments, int& output) {
  Expects(!arguments.empty(), "child arguments supplied");
  int descriptors[2];
  Expects(pipe2(descriptors, O_CLOEXEC) == 0, "stdout pipe created");
  output = descriptors[0];
  pid_t pid = -1;
  posix_spawn_file_actions_t actions;
  Expects(posix_spawn_file_actions_init(&actions) == 0, "spawn actions initialized");
  Expects(posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO) == 0,
          "child stdout redirected");
  Expects(posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO) == 0, "child stdout redirected");
  std::vector<char*> argv;
  std::ranges::transform(arguments, std::back_inserter(argv), [](auto& s) { return s.data(); });
  argv.push_back(nullptr);
  auto result = posix_spawn(&pid, "/usr/bin/env", &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(descriptors[1]);
  Expects(result == 0, "sample spawned");
  return pid;
}

class Process {
  int output = -1;
  pid_t pid = -1;
  std::string pending;
public:
  std::string transcript;
  explicit Process(std::vector<std::string> arguments) : pid(Spawn(std::move(arguments), output)) {}
  Process(Process const&) = delete;
  ~Process() {
    if (pid > 0) { kill(pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }
    close(output);
  }
  bool Line(std::string& line, Clock::time_point deadline) {
    Expects(output >= 0, "stdout pipe open");
    for (;;) {
      if (auto end = pending.find('\n'); end != std::string::npos) {
        line = pending.substr(0, end);
        pending.erase(0, end + 1);
        if (line.starts_with("INFO: ")) line.erase(0, 6);
        return true;
      }
      auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
      if (left <= 0) return false;
      pollfd descriptor{output, POLLIN, 0};
      if (poll(&descriptor, 1, int(left)) <= 0) return false;
      std::array<char, 4096> buffer;
      auto count = read(output, buffer.data(), buffer.size());
      if (count <= 0) return false;
      pending.append(buffer.data(), count); transcript.append(buffer.data(), count);
    }
  }
  bool Exit() {
    Expects(pid > 0, "sample has not been reaped");
    auto deadline = Clock::now() + 2s;
    int status = 0;
    while (Clock::now() < deadline) {
      if (waitpid(pid, &status, WNOHANG) == pid) { pid = -1; return WIFEXITED(status) && WEXITSTATUS(status) == 0; }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }
};

std::vector<std::string> Arguments(fs::path const& certificates, bool wait) {
  Expects(fs::is_directory(certificates), "certificate directory exists");
  auto root = BuildRoot();
  auto backend = root / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
  Expects(fs::is_regular_file(backend), "built backend exists");
  return {"env", "SDL_VIDEO_DRIVER=rdp", "SDL_RDP_PORT=0", "SDL_RDP_BIND=127.0.0.1",
    "SDL_RDP_CERT_DIR=" + certificates.string(), "SDL_RDP_BACKEND=" + backend.string(),
    "SDL_RDP_CODEC=planar", "SDL_RDP_WAIT_FOR_CLIENT=" + std::to_string(wait), (root / "bin/sdl-rdp-sample").string()};
}

unsigned Number(std::string_view text, int base = 10) {
  Expects(base >= 2 && base <= 36, "valid integer base");
  unsigned value = 0;
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}

pid_t ProcId() {
  // procfs can belong to an outer PID namespace; its children file uses that namespace.
  std::ifstream children("/proc/thread-self/children");
  pid_t child = 0;
  Expects(bool(children >> child), "spawned child visible in procfs");
  return child;
}

unsigned ListeningPort() {
  auto pid = ProcId();
  std::vector<std::string> sockets;
  for (auto const& entry : fs::directory_iterator("/proc/" + std::to_string(pid) + "/fd")) {
    std::error_code error;
    auto target = fs::read_symlink(entry.path(), error).string();
    if (!error && target.starts_with("socket:[")) sockets.push_back(target);
  }
  std::ifstream tcp("/proc/net/tcp");
  std::string line;
  while (std::getline(tcp, line)) {
    std::istringstream fields(line);
    std::array<std::string, 10> values;
    std::ranges::for_each(values, [&](auto& value) { fields >> value; });
    if (values[3] == "0A" && std::ranges::contains(sockets, "socket:[" + values[9] + "]"))
      return Number(std::string_view(values[1]).substr(9), 16);
  }
  return 0;
}

testing::AssertionResult Pattern(Client& client, bool pointer) {
  auto gdi = client.instance->context->gdi;
  if (!gdi || gdi->width != 640 || gdi->height != 480) return testing::AssertionFailure() << "framebuffer is not 640x480";
  auto pixel = [&](int index) {
    UINT32 value;
    std::memcpy(&value, gdi->primary_buffer + (index / 640) * gdi->stride + (index % 640) * 4, 4);
    return value & 0xffffff;
  };
  auto columns = std::views::iota(0, 640);
  auto first = std::ranges::find_if(columns, [&](int x) { return pixel(40 * 640 + x) == 0x00ff00; });
  if (first == columns.end() || *first > 608) return testing::AssertionFailure() << "no complete green block on row 40";
  auto expected = [&](int index) {
    int x = index % 640, y = index / 640;
    auto expected = x >= *first && x < *first + 32 && y >= 40 && y < 72 ? 0x00ff00u : 0x010101u;
    return expected;
  };
  auto indices = std::views::iota(0, 640 * 480);
  auto mismatch = std::ranges::find_if(indices, [&](int i) { return pixel(i) != expected(i); });
  if (mismatch == indices.end()) return testing::AssertionSuccess();
  return testing::AssertionFailure() << "pixel (" << *mismatch % 640 << "," << *mismatch / 640
    << ") actual=" << std::hex << pixel(*mismatch) << " expected=" << expected(*mismatch);
}

class NextFrame {
  inline static thread_local NextFrame* active = nullptr;
  Client& client;
  unsigned column;
  pSurfaceBits original;
  pBitmapUpdate original_bitmap;
public:
  bool received = false;
  testing::AssertionResult matches = testing::AssertionFailure() << "no complete frame";
  explicit NextFrame(Client& value, unsigned frame) : client(value), column(frame % 640), original(value.instance->context->update->SurfaceBits),
      original_bitmap(value.instance->context->update->BitmapUpdate) {
    Expects(!active && original && original_bitmap, "one frame observer with GDI installed");
    active = this;
    client.instance->context->update->SurfaceBits = Receive;
    client.instance->context->update->BitmapUpdate = ReceiveBitmap;
  }
  ~NextFrame() {
    client.instance->context->update->SurfaceBits = original;
    client.instance->context->update->BitmapUpdate = original_bitmap;
    active = nullptr;
  }
  static BOOL Receive(rdpContext* context, SURFACE_BITS_COMMAND const* command) {
    Expects(active && command, "frame observer and surface command exist");
    auto result = active->original(context, command);
    if (result && command->destBottom == 480) active->Observe(context);
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command) {
    Expects(active && command, "frame observer and bitmap command exist");
    auto result = active->original_bitmap(context, command);
    // Bitmap update corners are inclusive; surface command corners are exclusive.
    if (result && std::ranges::any_of(std::span(command->rectangles, command->number),
        [](auto const& rectangle) { return rectangle.destBottom == 479; })) active->Observe(context);
    return result;
  }
  void Observe(rdpContext* context) {
    Expects(context && context->gdi, "decoded framebuffer exists");
    auto gdi = context->gdi;
    UINT32 pixel = 0;
    std::memcpy(&pixel, gdi->primary_buffer + 40 * gdi->stride + column * 4, 4);
    UINT32 before = 0;
    if (column) std::memcpy(&before, gdi->primary_buffer + 40 * gdi->stride + (column - 1) * 4, 4);
    bool origin = (pixel & 0xffffff) == 0x00ff00 && (before & 0xffffff) != 0x00ff00;
    if (!received && origin) {
      matches = Pattern(client, true);
      received = true;
    }
  }
};

class Sample : public testing::Test {
protected:
  oxbox::platform::ScratchArea certificates{"certificates", "sdl-rdp"};
  std::unique_ptr<Process> process;
  std::string line;
  bool Read(std::string_view expected, std::chrono::milliseconds timeout = 2s) {
    Expects(process && !expected.empty() && timeout > 0ms, "running sample, expected line and deadline supplied");
    auto deadline = Clock::now() + timeout;
    while (process->Line(line, deadline)) if (line.starts_with(expected)) return true;
    return false;
  }
  void Exposed() {
    ASSERT_TRUE(Read("event EXPOSED ")) << "EXPOSED missing: " << process->transcript;
    auto name = line.find(" client_name=");
    ASSERT_NE(name, std::string::npos) << line;
    ASSERT_LT(name + 13, line.size()) << "non-empty client_name required: " << line;
  }
  void Input(Client& client) {
    auto input = client.instance->context->input;
    unsigned motion_frame = 0;
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e)) << "send A down";
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e)) << "send A up";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 100, 120)) << "send motion";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN, 100, 120)) << "send left down";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1, 100, 120)) << "send left up";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_WHEEL | 120, 0, 0)) << "send wheel";
    for (auto [event, text] : std::array<std::pair<std::string_view, std::string_view>, 6>{{
      {"KEY_DOWN", " scancode=4 "}, {"KEY_UP", " scancode=4 "}, {"MOUSE_MOTION", " x=100 y=120"},
      {"MOUSE_BUTTON_DOWN", " button=1 "}, {"MOUSE_BUTTON_UP", " button=1 "}, {"MOUSE_WHEEL", " y=1"}}}) {
      ASSERT_TRUE(Read("event " + std::string(event) + " ")) << event << text << ": " << process->transcript;
      ASSERT_TRUE(line.contains(text)) << "expected " << event << text << ", actual: " << line;
      if (event == "MOUSE_MOTION") {
        auto field = line.find(" frame=");
        ASSERT_NE(field, std::string::npos) << "motion frame identifier: " << line;
        motion_frame = Number(std::string_view(line).substr(field + 7));
      }
    }
    NextFrame frame(client, motion_frame);
    ASSERT_TRUE(client.Until([&] { return frame.received; })) << "next complete frame after motion";
    ASSERT_TRUE(frame.matches) << "frame excludes pointer: " << frame.matches.message();
  }
  void TearDown() override { if (process) SDL_Log("%s", process->transcript.c_str()); }
  void Escape(Client& client) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 1)) << "send Escape";
    ASSERT_TRUE(process->Exit()) << "sample exit 0 within two seconds: " << process->transcript;
  }
};

TEST_F(Sample, WholeSystem) {
  auto started = Clock::now();
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port ")) << "port <n>: " << process->transcript;
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_GT(port, 0u) << line;
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << "connect 640x480";
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event FOCUS_GAINED ")) << "FOCUS_GAINED: " << process->transcript;
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); })) << "0x010101 background and one green 32x32 block: " << Pattern(client, false).message();
  ASSERT_NO_FATAL_FAILURE(Input(client));
  ASSERT_TRUE(freerdp_disconnect(client.instance.get())) << "disconnect";
  ASSERT_TRUE(Read("event OCCLUDED ")) << "OCCLUDED: " << process->transcript;
  ASSERT_TRUE(Read("event FOCUS_LOST ")) << "FOCUS_LOST: " << process->transcript;
  Client second(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << "second session connects";
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_NO_FATAL_FAILURE(Escape(second));
  ASSERT_LT(Clock::now() - started, 5s) << "whole-system case under five seconds";
}

TEST_F(Sample, RequestedSizeReturns) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  Client first(port, true, 320, 200);
  ASSERT_TRUE(freerdp_connect(first.instance.get()));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=320x200"));
  ASSERT_TRUE(first.Until([&] { return Pattern(first, false); }));
  ASSERT_TRUE(freerdp_disconnect(first.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_LOST "));
  Client second(port, true, 800, 600);
  ASSERT_TRUE(freerdp_connect(second.instance.get()));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=800x600"));
  ASSERT_TRUE(second.Until([&] { return Pattern(second, false); }));
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(second));
}

TEST_F(Sample, TakeoverFocus) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  Client first(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(first.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(Read("event MOUSE_ENTER "));
  Client second(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(second.instance.get()));
  for (auto expected : {"OCCLUDED", "FOCUS_LOST", "MOUSE_LEAVE", "EXPOSED", "FOCUS_GAINED", "MOUSE_ENTER"}) {
    do { ASSERT_TRUE(process->Line(line, Clock::now() + 2s)) << process->transcript; }
    while (!line.starts_with("event ") || line.starts_with("event GEOMETRY "));
    EXPECT_TRUE(line.starts_with("event " + std::string(expected) + " ")) << line;
  }
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(second));
}

TEST_F(Sample, LiveCodec) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, "SDL_RDP_CODEC=remotefx");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  auto settings = client.instance->context->settings;
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE));
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event EXPOSED "));
  ASSERT_TRUE(line.ends_with("codec=remotefx")) << line;
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3b));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3b));
  ASSERT_TRUE(Read("event CODEC_CHANGED codec=nscodec")) << process->transcript;
  SDL_Log("%s", process->transcript.c_str());
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, WaitForClient) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), true));
  auto deadline = Clock::now() + 2s;
  unsigned port = 0;
  while (!(port = ListeningPort()) && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
  ASSERT_GT(port, 0u) << "sample's ephemeral listener: " << process->transcript;
  ASSERT_FALSE(Read("port ", 300ms)) << "no port line before client: " << process->transcript;
  Client client(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << "connect to waiting sample";
  ASSERT_TRUE(Read("port ")) << "port after connection: " << process->transcript;
  ASSERT_EQ(Number(std::string_view(line).substr(5)), port) << line;
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
TEST_F(Sample, DesktopIsPicture) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--size", "640x480"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=1024x768"));
  ASSERT_TRUE(client.Until([&] { return Pattern(client, false); }));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, FullscreenFollowsScreen) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, {"SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480"});
  arguments.push_back("--fullscreen");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event RESIZED "));
  EXPECT_TRUE(line.ends_with("data1=1024 data2=768")) << line;
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
  monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width = 1920; monitor.Height = 1080;
  monitor.PhysicalWidth = 500; monitor.PhysicalHeight = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  ASSERT_EQ(display.channel.load()->SendMonitorLayout(display.channel.load(), 1, &monitor), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 1920 && gdi->height == 1080; }));
  ASSERT_TRUE(Read("event RESIZED "));
  EXPECT_TRUE(line.ends_with("data1=1920 data2=1080")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, AspectMapsMouse) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--size", "640x350", "--aspect", "4:3"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 639, 479));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" x=639 y=349 ")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, VsyncAndRefresh) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.push_back("--tight");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  Headless::FrameObserver observer(client);
  auto started = Clock::now();
  for (unsigned i = 1; i <= 30; ++i) {
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() >= i; }));
    EXPECT_EQ(observer.ids.size(), i);
    std::this_thread::sleep_until(started + i * 40ms);
    ASSERT_TRUE(observer.Ack());
  }
  ASSERT_TRUE(Read("event DISPLAY_CURRENT_MODE_CHANGED "));
  float rate = 0;
  do {
    auto position = line.find(" refresh=");
    if (position != std::string::npos) rate = std::stof(line.substr(position + 9));
  } while (Read("event DISPLAY_CURRENT_MODE_CHANGED ", 150ms));
  EXPECT_NEAR(rate, 25.0, 2.5);
  SDL_Log("event PACING frames=%zu rate=%.3f", observer.ids.size(), rate);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

struct PointerObserver {
  inline static thread_local PointerObserver *active = nullptr;
  bool red = false;
  explicit PointerObserver(Client& client) {
    active = this;
    client.instance->context->update->pointer->PointerNew = Receive;
  }
  static BOOL Receive(rdpContext*, POINTER_NEW_UPDATE const* update) {
    auto& shape = update->colorPtrAttr;
    if (shape.width != 8 || shape.height != 8 || update->xorBpp != 32) return TRUE;
    auto pixels = reinterpret_cast<UINT32 const*>(shape.xorMaskData);
    active->red = std::all_of(pixels, pixels + 64, [](UINT32 pixel) { return pixel == 0xffff0000; });
    return TRUE;
  }
};
TEST_F(Sample, CursorShape) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  PointerObserver pointer(client);
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 100, 120));
  ASSERT_TRUE(client.Until([&] { return pointer.red && Pattern(client, false); }));
  SDL_Log("event POINTER width=8 height=8 argb=ffff0000 frame_has_no_red_block=1");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, Soname) {
  auto library = BuildRoot() / "sources/SDL3.so/libSDL3.so.0";
  ASSERT_TRUE(fs::is_regular_file(library));
  process = std::make_unique<Process>(std::vector<std::string>{"env", "objdump", "-p", library.string()});
  bool found = false;
  while (process->Line(line, Clock::now() + 2s)) {
    if (line.find("SONAME") == std::string::npos) continue;
    EXPECT_TRUE(line.ends_with("libSDL3.so.0")) << line;
    SDL_Log("%s", line.c_str());
    found = true;
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(process->Exit());
}

TEST_F(Sample, ClipboardAscii) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--clip", "hello"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({'h',0,'e',0,'l',0,'l',0,'o',0,0,0}); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=680065006c006c006f000000 text=hello");
  ASSERT_EQ(clipboard.RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({'h','e','l','l','o',0}); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=68656c6c6f00 text=hello");
  ASSERT_EQ(clipboard.Offer({'w',0,'o',0,'r',0,'l',0,'d',0,0,0}), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() == 1; }));
  ASSERT_TRUE(Read("event CLIPBOARD text=world"));
  SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=77006f0072006c0064000000 text=world");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ClipboardUnicode) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--clip", "żółw"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  std::vector<BYTE> bytes{0x7c,1,0xf3,0,0x42,1,0x77,0,0,0};
  ASSERT_TRUE(client.Until([&] { return clipboard.Received(bytes); }));
  SDL_Log("trace CLIPBOARD server formats=13,1 request=13 utf16le=7c01f300420177000000 text=żółw");
  ASSERT_EQ(clipboard.RequestFormat(CF_TEXT), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({'?','?','?','w',0}); }));
  SDL_Log("trace CLIPBOARD server request=1 bytes=3f3f3f7700 text=???w");
  ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  ASSERT_EQ(clipboard.Offer({0,0}), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() == 1; }));
  ASSERT_TRUE(Read("event CLIPBOARD text="));
  ASSERT_EQ(clipboard.Offer(bytes), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() == 2; }));
  ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=7c01f300420177000000 text=żółw");
  ASSERT_EQ(clipboard.Offer({}, false), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return clipboard.accepted.load() == 4; }));
  ASSERT_TRUE(Read("event CLIPBOARD text="));
  SDL_Log("trace CLIPBOARD client formats=8 text-cleared=1");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

}
