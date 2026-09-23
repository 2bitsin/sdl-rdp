#include "_detail/input.hpp"
#include "_detail/state.hpp"

#include <array>

namespace Backend {
void Input::Relative(Peer& peer, int dx, int dy) {
  Expects(peer.desktop.w > 0, "desktop width is positive");
  Expects(peer.desktop.h > 0, "desktop height is positive");
  std::scoped_lock const frame(peer.owner.frame_guard);
  dx = int(int64_t(dx) * peer.owner.width / peer.desktop.w);
  dy = int(int64_t(dy) * peer.owner.height / peer.desktop.h);
  peer.owner.Push({ .type = SDLRDP_MOUSE_RELATIVE, .mouse_relative = { .dx = dx, .dy = dy } });
}

bool Input::Center(Peer& peer) {
  Expects(peer.active, "active peer has a desktop");
  auto& input = Held(peer);
  POINTER_POSITION_UPDATE const position{ UINT32(peer.desktop.w / 2), UINT32(peer.desktop.h / 2) };
  auto* context = peer.client->context;
  input.warp_requested = context->update->pointer->PointerPosition(context, &position);
  return input.warp_requested;
}
namespace {
bool AbsoluteMotion(Peer& peer, int x, int y) {
  std::scoped_lock const frame(peer.owner.frame_guard);
  int const mx = std::clamp(int(int64_t(x) * peer.owner.width / peer.desktop.w), 0, int(peer.owner.width) - 1);
  int const my = std::clamp(int(int64_t(y) * peer.owner.height / peer.desktop.h), 0, int(peer.owner.height) - 1);
  peer.owner.Push({ .type = SDLRDP_MOUSE_MOVE, .mouse_move = { .x = mx, .y = my } });
  return true;
}
}
bool Input::Motion(Peer& peer, int x, int y) {
  Expects(peer.desktop.w > 0, "desktop width is positive");
  Expects(peer.desktop.h > 0, "desktop height is positive");
  auto&     input = Held(peer);
  int const dx    = x - input.last_x;
  int const dy    = y - input.last_y;
  input.last_x = x;
  input.last_y = y;
  if (input.relative) {
    if (input.have_relative) return true;
    bool const warped = input.warp_requested && x == peer.desktop.w / 2 && y == peer.desktop.h / 2;
    input.warp_requested = false;
    if (!warped && (dx || dy)) Relative(peer, dx, dy);
    if (x < peer.desktop.w / 8 || x >= peer.desktop.w * 7 / 8 || y < peer.desktop.h / 8 || y >= peer.desktop.h * 7 / 8)
      return Center(peer);
    return true;
  }
  return AbsoluteMotion(peer, x, y);
}
UINT Input::Advanced(ainput_server_context* context, UINT64 /*unused*/, UINT64 flags, INT32 x, INT32 y) {
  Expects(context, "callback context exists");
  Expects(context->data, "channel context carries its owner");
  auto& peer = *static_cast<Peer*>(context->data);
  std::scoped_lock const lock(peer.owner.session_guard);
  if (!peer.active) return CHANNEL_RC_OK;
  auto& input = Held(peer);
  if (flags & AINPUT_FLAGS_MOVE) input.have_relative = (flags & (AINPUT_FLAGS_REL | AINPUT_FLAGS_HAVE_REL)) != 0;
  if (input.have_relative) input.warp_requested = false;
  if ((flags & AINPUT_FLAGS_MOVE) && (flags & AINPUT_FLAGS_REL)) {
    if (input.relative) Relative(peer, x, y);
  } else if ((flags & AINPUT_FLAGS_MOVE) && !Motion(peer, x, y))
    return ERROR_INTERNAL_ERROR;
  constexpr std::array<UINT64, 5> buttons{ AINPUT_FLAGS_BUTTON1, AINPUT_FLAGS_BUTTON3, AINPUT_FLAGS_BUTTON2,
                                           AINPUT_XFLAGS_BUTTON1, AINPUT_XFLAGS_BUTTON2 };
  for (unsigned i = 0; i < buttons.size(); ++i)
    if (flags & buttons[i])
      peer.owner.Push(
          { .type = SDLRDP_MOUSE_BUTTON, .mouse_button = { .button = i + 1, .down = !!(flags & AINPUT_FLAGS_DOWN) } });
  if (flags & AINPUT_FLAGS_WHEEL)
    peer.owner.Push({ .type        = SDLRDP_MOUSE_WHEEL,
                      .mouse_wheel = { .dx = float(x) / (120.0f * 65536), .dy = float(y) / (120.0f * 65536) } });
  return CHANNEL_RC_OK;
}
}
int sdlrdp_set_relative_mouse(sdlrdp_handle* handle, int enabled) {
  Backend::Expects(handle, "backend is open");
  Backend::Expects(handle->state != nullptr, "backend is open");
  auto& owner = *handle->state;
  std::scoped_lock const lock(owner.session_guard);
  if (owner.current) {
    auto& input = Backend::Input::Held(*owner.current);
    input.RelativeMode(enabled != 0);
    owner.current->wake.Transition(Backend::WakeEvent::Phase::Pending);
  }
  return 0;
}
