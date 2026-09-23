#pragma once
#include "sample-process.hpp"

#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>
#include <sdl-rdp-backend.so/_detail/test-input-steps.hpp>
#include <utility>

namespace SampleGate {
inline void ChangeMonitor(Client& client) {
  ASSERT_TRUE(client.Until([] { return Headless::DisplayClient::Ready(); }));
  auto monitor = Headless::DisplayClient::Monitor(1920, 1080, 500);
  ASSERT_EQ(Headless::DisplayClient::Channel()->SendMonitorLayout(Headless::DisplayClient::Channel(), 1, &monitor),
            CHANNEL_RC_OK);
}
inline int64_t WallMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
inline UINT32 PatternPixel(rdpGdi const* gdi, int index) {

  UINT32 value = 0;
  std::memcpy(&value,
              gdi->primary_buffer + (static_cast<std::size_t>((index / 640)) * gdi->stride) +
                  ((static_cast<std::ptrdiff_t>(index % 640)) * 4),
              4);
  return value & 0xffffff;
}
inline testing::AssertionResult Pattern(Client& client, bool /*pointer*/) {
  auto* gdi = client.Instance()->context->gdi;
  if (!gdi || gdi->width != 640 || gdi->height != 480)
    return testing::AssertionFailure() << "framebuffer is not 640x480";
  auto pixel   = [&](int index) { return PatternPixel(gdi, index); };
  auto columns = std::views::iota(0, 640);
  auto first   = std::ranges::find_if(columns, [&](int x) { return pixel((40 * 640) + x) == 0x00ff00; });
  if (first == columns.end() || *first > 608) return testing::AssertionFailure() << "no complete green block on row 40";
  auto expected = [&](int index) {
    int const x        = index % 640;
    int const y        = index / 640;
    auto      expected = x >= *first && x < *first + 32 && y >= 40 && y < 72 ? 0x00ff00u : 0x010101u;
    return expected;
  };
  auto indices  = std::views::iota(0, 640 * 480);
  auto mismatch = std::ranges::find_if(indices, [&](int i) { return pixel(i) != expected(i); });
  if (mismatch == indices.end()) return testing::AssertionSuccess();
  return testing::AssertionFailure() << "pixel (" << *mismatch % 640 << "," << *mismatch / 640
                                     << ") actual=" << std::hex << pixel(*mismatch)
                                     << " expected=" << expected(*mismatch);
}

class FullDesktopFrames {
public:
  FullDesktopFrames(FullDesktopFrames const&) = delete;
  FullDesktopFrames(FullDesktopFrames&&)      = delete;
  explicit FullDesktopFrames(Client& client)
      : update(client.Instance()->context->update), surface(update->SurfaceBits), bitmap(update->BitmapUpdate) {
    Expects(!active, "no observer is already installed");
    Expects(surface, "surface callback is installed");
    Expects(bitmap, "bitmap callback is installed");
    active               = this;
    update->SurfaceBits  = ReceiveSurface;
    update->BitmapUpdate = ReceiveBitmap;
  }
  ~FullDesktopFrames() {
    update->SurfaceBits  = surface;
    update->BitmapUpdate = bitmap;
    active               = nullptr;
  }
  FullDesktopFrames& operator =(FullDesktopFrames const&) = delete;
  FullDesktopFrames& operator =(FullDesktopFrames&&) = delete;
  unsigned Full() const { return full; }
  unsigned Deliveries() const { return deliveries; }

private:
  void Observe(rdpContext* context, unsigned left, unsigned top, unsigned right, unsigned bottom) {
    auto* gdi = context->gdi;
    ++deliveries;
    rows.resize(gdi->height);
    if (left || std::cmp_not_equal(right, gdi->width) || top >= bottom || bottom > rows.size()) return;
    std::fill(rows.begin() + top, rows.begin() + bottom, true);
    if (std::ranges::all_of(rows, [](bool covered) { return covered; })) {
      ++full;
      std::ranges::fill(rows, false);
    }
  }
  static BOOL ReceiveSurface(rdpContext* context, SURFACE_BITS_COMMAND const* command) {
    Expects(active, "observer is installed");
    Expects(command, "wire command is supplied");
    auto result = active->surface(context, command);
    if (result) active->Observe(context, command->destLeft, command->destTop, command->destRight, command->destBottom);
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command) {
    Expects(active, "observer is installed");
    Expects(command, "wire command is supplied");
    auto result = active->bitmap(context, command);
    if (result)
      for (auto const& rectangle : std::span(command->rectangles, command->number))
        active->Observe(context, rectangle.destLeft, rectangle.destTop, rectangle.destRight + 1,
                        rectangle.destBottom + 1);
    return result;
  }
  unsigned                                      full       = 0;
  unsigned                                      deliveries = 0;
  inline static thread_local FullDesktopFrames* active     = nullptr;
  rdpUpdate*                                    update;
  pSurfaceBits                                  surface;
  pBitmapUpdate                                 bitmap;
  std::vector<bool>                             rows;
};

class NextFrame {
public:
  NextFrame(NextFrame const&) = delete;
  NextFrame(NextFrame&&)      = delete;
  explicit NextFrame(Client& value, unsigned frame)
      : client(value), column(frame % 640), original(value.Instance()->context->update->SurfaceBits),
        original_bitmap(value.Instance()->context->update->BitmapUpdate) {
    Expects(!active, "no observer is already installed");
    Expects(original, "original surface callback is installed");
    Expects(original_bitmap, "original bitmap callback is installed");
    active                                           = this;
    client.Instance()->context->update->SurfaceBits  = Receive;
    client.Instance()->context->update->BitmapUpdate = ReceiveBitmap;
  }
  ~NextFrame() {
    client.Instance()->context->update->SurfaceBits  = original;
    client.Instance()->context->update->BitmapUpdate = original_bitmap;
    active                                           = nullptr;
  }
  NextFrame& operator =(NextFrame const&) = delete;
  NextFrame& operator =(NextFrame&&) = delete;
  bool Received() const { return received; }
  auto const& Matches() const { return matches; }

private:
  static BOOL Receive(rdpContext* context, SURFACE_BITS_COMMAND const* command) {
    Expects(active, "observer is installed");
    Expects(command, "wire command is supplied");
    auto result = active->original(context, command);
    if (result && command->destBottom == 480) active->Observe(context);
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command) {
    Expects(active, "observer is installed");
    Expects(command, "wire command is supplied");
    auto result = active->original_bitmap(context, command);
    // Bitmap update corners are inclusive; surface command corners are exclusive.
    if (result && std::ranges::any_of(std::span(command->rectangles, command->number),
                                      [](auto const& rectangle) { return rectangle.destBottom == 479; }))
      active->Observe(context);
    return result;
  }
  void Observe(rdpContext* context) {
    Expects(context, "callback context exists");
    Expects(context->gdi, "decoded framebuffer exists");
    auto*  gdi   = context->gdi;
    UINT32 pixel = 0;
    std::memcpy(&pixel, gdi->primary_buffer + (40uz * gdi->stride) + (static_cast<std::size_t>(column) * 4), 4);
    UINT32 before = 0;
    if (column)
      std::memcpy(&before, gdi->primary_buffer + (40uz * gdi->stride) + ((static_cast<std::size_t>(column - 1)) * 4),
                  4);
    bool const origin = (pixel & 0xffffff) == 0x00ff00 && (before & 0xffffff) != 0x00ff00;
    if (!received && origin) {
      matches  = Pattern(client, true);
      received = true;
    }
  }
  bool                                  received        = false;
  testing::AssertionResult              matches         = testing::AssertionFailure() << "no complete frame";
  inline static thread_local NextFrame* active          = nullptr;
  Client&                               client;
  unsigned                              column;
  pSurfaceBits                          original;
  pBitmapUpdate                         original_bitmap;
};

inline void ThenAdvanced(Client& client) {
  ASSERT_TRUE(client.Until([&] {
    return InputClient::Advanced().load() && InputClient::Touch().load() &&
           InputClient::Touch().load()->GetVersion(InputClient::Touch().load()) == RDPINPUT_PROTOCOL_V10;
  }));
}
class SampleProcess : public testing::Test {
protected:
  void ThenInputEvent(std::string_view event, std::string_view text, unsigned& motion_frame) {
    ASSERT_TRUE(Read("event " + std::string(event) + " ")) << event << text << ": " << process->Transcript();
    ASSERT_TRUE(line.contains(text)) << "expected " << event << text << ", actual: " << line;
    if (event == "MOUSE_MOTION") {
      auto field = line.find(" frame=");
      ASSERT_NE(field, std::string::npos) << "motion frame identifier: " << line;
      motion_frame = Number(std::string_view(line).substr(field + 7));
    }
  }
  void GivenProcess(std::vector<std::string> arguments = { }) {
    if (arguments.empty()) arguments = Arguments(certificates.Path(), false);
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  void GivenFocus(Client& client) {
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
  }
  void WhenTextStops(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3c));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3c));
    ASSERT_TRUE(Read("event TEXT_MODE active=0"));
  }

  void WhenRelative(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3d));
    ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  }
  void SetUp() override {
    std::scoped_lock const lock(log_guard);
    client_logs = &logs;
    auto* root = WLog_GetRoot();
    ASSERT_NE(root, nullptr);
    wLogCallbacks callbacks{ CollectClientLog, CollectClientLog, CollectClientLog, CollectClientLog };
    ASSERT_TRUE(WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK));
    ASSERT_TRUE(WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks));
  }
  std::string ConnectLogs() {
    // Drain the child pipe too: connect can fail before another Read consumes its diagnostics.
    std::string ignored;
    auto        deadline = Clock::now() + 10ms;
    if (process)
      while (process->Line(ignored, deadline)) {
      }
    return "\nclient:\n" + logs.Text(true) + "\nsample:\n" + (process ? process->Transcript() : "");
  }
  bool Read(std::string_view expected, std::chrono::milliseconds timeout = 10s) {
    Expects(process != nullptr, "sample process is running");
    Expects(!expected.empty(), "expected output is nonempty");
    Expects(timeout > 0ms, "read timeout is positive");
    auto deadline = Clock::now() + timeout;
    while (process->Line(line, deadline))
      if (line.starts_with(expected)) return true;
    return false;
  }
  bool ReadInput(Client& client, std::string_view expected) {
    Expects(!expected.empty(), "expected input event supplied");
    bool received = false;
    return client.Until([&] { return received || (received = Read(expected, 1ms)); });
  }

  void Exposed() {
    ASSERT_TRUE(Read("event EXPOSED ")) << "EXPOSED missing: " << process->Transcript();
    auto name = line.find(" client_name=");
    ASSERT_NE(name, std::string::npos) << line;
    ASSERT_LT(name + 13, line.size()) << "non-empty client_name required: " << line;
  }
  void Input(Client& client) {
    unsigned motion_frame = 0;
    Headless::SendKeyboardAndMouse(client, 100, 120);
    if (::testing::Test::HasFatalFailure()) return;
    for (auto [event, text] :
         std::array<std::pair<std::string_view, std::string_view>, 6>{ { { "KEY_DOWN", " scancode=4 " },
                                                                         { "KEY_UP", " scancode=4 " },
                                                                         { "MOUSE_MOTION", " x=100 y=120" },
                                                                         { "MOUSE_BUTTON_DOWN", " button=1 " },
                                                                         { "MOUSE_BUTTON_UP", " button=1 " },
                                                                         { "MOUSE_WHEEL", " y=1" } } }) {
      ThenInputEvent(event, text, motion_frame);
      if (::testing::Test::HasFatalFailure()) return;
    }
    NextFrame frame(client, motion_frame);
    ASSERT_TRUE(client.Until([&] { return frame.Received(); })) << "next complete frame after motion";
    ASSERT_TRUE(frame.Matches()) << "frame excludes pointer: " << frame.Matches().message();
  }
  void TearDown() override {
    {
      std::scoped_lock const lock(log_guard);
      client_logs = nullptr;
    }
    if (process) SDL_Log("%s", process->Transcript().c_str());
  }
  void Escape(Client const& client) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 1))
        << "send Escape";
    ASSERT_TRUE(process->Exit()) << "sample exit 0 within ten seconds: " << process->Transcript();
  }
  Headless::Logs               logs;
  oxbox::platform::ScratchArea certificates{ "certificates", "sdl-rdp" };
  std::unique_ptr<Process>     process;
  std::string                  line;

private:
  static BOOL CollectClientLog(wLogMessage const* message) {
    std::scoped_lock const lock(log_guard);
    if (client_logs && message->TextString) {
      auto level = message->Level == WLOG_ERROR  ? SDLRDP_LOG_ERROR
                   : message->Level == WLOG_WARN ? SDLRDP_LOG_WARN
                                                 : SDLRDP_LOG_INFO;
      Headless::Logs::Collect(client_logs, level, message->TextString);
    }
    return TRUE;
  }
  inline static std::mutex      log_guard;
  inline static Headless::Logs* client_logs = nullptr;
};

} // namespace SampleGate
