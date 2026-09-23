#pragma once
#include "state.hpp"

namespace Backend {
template<class Action>
BOOL DispatchInput(rdpInput* input, Action action)
{
  Expects(input && input->context, "input context exists");
  auto& peer = Peer::Held(input->context->peer);
  std::scoped_lock lock(peer.owner.session_guard);
  return peer.active ? action(peer) : TRUE;
}
}
