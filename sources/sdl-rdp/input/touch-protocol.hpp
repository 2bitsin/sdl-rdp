#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/input/protocol.hpp>

#include <freerdp/server/rdpei.h>

namespace Backend {
using TouchProtocol = InputProtocol<RdpeiServerContext, rdpei_server_context_free>;
template <> auto TouchProtocol::Open(PeerLink& link, Channel& channel)            -> Context;
template <> auto TouchProtocol::Service(Context const& context)                   -> bool;
template <> auto TouchProtocol::Handle(Context const& context)                    -> WaitHandle;
template <> auto TouchProtocol::Activate(Context const& context)                  -> bool;
template <> auto TouchProtocol::Install(Context const& context, Channel& channel) -> void;
}
