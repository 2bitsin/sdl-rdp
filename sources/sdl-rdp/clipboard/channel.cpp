#include <sdl-rdp/clipboard/channel.hpp>

#include <sdl-rdp/clipboard/store.hpp>
#include <sdl-rdp/clipboard/text.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contained.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sdl_rdp::clipboard::detail::channel {
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::link::ClipboardChanged;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;

namespace {
constexpr std::array Offered{ ClipboardFormat::UnicodeText, ClipboardFormat::Text };
}
ClipboardChannel::ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store,
                                   EventQueue& events, Diagnostics const& diagnostics) noexcept
    : LoggedFailures{ diagnostics }, _activation{ activation }, _store{ store }, _events{ events },
      _channel{ link.Channels(), link.Connection(), *this } { }
auto ClipboardChannel::Event() const -> std::optional<WaitHandle> {
  if (!_opened) return std::nullopt;
  return _channel.Handle();
}
auto ClipboardChannel::Open() -> bool {
  if (!_channel.Open()) return false;
  _opened = true;
  return _channel.ServerCapabilities() && _channel.MonitorReady();
}
auto ClipboardChannel::Pump(Signalled const& signaled) -> bool {
  Expects(_opened, "clipboard channel open");
  if (signaled.Contains(Event()) && !_channel.Pump()) return false;
  if (!_ready || _announced == _store.Generation()) return true;
  return Announce();
}
auto ClipboardChannel::Announce() -> bool {
  Expects(_opened, "clipboard channel open");
  auto const sent = _channel.ServerFormatList(Offered);
  if (sent) _announced = _store.Generation();
  return sent;
}
auto ClipboardChannel::Request() -> bool {
  Expects(!_pending, "no clipboard request is pending");
  Expects(_has_unicode, "peer offers Unicode clipboard text");
  _requested            = _offered;
  _requested_generation = _offered_generation;
  _pending              = _channel.ServerFormatDataRequest(ClipboardFormat::UnicodeText);
  return _pending;
}
auto ClipboardChannel::Changed(std::string text) -> void {
  Expects(_activation.Active(), "clipboard sender is active");
  if (_store.Text() == text) return;
  _announced = _store.Replace(std::move(text));
  Logger().Line("clipboard",
                [&] { return std::format("generation={} bytes={}", _store.Generation(), _store.Text().size()); });
  _events.Push(ClipboardChanged{ });
}
auto ClipboardChannel::RespondToList() -> bool {
  if (!_channel.ServerFormatListResponse(true)) return false;
  _ready              = true;
  _offered_generation = _store.Generation();
  ++_offered;
  return true;
}
auto ClipboardChannel::FirstOfferWhileAppHoldsText() const -> bool {
  return _offered == 1 && !_store.Text().empty();
}
auto ClipboardChannel::ClientFormatList(std::span<ClipboardFormat const> formats) -> bool {
  return RespondToList() && RequestOfferedText(formats);
}
auto ClipboardChannel::RequestOfferedText(std::span<ClipboardFormat const> formats) -> bool {
  if (FirstOfferWhileAppHoldsText()) return true;
  _has_unicode = std::ranges::contains(formats, ClipboardFormat::UnicodeText);
  if (!_activation.Active()) return true;
  // A client clipboard holding only non-text is an empty text clipboard.
  if (!_has_unicode) Changed("");
  return _has_unicode && !_pending ? Request() : true;
}
auto ClipboardChannel::ClientFormatDataRequest(ClipboardFormat format) -> bool {
  switch (format) {
  case ClipboardFormat::UnicodeText: return _channel.ServerFormatDataResponse(_store.Unicode());
  case ClipboardFormat::Text: {
    auto const ansi = ClipboardAnsi(_store.Text());
    return _channel.ServerFormatDataResponse(std::as_bytes(std::span(ansi.c_str(), ansi.size() + 1)));
  }
  default: return _channel.ServerFormatDataResponse(std::nullopt);
  }
}
auto ClipboardChannel::ClientFormatDataResponse(FormatData data) -> bool {
  if (!_pending) return true;
  _pending = false;
  auto const received = [&] {
    auto const active = _activation.Active();
    if (active && _requested == _offered && _requested_generation == _store.Generation() && data)
      Changed(ClipboardUtf8(*data));
    return active && _requested != _offered && _has_unicode ? Request() : true;
  };
  return Contained(true, received, FailureLog{ Logger(), "Clipboard text decoding", LogLevel::Warn });
}
}
