#pragma once
#include "sample-process.hpp"

namespace SampleGate {
inline testing::AssertionResult Pattern(Client &client, bool /*pointer*/) {
  auto *gdi = client.instance->context->gdi;
  if (!gdi || gdi->width != 640 || gdi->height != 480)
    return testing::AssertionFailure() << "framebuffer is not 640x480";
  auto pixel = [&](int index) {
    UINT32 value = 0;
    std::memcpy(&value, gdi->primary_buffer + (static_cast<std::size_t>((index / 640)) * gdi->stride) + ((static_cast<std::ptrdiff_t>(index % 640)) * 4), 4);
    return value & 0xffffff;
  };
  auto columns = std::views::iota(0, 640);
  auto first = std::ranges::find_if(columns, [&](int x) { return pixel((40 * 640) + x) == 0x00ff00; });
  if (first == columns.end() || *first > 608)
    return testing::AssertionFailure() << "no complete green block on row 40";
  auto expected = [&](int index) {
    int x = index % 640;
    int y = index / 640;
    auto expected = x >= *first && x < *first + 32 && y >= 40 && y < 72 ? 0x00ff00u : 0x010101u;
    return expected;
  };
  auto indices = std::views::iota(0, 640 * 480);
  auto mismatch = std::ranges::find_if(indices, [&](int i) { return pixel(i) != expected(i); });
  if (mismatch == indices.end())
    return testing::AssertionSuccess();
  return testing::AssertionFailure() << "pixel (" << *mismatch % 640 << "," << *mismatch / 640
                                     << ") actual=" << std::hex << pixel(*mismatch) << " expected=" << expected(*mismatch);
}

class FullDesktopFrames {
public:
  FullDesktopFrames(FullDesktopFrames const &) = delete;
  FullDesktopFrames &operator=(FullDesktopFrames const &) = delete;
  FullDesktopFrames(FullDesktopFrames &&) = delete;
  FullDesktopFrames &operator=(FullDesktopFrames &&) = delete;
  explicit FullDesktopFrames(Client &client) : update(client.instance->context->update),
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
  void Observe(rdpContext *context, unsigned left, unsigned top, unsigned right, unsigned bottom) {
    auto *gdi = context->gdi;
    ++deliveries;
    rows.resize(gdi->height);
    if (left || right != unsigned(gdi->width) || top >= bottom || bottom > rows.size())
      return;
    std::fill(rows.begin() + top, rows.begin() + bottom, true);
    if (std::ranges::all_of(rows, [](bool covered) { return covered; })) {
      ++full;
      std::ranges::fill(rows, false);
    }
  }
  static BOOL ReceiveSurface(rdpContext *context, SURFACE_BITS_COMMAND const *command) {
    Expects(active && command, "desktop observer and surface command exist");
    auto result = active->surface(context, command);
    if (result)
      active->Observe(context, command->destLeft, command->destTop, command->destRight, command->destBottom);
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext *context, BITMAP_UPDATE const *command) {
    Expects(active && command, "desktop observer and bitmap command exist");
    auto result = active->bitmap(context, command);
    if (result)
      for (auto const &rectangle : std::span(command->rectangles, command->number))
        active->Observe(context, rectangle.destLeft, rectangle.destTop, rectangle.destRight + 1, rectangle.destBottom + 1);
    return result;
  }
  unsigned full = 0, deliveries = 0;

private:
  inline static thread_local FullDesktopFrames *active = nullptr;
  rdpUpdate *update;
  pSurfaceBits surface;
  pBitmapUpdate bitmap;
  std::vector<bool> rows;
};

class NextFrame {
public:
  NextFrame(NextFrame const &) = delete;
  NextFrame &operator=(NextFrame const &) = delete;
  NextFrame(NextFrame &&) = delete;
  NextFrame &operator=(NextFrame &&) = delete;
  explicit NextFrame(Client &value, unsigned frame) : client(value), column(frame % 640), original(value.instance->context->update->SurfaceBits),
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
  static BOOL Receive(rdpContext *context, SURFACE_BITS_COMMAND const *command) {
    Expects(active && command, "frame observer and surface command exist");
    auto result = active->original(context, command);
    if (result && command->destBottom == 480)
      active->Observe(context);
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext *context, BITMAP_UPDATE const *command) {
    Expects(active && command, "frame observer and bitmap command exist");
    auto result = active->original_bitmap(context, command);
    // Bitmap update corners are inclusive; surface command corners are exclusive.
    if (result && std::ranges::any_of(std::span(command->rectangles, command->number),
                                      [](auto const &rectangle) { return rectangle.destBottom == 479; }))
      active->Observe(context);
    return result;
  }
  void Observe(rdpContext *context) {
    Expects(context && context->gdi, "decoded framebuffer exists");
    auto *gdi = context->gdi;
    UINT32 pixel = 0;
    std::memcpy(&pixel, gdi->primary_buffer + (40uz * gdi->stride) + (static_cast<std::size_t>(column) * 4), 4);
    UINT32 before = 0;
    if (column)
      std::memcpy(&before, gdi->primary_buffer + (40uz * gdi->stride) + ((static_cast<std::size_t>(column - 1)) * 4), 4);
    bool const origin = (pixel & 0xffffff) == 0x00ff00 && (before & 0xffffff) != 0x00ff00;
    if (!received && origin) {
      matches = Pattern(client, true);
      received = true;
    }
  }
  bool received = false;
  testing::AssertionResult matches = testing::AssertionFailure() << "no complete frame";

private:
  inline static thread_local NextFrame *active = nullptr;
  Client &client;
  unsigned column;
  pSurfaceBits original;
  pBitmapUpdate original_bitmap;
};

class Sample : public testing::Test {
protected:
  void GivenProcess(std::vector<std::string> arguments = {}) {
    if (arguments.empty())
      arguments = Arguments(certificates.Path(), false);
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  void GivenFocus(Client &client) {
    ASSERT_TRUE(freerdp_connect(client.instance.get()));
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
  }
  void WhenTextStops(rdpInput *input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3c));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3c));
    ASSERT_TRUE(Read("event TEXT_MODE active=0"));
  }
  static void ThenAdvanced(Client &client, InputClient &channels) {
    ASSERT_TRUE(client.Until([&] { return channels.advanced.load() && channels.touch.load() && channels.touch.load()->GetVersion(channels.touch.load()) == RDPINPUT_PROTOCOL_V10; }));
  }
  void WhenRelative(rdpInput *input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3d));
    ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  }
  void SetUp() override {
    std::scoped_lock const lock(log_guard);
    client_logs = &logs;
    auto *root = WLog_GetRoot();
    ASSERT_NE(root, nullptr);
    wLogCallbacks callbacks{CollectClientLog, CollectClientLog, CollectClientLog, CollectClientLog};
    ASSERT_TRUE(WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK));
    ASSERT_TRUE(WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks));
  }
  std::string ConnectLogs() {
    // Drain the child pipe too: connect can fail before another Read consumes its diagnostics.
    std::string ignored;
    auto deadline = Clock::now() + 10ms;
    if (process)
      while (process->Line(ignored, deadline)) {
      }
    return "\nclient:\n" + logs.Text(true) + "\nsample:\n" + (process ? process->transcript : "");
  }
  bool Read(std::string_view expected, std::chrono::milliseconds timeout = 10s) {
    Expects(process && !expected.empty() && timeout > 0ms, "running sample, expected line and deadline supplied");
    auto deadline = Clock::now() + timeout;
    while (process->Line(line, deadline))
      if (line.starts_with(expected))
        return true;
    return false;
  }
  bool ReadInput(Client &client, std::string_view expected) {
    Expects(!expected.empty(), "expected input event supplied");
    bool received = false;
    return client.Until([&] { return received || (received = Read(expected, 1ms)); });
  }
  void DelayAcknowledgement(Client &client, Headless::FrameObserver &frames) {
    Expects(frames.ack_times.size() >= 2, "two previous acknowledgements define the delay");
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
    auto interval = frames.ack_times.back() - frames.ack_times[frames.ack_times.size() - 2];
    std::this_thread::sleep_until(frames.ack_times.back() + interval * 3);
  }
  void IncrementalFrames(Client &client, Headless::FrameObserver &frames, FullDesktopFrames &desktop, std::string_view change) {
    Expects(!change.empty(), "frame trigger is named");
    auto baseline = desktop.full;
    auto before = frames.ids.size();
    auto deliveries = desktop.deliveries;
    if (change == "delayed ack")
      ASSERT_NO_FATAL_FAILURE(DelayAcknowledgement(client, frames));
    ASSERT_TRUE(frames.Ack());
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
  void Input(Client &client) {
    auto *input = client.instance->context->input;
    unsigned motion_frame = 0;
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e)) << "send A down";
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e)) << "send A up";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 100, 120)) << "send motion";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN, 100, 120)) << "send left down";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1, 100, 120)) << "send left up";
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_WHEEL | 120, 0, 0)) << "send wheel";
    for (auto [event, text] : std::array<std::pair<std::string_view, std::string_view>, 6>{
             {{"KEY_DOWN", " scancode=4 "}, {"KEY_UP", " scancode=4 "}, {"MOUSE_MOTION", " x=100 y=120"}, {"MOUSE_BUTTON_DOWN", " button=1 "}, {"MOUSE_BUTTON_UP", " button=1 "}, {"MOUSE_WHEEL", " y=1"}}}) {
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
    {
      std::scoped_lock const lock(log_guard);
      client_logs = nullptr;
    }
    if (process)
      SDL_Log("%s", process->transcript.c_str());
  }
  void Escape(Client &client) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 1)) << "send Escape";
    ASSERT_TRUE(process->Exit()) << "sample exit 0 within ten seconds: " << process->transcript;
  }
  Headless::Logs logs;
  oxbox::platform::ScratchArea certificates{"certificates", "sdl-rdp"};
  std::unique_ptr<Process> process;
  std::string line;

private:
  static BOOL CollectClientLog(wLogMessage const *message) {
    std::scoped_lock const lock(log_guard);
    if (client_logs && message->TextString) {
      auto level = message->Level == WLOG_ERROR  ? SDLRDP_LOG_ERROR
                   : message->Level == WLOG_WARN ? SDLRDP_LOG_WARN
                                                 : SDLRDP_LOG_INFO;
      Headless::Logs::Collect(client_logs, level, message->TextString);
    }
    return TRUE;
  }
  inline static std::mutex log_guard;
  inline static Headless::Logs *client_logs = nullptr;
};

} // namespace SampleGate
