#include <sdl-rdp/freerdp-facade/touch-channel.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/freerdp-facade/record-array.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/server/rdpei.h>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::touch_channel {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::OperationName;

auto ReleaseTouch(s_rdpei_server_context* context) noexcept -> void {
  rdpei_server_context_free(context);
}
namespace {
constexpr OperationName TouchInput     { "Touch event"              };
constexpr OperationName TouchAssignment{ "Touch channel assignment" };
auto Owner(RdpeiServerContext const& context) -> TouchChannel& {
  return CallbackOwner<TouchChannel, &RdpeiServerContext::user_data>(context);
}
auto Phase(std::uint32_t flags) -> ContactPhase {
  if (flags & RDPINPUT_CONTACT_FLAG_CANCELED) return ContactPhase::Cancel;
  if (flags & RDPINPUT_CONTACT_FLAG_UP) return ContactPhase::Up;
  if (flags & RDPINPUT_CONTACT_FLAG_DOWN) return ContactPhase::Down;
  return ContactPhase::Move;
}
auto Pressure(RDPINPUT_CONTACT_DATA const& contact) -> std::optional<std::uint32_t> {
  if (!(contact.fieldsPresent & CONTACT_DATA_PRESSURE_PRESENT)) return std::nullopt;
  return contact.pressure;
}
auto Contact(RDPINPUT_CONTACT_DATA const& contact) -> TouchContact {
  return { .id       = contact.contactId,
           .x        = contact.x,
           .y        = contact.y,
           .phase    = Phase(contact.contactFlags),
           .pressure = Pressure(contact) };
}
auto Contacts(RDPINPUT_TOUCH_EVENT const& event) -> std::vector<TouchContact> {
  constexpr auto contacts = RecordArray<&RDPINPUT_TOUCH_FRAME::contacts, &RDPINPUT_TOUCH_FRAME::contactCount,
                                        RDPINPUT_TOUCH_FRAME>;
  return RecordArray<&RDPINPUT_TOUCH_EVENT::frames, &RDPINPUT_TOUCH_EVENT::frameCount>(event)
         | std::views::transform(contacts) | std::views::join | std::views::transform(Contact)
         | std::ranges::to<std::vector>();
}
// FreeRDP 3.32 channels/rdpei/server/rdpei_main.c:710 maps ERROR_NO_DATA to ERROR_READ_FAULT.
auto Serviced(std::uint32_t result) -> bool {
  switch (result) {
  case CHANNEL_RC_OK:
  case ERROR_READ_FAULT: return true;
  default:               return false;
  }
}
}
class TouchChannel::Slots {
public:
  static auto Install(RdpeiServerContext& context) -> void;

private:
  static auto Touched(TouchChannel& channel, RDPINPUT_TOUCH_EVENT const& event) -> std::uint32_t;
  static auto Assigned(TouchChannel& channel, std::uint32_t id)                 -> bool;
};
auto TouchChannel::Slots::Install(RdpeiServerContext& context) -> void {
  constexpr auto events   = [](TouchChannel const& channel, OperationName operation) noexcept {
    return SinkFailures(channel._events, operation);
  };
  constexpr auto assignee = [](TouchChannel const& channel, OperationName operation) noexcept {
    return SinkFailures(channel._assignee, operation);
  };
  // abi: rdpei onTouchEvent, UINT is uint32_t; onChannelIdAssigned, BOOL is int
  context.onTouchEvent        = Handled<Owner, &Slots::Touched, TouchInput, events, ERROR_INTERNAL_ERROR>;
  context.onChannelIdAssigned = Handled<Owner, &Slots::Assigned, TouchAssignment, assignee, false>;
}
auto TouchChannel::Slots::Touched(TouchChannel& channel, RDPINPUT_TOUCH_EVENT const& event) -> std::uint32_t {
  channel._events.Touch(Contacts(event));
  return CHANNEL_RC_OK;
}
auto TouchChannel::Slots::Assigned(TouchChannel& channel, std::uint32_t id) -> bool {
  channel._assignee.ChannelAssigned(id);
  return true;
}

TouchChannel::TouchChannel(ChannelManager& channels, TouchChannelEvents& events, AssignmentSink& assignee) noexcept
    : _channels{ channels }, _events{ events }, _assignee{ assignee } { }
auto TouchChannel::Open() -> bool {
  Expects(_context == nullptr, "a touch channel opens once");
  _context = _channels.Create<TouchContext, rdpei_server_context_new>();
  if (!_context) return false;
  auto& context = *_context;
  context.user_data = this;
  Slots::Install(context);
  return rdpei_server_init(&context) == CHANNEL_RC_OK;
}
auto TouchChannel::Pump() -> bool {
  return Serviced(rdpei_server_handle_messages(&Context()));
}
auto TouchChannel::Handle() const -> WaitHandle {
  return WaitHandle::Lent<rdpei_server_get_event_handle>(Context());
}
// Once the client has created the channel, the server ready PDU starts the protocol (MS-RDPEI 2.2.3.1).
auto TouchChannel::Activate() -> bool {
  return rdpei_server_send_sc_ready(&Context(), RDPINPUT_PROTOCOL_V10, 0) == CHANNEL_RC_OK;
}
auto TouchChannel::Context() const -> s_rdpei_server_context& {
  Expects(_context != nullptr, "the touch channel has its context");
  return *_context;
}
}
