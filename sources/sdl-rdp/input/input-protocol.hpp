#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/input/input-channel.hpp>

#include <concepts>
#include <memory>

namespace Backend {
// Each protocol's .cpp defines these members as explicit specializations its header declares.
template <class Server, std::invocable<Server*> auto Free> class InputProtocol {
public:
  using Context = std::unique_ptr<Server, Releases<Free>>;
  using Channel = InputChannel<InputProtocol>;
  static auto Open(PeerLink& link, Channel& channel) -> Context;
  static auto Service(Context const& context)        -> bool;
  static auto Handle(Context const& context)         -> WaitHandle;
  static auto Activate(Context const& context)       -> bool;

private:
  static auto Install(Context const& context, Channel& channel) -> void;
};
}
