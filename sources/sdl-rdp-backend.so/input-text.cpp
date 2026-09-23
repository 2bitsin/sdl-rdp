#include "_detail/input-dispatch.hpp"
#include "_detail/input.hpp"
#include <freerdp/input.h>

namespace Backend {
BOOL Input::Unicode(rdpInput* input, UINT16 flags, UINT16 code)
{
  return DispatchInput(input, [&](Peer& peer) {
    bool down  = !(flags & KBD_FLAGS_RELEASE);
    auto point = oxbox::utilities::UtfDecode(Held(peer).unicode[down], code);
    if (point && *point != oxbox::utilities::INVALID_CODEPOINT<>) {
      peer.owner.trace.Line("key", [&] { return std::format("codepoint={} down={}", uint32_t(*point), int(down)); });
      peer.owner.Push({
          .type = SDLRDP_TEXT, .text = { .codepoint = *point, .down = down }
      });
    }
    return TRUE;
  });
}
}
