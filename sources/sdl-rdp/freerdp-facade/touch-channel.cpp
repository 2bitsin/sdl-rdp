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
constexpr OperationName TouchInput      { "Touch event"              };
constexpr OperationName TouchAssignment { "Touch channel assignment" };
constexpr auto          UserData        = &RdpeiServerContext::user_data;
using Owner = TouchChannel;
auto Channel(RdpeiServerContext const& context) -> Owner& {
  return CallbackOwner<Owner, UserData>(context);
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
auto Touched(TouchChannelEvents& events, RDPINPUT_TOUCH_EVENT const& event) -> std::uint32_t {
  events.Touch(Contacts(event));
  return CHANNEL_RC_OK;
}
}
class TouchChannel::Slots {
public:
  static auto Install(RdpeiServerContext& context) -> void;
};
auto TouchChannel::Slots::Install(RdpeiServerContext& context) -> void {
  constexpr auto events   = [](RdpeiServerContext const& bound) -> auto& { return Channel(bound)._events; };
  constexpr auto assignee = [](RdpeiServerContext const& bound) -> auto& { return Channel(bound)._assignee; };
  // abi: rdpei onTouchEvent, UINT is uint32_t; onChannelIdAssigned, BOOL is int
  context.onTouchEvent        = Handled<events, Touched, TouchInput, SinkFailures, ERROR_INTERNAL_ERROR>;
  context.onChannelIdAssigned = Handled<assignee, Assigned, TouchAssignment, SinkFailures, false>;
}

TouchChannel::TouchChannel(ChannelManager& channels, TouchChannelEvents& events, AssignmentSink& assignee) noexcept
    : _channels{ channels }, _events{ events }, _assignee{ assignee } { }
auto TouchChannel::Open() -> bool {
  Expects(_context == nullptr, "a touch channel opens once");
  _context = _channels.Bound<TouchContext, rdpei_server_context_new, UserData, &Slots::Install, Owner>(*this);
  return rdpei_server_init(&Context()) == CHANNEL_RC_OK;
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
