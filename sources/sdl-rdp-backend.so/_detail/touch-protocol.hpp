#pragma once
#include "input-channel.hpp"
#include "rdp-handles.hpp"

#include <freerdp/server/rdpei.h>

namespace Backend {
class TouchProtocol {
public:
  using Context = std::unique_ptr<RdpeiServerContext, Releases<rdpei_server_context_free>>;
  static auto Open(PeerLink& link, InputChannel<TouchProtocol>& channel) -> Context;
  static auto Service(Context const& context)                            -> bool;
  static auto Handle(Context const& context)                             -> HANDLE;
  static auto Activate(Context const& context)                           -> bool;

private:
  static auto Install(Context const& context, InputChannel<TouchProtocol>& channel) -> void;
};
}
