#include "_detail/state.hpp"
#include "_detail/clipboard.hpp"
#include <algorithm>
#include <freerdp/channels/wtsvc.h>
#include <winpr/clipboard.h>

namespace Backend {
ClipboardChannel::ClipboardChannel(Peer& value) : peer(value) {}
ClipboardChannel::~ClipboardChannel()
{
  if (context) context->Close(context.get());
}
HANDLE ClipboardChannel::Event() const
{
  return opened ? context->GetEventHandle(context.get()) : nullptr;
}
bool ClipboardChannel::Open()
{
  Expects(!context, "clipboard opens once");
  context.reset(cliprdr_server_context_new(peer.channels));
  if (!context) return false;
  context->custom = this;
  context->rdpcontext = peer.client->context;
  context->autoInitializationSequence = FALSE;
  context->useLongFormatNames = TRUE;
  context->ClientFormatList = Formats;
  context->ClientFormatDataRequest = DataRequest;
  context->ClientFormatDataResponse = DataResponse;
  if (context->Open(context.get()) != CHANNEL_RC_OK) return false;
  opened = true;
  CLIPRDR_GENERAL_CAPABILITY_SET general{CB_CAPSTYPE_GENERAL, CB_CAPSTYPE_GENERAL_LEN, CB_CAPS_VERSION_2, CB_USE_LONG_FORMAT_NAMES};
  CLIPRDR_CAPABILITIES caps{.common = {.msgType = CB_CLIP_CAPS}};
  caps.cCapabilitiesSets = 1;
  caps.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);
  CLIPRDR_MONITOR_READY monitor{.common = {.msgType = CB_MONITOR_READY}};
  return context->ServerCapabilities(context.get(), &caps) == CHANNEL_RC_OK
    && context->MonitorReady(context.get(), &monitor) == CHANNEL_RC_OK;
}
bool ClipboardChannel::Pump()
{
  Expects(opened, "clipboard channel open");
  if (context->CheckEventHandle(context.get()) != CHANNEL_RC_OK) return false;
  if (!ready || announced == peer.owner.clipboard.generation) return true;
  return Announce() == CHANNEL_RC_OK;
}
UINT ClipboardChannel::Announce()
{
  Expects(opened, "clipboard channel open");
  CLIPRDR_FORMAT formats[]{{CF_UNICODETEXT, nullptr}, {CF_TEXT, nullptr}};
  CLIPRDR_FORMAT_LIST list{.common = {.msgType = CB_FORMAT_LIST}};
  list.numFormats = 2;
  list.formats = formats;
  auto result = context->ServerFormatList(context.get(), &list);
  if (result == CHANNEL_RC_OK) announced = peer.owner.clipboard.generation;
  return result;
}
UINT ClipboardChannel::Request()
{
  Expects(!pending && has_unicode, "one Unicode clipboard request at a time");
  CLIPRDR_FORMAT_DATA_REQUEST request{.common = {.msgType = CB_FORMAT_DATA_REQUEST}};
  request.requestedFormatId = CF_UNICODETEXT;
  requested = offered;
  requested_generation = offered_generation;
  auto result = context->ServerFormatDataRequest(context.get(), &request);
  pending = result == CHANNEL_RC_OK;
  return result;
}
void ClipboardChannel::Changed(std::string text)
{
  Expects(peer.active, "clipboard sender is active");
  auto& clipboard = peer.owner.clipboard;
  if (clipboard.text == text) return;
  auto unicode = ClipboardUnicode(text);
  clipboard.text = std::move(text);
  clipboard.unicode = std::move(unicode);
  announced = ++clipboard.generation;
  peer.owner.Push({.type = SDLRDP_CLIPBOARD});
}
UINT ClipboardChannel::RespondToList()
{
  CLIPRDR_FORMAT_LIST_RESPONSE response{.common = {.msgType = CB_FORMAT_LIST_RESPONSE}};
  response.common.msgFlags = CB_RESPONSE_OK;
  auto result = context->ServerFormatListResponse(context.get(), &response);
  if (result == CHANNEL_RC_OK) {
    ready = true;
    offered_generation = peer.owner.clipboard.generation;
    ++offered;
  }
  return result;
}
bool ClipboardChannel::FirstOfferWhileAppHoldsText() const
{
  return offered == 1 && !peer.owner.clipboard.text.empty();
}
UINT ClipboardChannel::Formats(CliprdrServerContext* context, CLIPRDR_FORMAT_LIST const* list)
{
  Expects(context && list, "clipboard formats supplied");
  auto& self = *static_cast<ClipboardChannel*>(context->custom);
  try {
    auto result = self.RespondToList();
    return result == CHANNEL_RC_OK ? self.RequestOfferedText(*list) : result;
  } catch (std::exception const&) { return ERROR_INTERNAL_ERROR; }
}
UINT ClipboardChannel::RequestOfferedText(CLIPRDR_FORMAT_LIST const& list)
{
  if (FirstOfferWhileAppHoldsText()) return CHANNEL_RC_OK;
  has_unicode = std::ranges::any_of(std::span(list.formats, list.numFormats),
    [](auto const& format) { return format.formatId == CF_UNICODETEXT; });
  if (!peer.active) return CHANNEL_RC_OK;
  // A client clipboard holding only non-text is an empty text clipboard.
  if (!has_unicode) Changed("");
  return has_unicode && !pending ? Request() : CHANNEL_RC_OK;
}
UINT ClipboardChannel::DataRequest(CliprdrServerContext* context, CLIPRDR_FORMAT_DATA_REQUEST const* request)
{
  Expects(context && request, "clipboard request supplied");
  auto& self = *static_cast<ClipboardChannel*>(context->custom);
  try {
    auto const& clipboard = self.peer.owner.clipboard;
    std::string ansi;
    CLIPRDR_FORMAT_DATA_RESPONSE response{.common = {.msgType = CB_FORMAT_DATA_RESPONSE}};
    response.common.msgFlags = CB_RESPONSE_OK;
    if (request->requestedFormatId == CF_UNICODETEXT) {
      response.requestedFormatData = clipboard.unicode.data();
      response.common.dataLen = clipboard.unicode.size();
    } else if (request->requestedFormatId == CF_TEXT) {
      ansi = ClipboardAnsi(clipboard.text);
      response.requestedFormatData = reinterpret_cast<BYTE const*>(ansi.c_str());
      response.common.dataLen = ansi.size() + 1;
    } else response.common.msgFlags = CB_RESPONSE_FAIL;
    return context->ServerFormatDataResponse(context, &response);
  } catch (std::exception const&) { return ERROR_INTERNAL_ERROR; }
}
UINT ClipboardChannel::DataResponse(CliprdrServerContext* context, CLIPRDR_FORMAT_DATA_RESPONSE const* response)
{
  Expects(context && response, "clipboard response supplied");
  auto& self = *static_cast<ClipboardChannel*>(context->custom);
  if (!self.pending) return CHANNEL_RC_OK;
  self.pending = false;
  try {
    if (self.peer.active && self.requested == self.offered
        && self.requested_generation == self.peer.owner.clipboard.generation
        && (response->common.msgFlags & CB_RESPONSE_OK))
      self.Changed(ClipboardUtf8({response->requestedFormatData, response->common.dataLen}));
    if (self.peer.active && self.requested != self.offered && self.has_unicode) return self.Request();
  } catch (std::exception const& error) { self.peer.owner.Log(SDLRDP_LOG_WARN, error.what()); }
  return CHANNEL_RC_OK;
}
}
