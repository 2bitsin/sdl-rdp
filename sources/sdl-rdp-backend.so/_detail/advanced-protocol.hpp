#pragma once
#include "input-channel.hpp"
#include "rdp-handles.hpp"

#include <freerdp/server/ainput.h>

namespace Backend {
class AdvancedProtocol {
public:
  using Context = std::unique_ptr<ainput_server_context, Releases<ainput_server_context_free>>;
  static auto Open(PeerLink& link, InputChannel<AdvancedProtocol>& channel) -> Context;
  static auto Service(Context const& context)                               -> bool;
  static auto Handle(Context const& context)                                -> HANDLE;
  static auto Activate(Context const& context)                              -> bool;

private:
  static auto Install(Context const& context, PeerLink& link, InputChannel<AdvancedProtocol>& channel) -> void;
  static auto Started(Context const& context)                                                          -> bool;
};
}
