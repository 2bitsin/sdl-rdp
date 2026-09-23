#pragma once
#include <sdl-rdp-backend.so/_detail/headless-client.hpp>
#include <sdl-rdp-backend.so/_detail/test-logs.hpp>
#include <winpr/wlog.h>
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
namespace SampleGate {
using Headless::Client;
using Headless::Clock;
using utilities::Expects;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

inline fs::path BuildRoot() {
  auto path = std::getenv("PATH");
  Expects(path != nullptr, "ctest supplies PATH");
  for (auto part : std::string_view(path) | std::views::split(':')) {
    auto directory = fs::path(std::string_view(part));
    if (fs::is_regular_file(directory / "sdl-rdp-sample")) return directory.parent_path();
  }
  Expects(false, "built sample exists on ctest PATH");
  return {};
}

inline pid_t Spawn(std::vector<std::string> arguments, int& output) {
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
    auto deadline = Clock::now() + 10s;
    int status = 0;
    while (Clock::now() < deadline) {
      if (waitpid(pid, &status, WNOHANG) == pid) { pid = -1; return WIFEXITED(status) && WEXITSTATUS(status) == 0; }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }
};

inline std::vector<std::string> Arguments(fs::path const& certificates, bool wait) {
  Expects(fs::is_directory(certificates), "certificate directory exists");
  auto root = BuildRoot();
  auto backend = root / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
  Expects(fs::is_regular_file(backend), "built backend exists");
  return {"env", "SDL_VIDEO_DRIVER=rdp", "SDL_RDP_PORT=0", "SDL_RDP_BIND=127.0.0.1",
    "SDL_RDP_CERT_DIR=" + certificates.string(), "SDL_RDP_BACKEND=" + backend.string(),
    "SDL_RDP_CODEC=planar", "SDL_RDP_WAIT_FOR_CLIENT=" + std::to_string(wait), (root / "bin/sdl-rdp-sample").string()};
}

inline unsigned Number(std::string_view text, int base = 10) {
  Expects(base >= 2 && base <= 36, "valid integer base");
  unsigned value = 0;
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}

inline pid_t ProcId() {
  // procfs can belong to an outer PID namespace; its children file uses that namespace.
  std::ifstream children("/proc/thread-self/children");
  pid_t child = 0;
  Expects(bool(children >> child), "spawned child visible in procfs");
  return child;
}

inline unsigned ListeningPort(pid_t pid = 0) {
  if (!pid) pid = ProcId();
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

inline testing::AssertionResult Pattern(Client& client, bool pointer) {
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

class FirstFrameSize {
  inline static thread_local FirstFrameSize* active = nullptr;
  Client& client;
  decltype(freerdp::PostConnect) original_connect;
  pEndPaint original_paint = nullptr;
  bool paint_installed = false;
public:
  bool received = false;
  int width = 0, height = 0;
  explicit FirstFrameSize(Client& value) : client(value), original_connect(value.instance->PostConnect) {
    Expects(!active && original_connect, "one first-frame observer before connection");
    active = this;
    client.instance->PostConnect = Connect;
  }
  ~FirstFrameSize() {
    client.instance->PostConnect = original_connect;
    if (paint_installed) client.instance->context->update->EndPaint = original_paint;
    active = nullptr;
  }
  static BOOL Connect(freerdp* instance) {
    Expects(active && instance, "first-frame observer and client exist");
    if (!active->original_connect(instance)) return FALSE;
    active->original_paint = instance->context->update->EndPaint;
    instance->context->update->EndPaint = Paint;
    active->paint_installed = true;
    return TRUE;
  }
  static BOOL Paint(rdpContext* context) {
    Expects(active && context && context->gdi, "first-frame observer and framebuffer exist");
    if (!active->received) {
      active->width = context->gdi->width;
      active->height = context->gdi->height;
      active->received = true;
    }
    return active->original_paint ? active->original_paint(context) : TRUE;
  }
};

class FullDesktopFrames {
  inline static thread_local FullDesktopFrames* active = nullptr;
  rdpUpdate* update;
  pSurfaceBits surface;
  pBitmapUpdate bitmap;
  std::vector<bool> rows;
public:
  unsigned full = 0, deliveries = 0;
  explicit FullDesktopFrames(Client& client) : update(client.instance->context->update),
      surface(update->SurfaceBits), bitmap(update->BitmapUpdate) {
    Expects(!active && surface && bitmap, "one desktop observer with GDI installed");
    active = this;
    update->SurfaceBits = ReceiveSurface;
    update->BitmapUpdate = ReceiveBitmap;
  }
  ~FullDesktopFrames() {
    update->SurfaceBits = surface;
    update->BitmapUpdate = bitmap;
    active = nullptr;
  }
  void Observe(rdpContext* context, unsigned left, unsigned top, unsigned right, unsigned bottom) {
    auto gdi = context->gdi;
    ++deliveries;
    rows.resize(gdi->height);
    if (left || right != unsigned(gdi->width) || top >= bottom || bottom > rows.size()) return;
    std::fill(rows.begin() + top, rows.begin() + bottom, true);
    if (std::ranges::all_of(rows, [](bool covered) { return covered; })) {
      ++full;
      std::fill(rows.begin(), rows.end(), false);
    }
  }
  static BOOL ReceiveSurface(rdpContext* context, SURFACE_BITS_COMMAND const* command) {
    Expects(active && command, "desktop observer and surface command exist");
    auto result = active->surface(context, command);
    if (result) active->Observe(context, command->destLeft, command->destTop, command->destRight, command->destBottom);
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command) {
    Expects(active && command, "desktop observer and bitmap command exist");
    auto result = active->bitmap(context, command);
    if (result) for (auto const& rectangle : std::span(command->rectangles, command->number))
      active->Observe(context, rectangle.destLeft, rectangle.destTop, rectangle.destRight + 1, rectangle.destBottom + 1);
    return result;
  }
};

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
  inline static std::mutex log_guard;
  inline static Headless::Logs* client_logs = nullptr;
  static BOOL CollectClientLog(wLogMessage const* message) {
    std::scoped_lock lock(log_guard);
    if (client_logs && message->TextString) {
      auto level = message->Level == WLOG_ERROR ? SDLRDP_LOG_ERROR
        : message->Level == WLOG_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO;
      Headless::Logs::Collect(client_logs, level, message->TextString);
    }
    return TRUE;
  }
protected:
  Headless::Logs logs;
  void SetUp() override {
    std::scoped_lock lock(log_guard);
    client_logs = &logs;
    auto root = WLog_GetRoot();
    ASSERT_NE(root, nullptr);
    wLogCallbacks callbacks{CollectClientLog, CollectClientLog, CollectClientLog, CollectClientLog};
    ASSERT_TRUE(WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK));
    ASSERT_TRUE(WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks));
  }
  std::string ConnectLogs() {
    // Drain the child pipe too: connect can fail before another Read consumes its diagnostics.
    std::string ignored;
    auto deadline = Clock::now() + 10ms;
    if (process) while (process->Line(ignored, deadline)) {}
    return "\nclient:\n" + logs.Text(true) + "\nsample:\n" + (process ? process->transcript : "");
  }
  oxbox::platform::ScratchArea certificates{"certificates", "sdl-rdp"};
  std::unique_ptr<Process> process;
  std::string line;
  bool Read(std::string_view expected, std::chrono::milliseconds timeout = 10s) {
    Expects(process && !expected.empty() && timeout > 0ms, "running sample, expected line and deadline supplied");
    auto deadline = Clock::now() + timeout;
    while (process->Line(line, deadline)) if (line.starts_with(expected)) return true;
    return false;
  }
  bool ReadInput(Client& client, std::string_view expected) {
    Expects(!expected.empty(), "expected input event supplied");
    bool received = false;
    return client.Until([&] { return received || (received = Read(expected, 1ms)); });
  }
  void IncrementalFrames(Client& client, Headless::FrameObserver& frames, FullDesktopFrames& desktop, std::string_view change) {
    auto baseline = desktop.full;
    auto before = frames.ids.size();
    auto deliveries = desktop.deliveries;
    if (change == "refresh") {
      ASSERT_GE(frames.ack_times.size(), 2u);
      ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
      ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
      auto interval = frames.ack_times.back() - frames.ack_times[frames.ack_times.size() - 2];
      std::this_thread::sleep_until(frames.ack_times.back() + interval * 3);
    }
    ASSERT_TRUE(frames.Ack());
    if (change == "refresh") {
      ASSERT_TRUE(ReadInput(client, "event DISPLAY_CURRENT_MODE_CHANGED "));
      EXPECT_FALSE(line.contains("numerator=0 "));
      SDL_Log("trace refresh change: %s", line.c_str());
    }
    ASSERT_TRUE(client.Until([&] { return frames.ids.size() >= before + 2; }));
    EXPECT_EQ(desktop.full, baseline);
    EXPECT_GT(desktop.deliveries, deliveries);
    EXPECT_EQ(client.instance->context->gdi->width, 320);
    EXPECT_EQ(client.instance->context->gdi->height, 200);
    SDL_Log("trace exclusive %.*s gdi=%dx%d new_full_desktop=%u frames=%zu", int(change.size()), change.data(),
            client.instance->context->gdi->width, client.instance->context->gdi->height,
            desktop.full - baseline, frames.ids.size() - before);
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
  void TearDown() override {
    { std::scoped_lock lock(log_guard); client_logs = nullptr; }
    if (process) SDL_Log("%s", process->transcript.c_str());
  }
  void Escape(Client& client) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 1)) << "send Escape";
    ASSERT_TRUE(process->Exit()) << "sample exit 0 within ten seconds: " << process->transcript;
  }
};

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
}
