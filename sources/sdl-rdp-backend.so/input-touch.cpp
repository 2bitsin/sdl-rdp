#include "_detail/state.hpp"
#include "_detail/input.hpp"
#include <span>

namespace Backend {
namespace {
sdlrdp_event Contact(Peer const& peer, RDPINPUT_CONTACT_DATA const& contact)
{
  Expects(peer.desktop.w > 0 && peer.desktop.h > 0, "touch desktop dimensions are positive");
  sdlrdp_touch_phase phase = SDLRDP_TOUCH_MOVE;
  if (contact.contactFlags & RDPINPUT_CONTACT_FLAG_DOWN) phase = SDLRDP_TOUCH_DOWN;
  if (contact.contactFlags & RDPINPUT_CONTACT_FLAG_UP) phase = SDLRDP_TOUCH_UP;
  if (contact.contactFlags & RDPINPUT_CONTACT_FLAG_CANCELED) phase = SDLRDP_TOUCH_CANCEL;
  float pressure = (contact.fieldsPresent & CONTACT_DATA_PRESSURE_PRESENT)
                       ? std::min(contact.pressure, 1024u) / 1024.0f
                       : 1.0f;
  return {
    .type = SDLRDP_TOUCH, .touch = { .id = contact.contactId, .x = std::clamp(float(contact.x) / peer.desktop.w, 0.0f, 1.0f), .y = std::clamp(float(contact.y) / peer.desktop.h, 0.0f, 1.0f), .pressure = pressure, .phase = phase }
  };
}
}
UINT Input::Touch(RdpeiServerContext* context, const RDPINPUT_TOUCH_EVENT* event)
{
  Expects(context && context->user_data && event, "touch event has a peer");
  auto& peer = *static_cast<Peer*>(context->user_data);
  std::scoped_lock const lock(peer.owner.session_guard);
  if (!peer.active) return CHANNEL_RC_OK;
  for (auto const& frame : std::span(event->frames, event->frameCount))
    for (auto const& contact : std::span(frame.contacts, frame.contactCount))
      peer.owner.Push(Contact(peer, contact));
  return CHANNEL_RC_OK;
}
}
