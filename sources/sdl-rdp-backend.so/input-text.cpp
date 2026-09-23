#include "_detail/state.hpp"
#include "_detail/input.hpp"
#include <freerdp/input.h>

namespace Backend {
BOOL Input::Unicode(rdpInput* input, UINT16 flags, UINT16 code)
{
  Expects(input && input->context, "input context exists");
  auto& peer = Peer::Held(input->context->peer);
  std::scoped_lock lock(peer.owner.session_guard);
  if (!peer.active) return TRUE;
  bool down = !(flags & KBD_FLAGS_RELEASE);
  auto point = oxbox::utilities::UtfDecode(Held(peer).unicode[down], code);
  if (point && *point != oxbox::utilities::INVALID_CODEPOINT<>) {
    peer.owner.trace.Line("key", [&] { return std::format("codepoint={} down={}", uint32_t(*point), int(down)); });
    peer.owner.Push({.type = SDLRDP_TEXT, .text = {*point, down}});
  }
  return TRUE;
}
}
