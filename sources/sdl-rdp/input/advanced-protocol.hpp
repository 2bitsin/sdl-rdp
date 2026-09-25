#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/input/protocol.hpp>

#include <freerdp/server/ainput.h>

namespace Backend {
using AdvancedProtocol = InputProtocol<ainput_server_context, ainput_server_context_free>;
template <> auto AdvancedProtocol::Open(PeerLink& link, Channel& channel)            -> Context;
template <> auto AdvancedProtocol::Service(Context const& context)                   -> bool;
template <> auto AdvancedProtocol::Handle(Context const& context)                    -> WaitHandle;
template <> auto AdvancedProtocol::Activate(Context const& context)                  -> bool;
template <> auto AdvancedProtocol::Install(Context const& context, Channel& channel) -> void;
}
