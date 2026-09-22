#include "_detail/state.hpp"
#include "_detail/input.hpp"
#include <array>

namespace Backend {
void Input::Relative(Peer& peer, int dx, int dy)
{
  Expects(peer.desktop.w > 0 && peer.desktop.h > 0, "desktop dimensions are positive");
  std::scoped_lock frame(peer.owner.frame_guard);
  dx = int(int64_t(dx) * peer.owner.width / peer.desktop.w);
  dy = int(int64_t(dy) * peer.owner.height / peer.desktop.h);
  peer.owner.Push({.type = SDLRDP_MOUSE_RELATIVE, .mouse_relative = {dx, dy}});
}

bool Input::Center(Peer& peer)
{
  Expects(peer.active, "active peer has a desktop");
  auto& input = Held(peer);
  input.center_pending = false;
  POINTER_POSITION_UPDATE position{UINT32(peer.desktop.w / 2), UINT32(peer.desktop.h / 2)};
  auto context = peer.client->context;
  return context->update->pointer->PointerPosition(context, &position);
}
bool Input::Motion(Peer& peer, int x, int y)
{
  Expects(peer.desktop.w > 0 && peer.desktop.h > 0, "desktop dimensions are positive");
  auto& input = Held(peer);
  input.last_x = x; input.last_y = y;
  if (input.relative) {
    if (input.have_relative) return true;
    int dx = x - peer.desktop.w / 2, dy = y - peer.desktop.h / 2;
    if (dx || dy) {
      Relative(peer, dx, dy);
      return Center(peer);
    }
    return true;
  }
  std::scoped_lock frame(peer.owner.frame_guard);
  int mx = std::clamp(int(int64_t(x) * peer.owner.width / peer.desktop.w), 0, int(peer.owner.width) - 1);
  int my = std::clamp(int(int64_t(y) * peer.owner.height / peer.desktop.h), 0, int(peer.owner.height) - 1);
  peer.owner.Push({.type = SDLRDP_MOUSE_MOVE, .mouse_move = {mx, my}});
  return true;
}
UINT Input::Advanced(ainput_server_context* context, UINT64, UINT64 flags, INT32 x, INT32 y)
{
  Expects(context && context->data, "advanced input has a peer");
  auto& peer = *static_cast<Peer*>(context->data);
  std::scoped_lock lock(peer.owner.session_guard);
  if (!peer.active) return CHANNEL_RC_OK;
  auto& input = Held(peer);
  if (flags & AINPUT_FLAGS_MOVE) input.have_relative = (flags & (AINPUT_FLAGS_REL | AINPUT_FLAGS_HAVE_REL)) != 0;
  if (input.have_relative) input.center_pending = false;
  if ((flags & AINPUT_FLAGS_MOVE) && (flags & AINPUT_FLAGS_REL)) {
    if (input.relative) Relative(peer, x, y);
  } else if ((flags & AINPUT_FLAGS_MOVE) && !Motion(peer, x, y)) return ERROR_INTERNAL_ERROR;
  constexpr std::array<UINT64, 5> buttons{AINPUT_FLAGS_BUTTON1, AINPUT_FLAGS_BUTTON3,
    AINPUT_FLAGS_BUTTON2, AINPUT_XFLAGS_BUTTON1, AINPUT_XFLAGS_BUTTON2};
  for (unsigned i = 0; i < buttons.size(); ++i)
    if (flags & buttons[i]) peer.owner.Push({.type = SDLRDP_MOUSE_BUTTON,
      .mouse_button = {i + 1, !!(flags & AINPUT_FLAGS_DOWN)}});
  if (flags & AINPUT_FLAGS_WHEEL)
    peer.owner.Push({.type = SDLRDP_MOUSE_WHEEL, .mouse_wheel = {x / (120.0f * 65536), y / (120.0f * 65536)}});
  return CHANNEL_RC_OK;
}
}
int sdlrdp_set_relative_mouse(sdlrdp_handle* handle, int enabled)
{
  Backend::Expects(handle && handle->state, "backend is open");
  auto& owner = *handle->state;
  std::scoped_lock lock(owner.session_guard);
  if (owner.current) {
    auto& input = Backend::Input::Held(*owner.current);
    input.relative = enabled != 0;
    input.center_pending = input.relative && !input.have_relative;
    SetEvent(owner.current->wake.get());
  }
  return 0;
}
