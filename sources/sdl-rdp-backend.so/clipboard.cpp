#include "_detail/clipboard.hpp"

#include "_detail/activation.hpp"
#include "_detail/callback-owner.hpp"
#include "_detail/clipboard-capabilities.hpp"
#include "_detail/clipboard-store.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/peer-link.hpp"

#include <algorithm>
#include <array>
#include <freerdp/channels/wtsvc.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/clipboard.h>

namespace Backend {
ClipboardChannel::ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store,
                                   EventQueue& events, Diagnostics const& diagnostics) noexcept
    : _link { link }, _activation{ activation }, _store{ store }, _events{ events }, _diagnostics{ diagnostics } { }
ClipboardChannel::~ClipboardChannel() {
  if (_context) _context->Close(_context.get());
}
auto ClipboardChannel::Event() const -> HANDLE {
  return _opened ? _context->GetEventHandle(_context.get()) : nullptr;
}
auto ClipboardChannel::Open() -> bool {
  Expects(!_context, "clipboard opens once");
  _context.reset(cliprdr_server_context_new(_link.Channels()));
  if (!BindContext(_context.get(), this, _link.Context())) return false;
  _context->autoInitializationSequence = FALSE;
  _context->useLongFormatNames         = TRUE;
  _context->ClientFormatList           = Formats;
  _context->ClientFormatDataRequest    = DataRequest;
  _context->ClientFormatDataResponse   = DataResponse;
  if (_context->Open(_context.get()) != CHANNEL_RC_OK) return false;
  _opened = true;
  CLIPRDR_MONITOR_READY const monitor{ .common = { .msgType = CB_MONITOR_READY } };
  return SendGeneralCapabilities([&](auto const* caps) {
           return _context->ServerCapabilities(_context.get(), caps);
         }) == CHANNEL_RC_OK &&
         _context->MonitorReady(_context.get(), &monitor) == CHANNEL_RC_OK;
}
auto ClipboardChannel::Pump(std::span<HANDLE const> signaled) -> bool {
  Expects(_opened, "clipboard channel open");
  if (std::ranges::contains(signaled, Event()) && _context->CheckEventHandle(_context.get()) != CHANNEL_RC_OK)
    return false;
  if (!_ready || _announced == _store.Generation()) return true;
  return Announce() == CHANNEL_RC_OK;
}
auto ClipboardChannel::Announce() -> UINT {
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
auto ClipboardChannel::Request() -> UINT {
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
auto ClipboardChannel::RespondToList() -> UINT {
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
auto ClipboardChannel::Formats(CliprdrServerContext* context, CLIPRDR_FORMAT_LIST const* list) -> UINT {
  Expects(context, "callback context exists");
  Expects(list, "clipboard format list is supplied");
  auto& self = CallbackOwner<ClipboardChannel>(context->custom);
  try {
    auto result = self.RespondToList();
    return result == CHANNEL_RC_OK ? self.RequestOfferedText(*list) : result;
  } catch (std::exception const&) {
    return ERROR_INTERNAL_ERROR;
  }
}
auto ClipboardChannel::RequestOfferedText(CLIPRDR_FORMAT_LIST const& list) -> UINT {
  if (FirstOfferWhileAppHoldsText()) return CHANNEL_RC_OK;
  _has_unicode = std::ranges::any_of(std::span(list.formats, list.numFormats),
                                    [](auto const& format) { return format.formatId == CF_UNICODETEXT; });
  if (!_activation.Active()) return CHANNEL_RC_OK;
  // A client clipboard holding only non-text is an empty text clipboard.
  if (!_has_unicode) Changed("");
  return _has_unicode && !_pending ? Request() : CHANNEL_RC_OK;
}
namespace {
auto SendClipboardText(CliprdrServerContext* context, ClipboardStore const& clipboard, UINT32 format) -> UINT {
  std::string                  ansi;
  CLIPRDR_FORMAT_DATA_RESPONSE response{ .common = { .msgType = CB_FORMAT_DATA_RESPONSE } };
  response.common.msgFlags = CB_RESPONSE_OK;
  if (format == CF_UNICODETEXT) {
    response.requestedFormatData = clipboard.Unicode().data();
    response.common.dataLen      = clipboard.Unicode().size();
  } else if (format == CF_TEXT) {
    ansi                         = ClipboardAnsi(clipboard.Text());
    response.requestedFormatData = oxbox::utilities::SpanCast<uint8_t const>(std::span(ansi)).data();
    response.common.dataLen      = ansi.size() + 1;
  } else
    response.common.msgFlags = CB_RESPONSE_FAIL;
  return context->ServerFormatDataResponse(context, &response);
}
}
auto ClipboardChannel::DataRequest(CliprdrServerContext* context, CLIPRDR_FORMAT_DATA_REQUEST const* request) -> UINT {
  Expects(context, "callback context exists");
  Expects(request, "clipboard request is supplied");
  auto& self = CallbackOwner<ClipboardChannel>(context->custom);
  try {
    return SendClipboardText(context, self._store, request->requestedFormatId);
  } catch (std::exception const&) {
    return ERROR_INTERNAL_ERROR;
  }
}
auto ClipboardChannel::DataResponse(CliprdrServerContext* context,
                                    CLIPRDR_FORMAT_DATA_RESPONSE const* response) -> UINT {
  Expects(context, "callback context exists");
  Expects(response, "callback response is supplied");
  auto& self = CallbackOwner<ClipboardChannel>(context->custom);
  if (!self._pending) return CHANNEL_RC_OK;
  self._pending = false;
  try {
    auto const active = self._activation.Active();
    if (active && self._requested == self._offered &&
        self._requested_generation == self._store.Generation() &&
        (response->common.msgFlags & CB_RESPONSE_OK))
      self.Changed(ClipboardUtf8({ response->requestedFormatData, response->common.dataLen }));
    if (active && self._requested != self._offered && self._has_unicode) return self.Request();
  } catch (std::exception const& error) {
    self._diagnostics.Log(SDLRDP_LOG_WARN, error.what());
  }
  return CHANNEL_RC_OK;
}
} // namespace Backend
