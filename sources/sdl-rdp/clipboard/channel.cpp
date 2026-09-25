#include <sdl-rdp/clipboard/channel.hpp>

#include <sdl-rdp/clipboard/capabilities.hpp>
#include <sdl-rdp/clipboard/store.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/dispatched.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/channels/wtsvc.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/clipboard.h>
#include <algorithm>
#include <array>
#include <cstdint>

namespace Backend {
namespace {
auto Owner(CliprdrServerContext* context) -> ClipboardChannel& {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<ClipboardChannel>(context->custom);
}
}
class ClipboardChannel::Callbacks {
public:
  static auto Install(CliprdrServerContext& server) -> void;

private:
  template <auto HANDLER, class PduTy>
  static auto Handled(CliprdrServerContext* context, PduTy const* pdu, OperationName operation) noexcept
      -> std::uint32_t;
};
template <auto HANDLER, class PduTy>
auto ClipboardChannel::Callbacks::Handled(CliprdrServerContext* context, PduTy const* pdu,
                                          OperationName operation) noexcept -> std::uint32_t {
  auto& owner = Owner(context);
  return Dispatched<HANDLER>(ERROR_INTERNAL_ERROR, owner, pdu, FailureLog{ owner._diagnostics, operation });
}
auto ClipboardChannel::Callbacks::Install(CliprdrServerContext& server) -> void {
  // abi: psCliprdrClientFormatList, UINT is uint32_t
  server.ClientFormatList = [](CliprdrServerContext* context,
                               CLIPRDR_FORMAT_LIST const* list) noexcept -> std::uint32_t {
    return Handled<&ClipboardChannel::Formats>(context, list, "Clipboard format list");
  };
  // abi: psCliprdrClientFormatDataRequest, UINT is uint32_t
  server.ClientFormatDataRequest = [](CliprdrServerContext* context,
                                      CLIPRDR_FORMAT_DATA_REQUEST const* request) noexcept -> std::uint32_t {
    return Handled<&ClipboardChannel::DataRequest>(context, request, "Clipboard data request");
  };
  // abi: psCliprdrClientFormatDataResponse, UINT is uint32_t
  server.ClientFormatDataResponse = [](CliprdrServerContext* context,
                                       CLIPRDR_FORMAT_DATA_RESPONSE const* response) noexcept -> std::uint32_t {
    return Handled<&ClipboardChannel::DataResponse>(context, response, "Clipboard data response");
  };
}
ClipboardChannel::ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store,
                                   EventQueue& events, Diagnostics const& diagnostics) noexcept
    : _link{ link }, _activation{ activation }, _store{ store }, _events{ events }, _diagnostics{ diagnostics } { }
ClipboardChannel::~ClipboardChannel() {
  if (_context) _context->Close(_context.get());
}
auto ClipboardChannel::Event() const -> WaitHandle {
  return _opened ? _context->GetEventHandle(_context.get()) : nullptr;
}
auto ClipboardChannel::Open() -> bool {
  Expects(!_context, "clipboard opens once");
  _context.reset(cliprdr_server_context_new(_link.Channels()));
  if (!BindContext(_context.get(), this, _link.Context())) return false;
  _context->autoInitializationSequence = false;
  _context->useLongFormatNames         = true;
  Callbacks::Install(*_context);
  if (_context->Open(_context.get()) != CHANNEL_RC_OK) return false;
  _opened = true;
  CLIPRDR_MONITOR_READY const monitor{ .common = { .msgType = CB_MONITOR_READY } };
  return SendGeneralCapabilities([&](auto const* caps) { return _context->ServerCapabilities(_context.get(), caps); })
             == CHANNEL_RC_OK
         && _context->MonitorReady(_context.get(), &monitor) == CHANNEL_RC_OK;
}
auto ClipboardChannel::Pump(std::span<WaitHandle const> signaled) -> bool {
  Expects(_opened, "clipboard channel open");
  if (std::ranges::contains(signaled, Event()) && _context->CheckEventHandle(_context.get()) != CHANNEL_RC_OK)
    return false;
  if (!_ready || _announced == _store.Generation()) return true;
  return Announce() == CHANNEL_RC_OK;
}
auto ClipboardChannel::Announce() -> std::uint32_t {
  Expects(_opened, "clipboard channel open");
  std::array<CLIPRDR_FORMAT, 2> formats{ { { .formatId = CF_UNICODETEXT, .formatName = nullptr },
                                           { .formatId = CF_TEXT       , .formatName = nullptr } } };
  CLIPRDR_FORMAT_LIST           list   { .common = { .msgType = CB_FORMAT_LIST } };
  list.numFormats = 2;
  list.formats    = formats.data();
  auto result = _context->ServerFormatList(_context.get(), &list);
  if (result == CHANNEL_RC_OK) _announced = _store.Generation();
  return result;
}
auto ClipboardChannel::Request() -> std::uint32_t {
  Expects(!_pending, "no clipboard request is pending");
  Expects(_has_unicode, "peer offers Unicode clipboard text");
  CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = CF_UNICODETEXT;
  _requested                = _offered;
  _requested_generation     = _offered_generation;
  auto result = _context->ServerFormatDataRequest(_context.get(), &request);
  _pending = result == CHANNEL_RC_OK;
  return result;
}
auto ClipboardChannel::Changed(std::string text) -> void {
  Expects(_activation.Active(), "clipboard sender is active");
  if (_store.Text() == text) return;
  _announced = _store.Replace(std::move(text));
  _diagnostics.Line("clipboard",
                    [&] { return std::format("generation={} bytes={}", _store.Generation(), _store.Text().size()); });
  _events.Push({ .type = SDLRDP_CLIPBOARD });
}
auto ClipboardChannel::RespondToList() -> std::uint32_t {
  CLIPRDR_FORMAT_LIST_RESPONSE response{ .common = { .msgType = CB_FORMAT_LIST_RESPONSE } };
  response.common.msgFlags = CB_RESPONSE_OK;
  auto result = _context->ServerFormatListResponse(_context.get(), &response);
  if (result == CHANNEL_RC_OK) {
    _ready              = true;
    _offered_generation = _store.Generation();
    ++_offered;
  }
  return result;
}
auto ClipboardChannel::FirstOfferWhileAppHoldsText() const -> bool {
  return _offered == 1 && !_store.Text().empty();
}
auto ClipboardChannel::Formats(CLIPRDR_FORMAT_LIST const& list) -> std::uint32_t {
  auto result = RespondToList();
  return result == CHANNEL_RC_OK ? RequestOfferedText(list) : result;
}
auto ClipboardChannel::RequestOfferedText(CLIPRDR_FORMAT_LIST const& list) -> std::uint32_t {
  if (FirstOfferWhileAppHoldsText()) return CHANNEL_RC_OK;
  _has_unicode = std::ranges::any_of(std::span(list.formats, list.numFormats),
                                     [](auto const& format) { return format.formatId == CF_UNICODETEXT; });
  if (!_activation.Active()) return CHANNEL_RC_OK;
  // A client clipboard holding only non-text is an empty text clipboard.
  if (!_has_unicode) Changed("");
  return _has_unicode && !_pending ? Request() : CHANNEL_RC_OK;
}
namespace {
auto SendClipboardText(CliprdrServerContext* context, ClipboardStore const& clipboard, std::uint32_t format)
    -> std::uint32_t {
  std::string                  ansi;
  CLIPRDR_FORMAT_DATA_RESPONSE response{ .common = { .msgType = CB_FORMAT_DATA_RESPONSE } };
  response.common.msgFlags = CB_RESPONSE_OK;
  if (format == CF_UNICODETEXT) {
    response.requestedFormatData = oxbox::utilities::SpanCast<std::uint8_t const>(clipboard.Unicode()).data();
    response.common.dataLen      = clipboard.Unicode().size();
  } else if (format == CF_TEXT) {
    ansi                         = ClipboardAnsi(clipboard.Text());
    response.requestedFormatData = oxbox::utilities::SpanCast<std::uint8_t const>(std::span(ansi)).data();
    response.common.dataLen      = ansi.size() + 1;
  } else
    response.common.msgFlags = CB_RESPONSE_FAIL;
  return context->ServerFormatDataResponse(context, &response);
}
}
auto ClipboardChannel::DataRequest(CLIPRDR_FORMAT_DATA_REQUEST const& request) -> std::uint32_t {
  return SendClipboardText(_context.get(), _store, request.requestedFormatId);
}
auto ClipboardChannel::DataResponse(CLIPRDR_FORMAT_DATA_RESPONSE const& response) -> std::uint32_t {
  if (!_pending) return CHANNEL_RC_OK;
  _pending = false;
  auto const received = [&] {
    auto const active = _activation.Active();
    if (active && _requested == _offered && _requested_generation == _store.Generation()
        && (response.common.msgFlags & CB_RESPONSE_OK))
      Changed(ClipboardUtf8(std::as_bytes(std::span(response.requestedFormatData, response.common.dataLen))));
    return active && _requested != _offered && _has_unicode ? Request() : std::uint32_t{ CHANNEL_RC_OK };
  };
  return Contained(std::uint32_t{ CHANNEL_RC_OK }, received,
                   FailureLog{ _diagnostics, "Clipboard text decoding", SDLRDP_LOG_WARN });
}
} // namespace Backend
