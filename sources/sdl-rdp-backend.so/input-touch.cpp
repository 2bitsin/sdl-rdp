#include "_detail/input.hpp"
#include "_detail/state.hpp"

#include <span>

namespace Backend {
namespace {
sdlrdp_event Contact(Peer const& peer, RDPINPUT_CONTACT_DATA const& contact) {
  Expects(peer.desktop.w > 0, "desktop width is positive");
  Expects(peer.desktop.h > 0, "desktop height is positive");
  sdlrdp_touch_phase phase = SDLRDP_TOUCH_MOVE;
  if (contact.contactFlags & RDPINPUT_CONTACT_FLAG_DOWN) phase = SDLRDP_TOUCH_DOWN;
  if (contact.contactFlags & RDPINPUT_CONTACT_FLAG_UP) phase = SDLRDP_TOUCH_UP;
  if (contact.contactFlags & RDPINPUT_CONTACT_FLAG_CANCELED) phase = SDLRDP_TOUCH_CANCEL;
  float const pressure = (contact.fieldsPresent & CONTACT_DATA_PRESSURE_PRESENT)
                             ? float(std::min(contact.pressure, 1024u)) / 1024.0f
                             : 1.0f;
  return { .type  = SDLRDP_TOUCH,
           .touch = { .id       = contact.contactId,
                      .x        = std::clamp(float(contact.x) / float(peer.desktop.w), 0.0f, 1.0f),
                      .y        = std::clamp(float(contact.y) / float(peer.desktop.h), 0.0f, 1.0f),
                      .pressure = pressure,
                      .phase    = phase } };
}
}
UINT Input::Touch(RdpeiServerContext* context, RDPINPUT_TOUCH_EVENT const* event) {
  Expects(context, "callback context exists");
  Expects(context->user_data, "context carries peer input state");
  Expects(event, "event is supplied");
  auto& peer = *static_cast<Peer*>(context->user_data);
  std::scoped_lock const lock(peer.owner.session_guard);
  if (!peer.active) return CHANNEL_RC_OK;
  for (auto const& frame : std::span(event->frames, event->frameCount))
    for (auto const& contact : std::span(frame.contacts, frame.contactCount))
      peer.owner.Push(Contact(peer, contact));
  return CHANNEL_RC_OK;
}
}
