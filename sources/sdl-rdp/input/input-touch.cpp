#include "_detail/input-dispatch.hpp"
#include <sdl-rdp/core/desktop-layout.hpp>

#include <algorithm>
#include <ranges>
#include <span>

namespace Backend {
namespace {
constexpr UINT32 PressureScale = 1024;
auto Phase(UINT32 flags) -> sdlrdp_touch_phase {
  if (flags & RDPINPUT_CONTACT_FLAG_CANCELED) return SDLRDP_TOUCH_CANCEL;
  if (flags & RDPINPUT_CONTACT_FLAG_UP) return SDLRDP_TOUCH_UP;
  if (flags & RDPINPUT_CONTACT_FLAG_DOWN) return SDLRDP_TOUCH_DOWN;
  return SDLRDP_TOUCH_MOVE;
}
auto Pressure(RDPINPUT_CONTACT_DATA const& contact) -> float {
  if (!(contact.fieldsPresent & CONTACT_DATA_PRESSURE_PRESENT)) return 1.0F;
  return float(std::min(contact.pressure, PressureScale)) / float(PressureScale);
}
auto Unit(INT32 value, int extent) -> float {
  return std::clamp(float(value) / float(extent), 0.0F, 1.0F);
}
auto Contact(sdlrdp_rect desktop, RDPINPUT_CONTACT_DATA const& contact) -> sdlrdp_event {
  Expects(desktop.w > 0, "desktop width is positive");
  Expects(desktop.h > 0, "desktop height is positive");
  return { .type  = SDLRDP_TOUCH,
           .touch = { .id       = contact.contactId,
                      .x        = Unit(contact.x, desktop.w),
                      .y        = Unit(contact.y, desktop.h),
                      .pressure = Pressure(contact),
                      .phase    = Phase(contact.contactFlags) } };
}
auto Contacts(RDPINPUT_TOUCH_EVENT const& event) {
  return std::span(event.frames, event.frameCount) | std::views::transform([](RDPINPUT_TOUCH_FRAME const& frame) {
           return std::span(frame.contacts, frame.contactCount);
         })
         | std::views::join;
}
}
auto InputEvents::Touch(RDPINPUT_TOUCH_EVENT const& event) -> UINT {
  return WhenActive(UINT{ CHANNEL_RC_OK }, [&] {
    for (auto const& contact : Contacts(event)) _events.Push(Contact(_desktop.Rect(), contact));
    return UINT{ CHANNEL_RC_OK };
  });
}
}
