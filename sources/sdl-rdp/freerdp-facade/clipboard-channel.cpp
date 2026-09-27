#include <sdl-rdp/freerdp-facade/clipboard-channel.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/server/cliprdr.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/clipboard.h>
#include <cstdint>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::clipboard_channel {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OperationName;

static_assert(ClipboardChannelName == CLIPRDR_SVC_CHANNEL_NAME);
static_assert(std::to_underlying(ClipboardFormat::Text) == CF_TEXT);
static_assert(std::to_underlying(ClipboardFormat::UnicodeText) == CF_UNICODETEXT);

namespace {
constexpr OperationName ClipboardFormats { "Clipboard format list"   };
constexpr OperationName ClipboardRequest { "Clipboard data request"  };
constexpr OperationName ClipboardResponse{ "Clipboard data response" };
auto Owner(CliprdrServerContext const& context) -> ClipboardChannel& {
  return CallbackOwner<ClipboardChannel, &CliprdrServerContext::custom>(context);
}
auto Formats(CLIPRDR_FORMAT_LIST const& list) -> std::vector<ClipboardFormat> {
  return std::span(list.formats, list.numFormats)
         | std::views::transform([](CLIPRDR_FORMAT const& format) { return ClipboardFormat{ format.formatId }; })
         | std::ranges::to<std::vector>();
}
auto Data(CLIPRDR_FORMAT_DATA_RESPONSE const& response) -> FormatData {
  if (!(response.common.msgFlags & CB_RESPONSE_OK)) return std::nullopt;
  return std::as_bytes(std::span(response.requestedFormatData, response.common.dataLen));
}
auto Entry(ClipboardFormat format) -> CLIPRDR_FORMAT {
  return { .formatId = std::to_underlying(format), .formatName = nullptr };
}
// FreeRDP's cliprdr server reports a reply it could not send as ERROR_INTERNAL_ERROR (cliprdr_server_packet_send).
auto Answered(bool replied) -> std::uint32_t {
  return replied ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;
}
auto Sent(std::uint32_t code) -> bool {
  return code == CHANNEL_RC_OK;
}
}
auto ReleaseClipboard(s_cliprdr_server_context* context) noexcept -> void {
  context->Close(context);
  cliprdr_server_context_free(context);
}

class ClipboardChannel::Slots {
public:
  static auto Install(CliprdrServerContext& context) -> void;

private:
  static auto FormatList(ClipboardChannel& channel, CLIPRDR_FORMAT_LIST const& list)                -> std::uint32_t;
  static auto DataRequest(ClipboardChannel& channel, CLIPRDR_FORMAT_DATA_REQUEST const& request)    -> std::uint32_t;
  static auto DataResponse(ClipboardChannel& channel, CLIPRDR_FORMAT_DATA_RESPONSE const& response) -> std::uint32_t;
};
auto ClipboardChannel::Slots::Install(CliprdrServerContext& context) -> void {
  constexpr auto failures = [](ClipboardChannel const& channel, OperationName operation) noexcept {
    return SinkFailures(channel._events, operation);
  };
  constexpr auto failed   = ERROR_INTERNAL_ERROR;
  // abi: psCliprdrClientFormatList, FormatDataRequest, FormatDataResponse; UINT is uint32_t
  context.ClientFormatList         = Handled<Owner, &Slots::FormatList, ClipboardFormats, failures, failed>;
  context.ClientFormatDataRequest  = Handled<Owner, &Slots::DataRequest, ClipboardRequest, failures, failed>;
  context.ClientFormatDataResponse = Handled<Owner, &Slots::DataResponse, ClipboardResponse, failures, failed>;
}
auto ClipboardChannel::Slots::FormatList(ClipboardChannel& channel, CLIPRDR_FORMAT_LIST const& list) -> std::uint32_t {
  return Answered(channel._events.ClientFormatList(Formats(list)));
}
auto ClipboardChannel::Slots::DataRequest(ClipboardChannel& channel, CLIPRDR_FORMAT_DATA_REQUEST const& request)
    -> std::uint32_t {
  return Answered(channel._events.ClientFormatDataRequest(ClipboardFormat{ request.requestedFormatId }));
}
auto ClipboardChannel::Slots::DataResponse(ClipboardChannel& channel, CLIPRDR_FORMAT_DATA_RESPONSE const& response)
    -> std::uint32_t {
  return Answered(channel._events.ClientFormatDataResponse(Data(response)));
}

ClipboardChannel::ClipboardChannel(ChannelManager& channels, Connection& connection,
                                   ClipboardChannelEvents& events) noexcept
    : _channels{ channels }, _connection{ connection }, _events{ events } { }
auto ClipboardChannel::Open() -> bool {
  Expects(_context == nullptr, "clipboard opens once");
  _context = _channels.Create<ClipboardContext, cliprdr_server_context_new>();
  if (!_context) return false;
  auto& context = *_context;
  BindContext(context, *this, _connection.Context());
  context.autoInitializationSequence = false;
  context.useLongFormatNames         = true;
  Slots::Install(context);
  return context.Open(&context) == CHANNEL_RC_OK;
}
auto ClipboardChannel::Pump() -> bool {
  auto& context = Context();
  return context.CheckEventHandle(&context) == CHANNEL_RC_OK;
}
auto ClipboardChannel::Handle() const -> WaitHandle {
  return WaitHandle::Lent<&CliprdrServerContext::GetEventHandle>(Context());
}
auto ClipboardChannel::ServerCapabilities() -> bool {
  CLIPRDR_GENERAL_CAPABILITY_SET general{ CB_CAPSTYPE_GENERAL, CB_CAPSTYPE_GENERAL_LEN, CB_CAPS_VERSION_2,
                                          CB_USE_LONG_FORMAT_NAMES };
  CLIPRDR_CAPABILITIES           caps   { .common = { .msgType = CB_CLIP_CAPS } };
  caps.cCapabilitiesSets = 1;
  // MS-RDPECLIP 2.2.2.1.1: a general capability set begins with the generic capability set header.
  caps.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);
  auto& context = Context();
  return Sent(context.ServerCapabilities(&context, &caps));
}
auto ClipboardChannel::MonitorReady() -> bool {
  CLIPRDR_MONITOR_READY const monitor { .common = { .msgType = CB_MONITOR_READY } };
  auto&                       context = Context();
  return Sent(context.MonitorReady(&context, &monitor));
}
auto ClipboardChannel::ServerFormatList(std::span<ClipboardFormat const> formats) -> bool {
  auto                entries = formats | std::views::transform(Entry) | std::ranges::to<std::vector>();
  CLIPRDR_FORMAT_LIST list    { .common = { .msgType = CB_FORMAT_LIST } };
  list.numFormats = Narrowed<std::uint32_t>(entries.size());
  list.formats    = entries.data();
  auto& context = Context();
  return Sent(context.ServerFormatList(&context, &list));
}
auto ClipboardChannel::ServerFormatListResponse(bool accepted) -> bool {
  CLIPRDR_FORMAT_LIST_RESPONSE response{ .common = { .msgType = CB_FORMAT_LIST_RESPONSE } };
  response.common.msgFlags = accepted ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
  auto& context = Context();
  return Sent(context.ServerFormatListResponse(&context, &response));
}
auto ClipboardChannel::ServerFormatDataRequest(ClipboardFormat format) -> bool {
  CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = std::to_underlying(format);
  auto& context = Context();
  return Sent(context.ServerFormatDataRequest(&context, &request));
}
auto ClipboardChannel::ServerFormatDataResponse(FormatData data) -> bool {
  CLIPRDR_FORMAT_DATA_RESPONSE response{ .common = { .msgType = CB_FORMAT_DATA_RESPONSE } };
  response.common.msgFlags = data ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
  if (data) {
    response.requestedFormatData = oxbox::utilities::SpanCast<std::uint8_t const>(*data).data();
    response.common.dataLen      = Narrowed<std::uint32_t>(data->size());
  }
  auto& context = Context();
  return Sent(context.ServerFormatDataResponse(&context, &response));
}
auto ClipboardChannel::Context() const -> s_cliprdr_server_context& {
  Expects(_context != nullptr, "the clipboard channel is open");
  return *_context;
}
}
