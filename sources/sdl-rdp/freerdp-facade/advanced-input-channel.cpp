#include <sdl-rdp/freerdp-facade/advanced-input-channel.hpp>

#include <sdl-rdp/freerdp-facade/button-flags.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/server/ainput.h>
#include <cstdint>
#include <optional>

namespace sdl_rdp::freerdp_facade::detail::advanced_input_channel {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::OperationName;

auto ReleaseAdvancedInput(s_ainput_server_context* context) noexcept -> void {
  ainput_server_context_free(context);
}
namespace {
constexpr OperationName AdvancedMouse      { "Advanced input mouse event"        };
constexpr OperationName AdvancedAssignment { "Advanced input channel assignment" };
constexpr float         WheelUnit          = 120.0F * 65536;
constexpr auto          UserData           = &ainput_server_context::data;
using Owner = AdvancedInputChannel;

constexpr ButtonFlags<std::uint64_t, 5> AdvancedButtons{ { { AINPUT_FLAGS_BUTTON1 , PointerButton::Left    },
                                                           { AINPUT_FLAGS_BUTTON3 , PointerButton::Middle  },
                                                           { AINPUT_FLAGS_BUTTON2 , PointerButton::Right   },
                                                           { AINPUT_XFLAGS_BUTTON1, PointerButton::Back    },
                                                           { AINPUT_XFLAGS_BUTTON2, PointerButton::Forward } } };
auto Channel(ainput_server_context const& context) -> Owner& {
  return CallbackOwner<Owner, UserData>(context);
}
auto Wheel(std::uint64_t flags, std::int32_t x, std::int32_t y) -> std::optional<WheelTurn> {
  if (!(flags & AINPUT_FLAGS_WHEEL)) return std::nullopt;
  return WheelTurn{ .horizontal = static_cast<float>(x) / WheelUnit, .vertical = static_cast<float>(y) / WheelUnit };
}
auto Pointer(std::uint64_t flags, std::int32_t x, std::int32_t y) -> AdvancedPointerEvent {
  return { .buttons          = Buttons(flags, AdvancedButtons),
           .down             = (flags & AINPUT_FLAGS_DOWN) != 0,
           .moved            = (flags & AINPUT_FLAGS_MOVE) != 0,
           .relative         = (flags & AINPUT_FLAGS_REL) != 0,
           .relative_capable = (flags & (AINPUT_FLAGS_REL | AINPUT_FLAGS_HAVE_REL)) != 0,
           .x                = x,
           .y                = y,
           .wheel            = Wheel(flags, x, y) };
}
auto Mouse(AdvancedInputChannelEvents& events, std::uint64_t /*timestamp*/, std::uint64_t flags, std::int32_t x,
           std::int32_t y) -> std::uint32_t {
  return events.AdvancedPointer(Pointer(flags, x, y)) ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;
}
}
class AdvancedInputChannel::Slots {
public:
  static auto Install(ainput_server_context& context) -> void;
};
auto AdvancedInputChannel::Slots::Install(ainput_server_context& context) -> void {
  constexpr auto events   = [](ainput_server_context const& bound) -> auto& { return Channel(bound)._events; };
  constexpr auto assignee = [](ainput_server_context const& bound) -> auto& { return Channel(bound)._assignee; };
  // abi: psAInputServerMouseEvent, UINT is uint32_t; psAInputChannelIdAssigned, BOOL is int
  context.MouseEvent        = Handled<events, Mouse, AdvancedMouse, SinkFailures, ERROR_INTERNAL_ERROR>;
  context.ChannelIdAssigned = Handled<assignee, Assigned, AdvancedAssignment, SinkFailures, false>;
}

AdvancedInputChannel::AdvancedInputChannel(ChannelManager& channels, AdvancedInputChannelEvents& events,
                                           AssignmentSink& assignee) noexcept
    : _channels{ channels }, _events{ events }, _assignee{ assignee } { }
auto AdvancedInputChannel::Open() -> bool {
  Expects(_context == nullptr, "an advanced input channel opens once");
  _context = _channels.Bound<AdvancedInputContext, ainput_server_context_new, UserData, &Slots::Install, Owner>(*this);
  return Started();
}
auto AdvancedInputChannel::Pump() -> bool {
  auto& context = Context();
  return context.Poll(&context) == CHANNEL_RC_OK;
}
auto AdvancedInputChannel::Handle() const -> std::optional<WaitHandle> {
  return WaitHandle::Reported<&ainput_server_context::ChannelHandle>(Context());
}
// Once the client has created the channel, a poll sends the server version PDU (FreeRDP 3.32 ainput_main.c:560).
auto AdvancedInputChannel::Activate() -> bool {
  return Pump();
}
// Initialized for an external thread: the peer loop polls the channel on its handle.
auto AdvancedInputChannel::Started() -> bool {
  auto& context = Context();
  return context.Initialize(&context, true) == CHANNEL_RC_OK && context.Open(&context) == CHANNEL_RC_OK && Pump()
         && Handle().has_value();
}
auto AdvancedInputChannel::Context() const -> s_ainput_server_context& {
  Expects(_context != nullptr, "the advanced input channel has its context");
  return *_context;
}
}
