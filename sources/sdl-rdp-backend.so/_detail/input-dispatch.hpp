#pragma once
#include "state.hpp"

namespace Backend {
template <class Action> BOOL DispatchInput(rdpInput* input, Action action) {
  Expects(input, "input object exists");
  Expects(input->context, "input object has a context");
  auto& peer = Peer::Held(input->context->peer);
  std::scoped_lock const lock(peer.owner.session_guard);
  return peer.active ? action(peer) : TRUE;
}
}
