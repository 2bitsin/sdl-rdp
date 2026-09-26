#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/input/channel.hpp>

#include <freerdp/server/ainput.h>
#include <freerdp/server/rdpei.h>
#include <concepts>
#include <memory>
#include <optional>

namespace sdl_rdp::input::detail::protocol {
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Releases;

template <class Server, std::invocable<Server*> auto Free> class InputProtocol {
public:
  using Context = std::unique_ptr<Server, Releases<Free>>;
  using Channel = InputChannel<InputProtocol>;
  static auto Open(PeerLink& link, Channel& channel) -> Context;
  static auto Service(Context const& context)        -> bool;
  static auto Handle(Context const& context)         -> std::optional<WaitHandle>;
  static auto Activate(Context const& context)       -> bool;

private:
  static auto Install(Server& server, Channel& channel) -> void;
};
using AdvancedProtocol = InputProtocol<ainput_server_context, ainput_server_context_free>;
template <> auto AdvancedProtocol::Open(PeerLink& link, Channel& channel)             -> Context;
template <> auto AdvancedProtocol::Service(Context const& context)                    -> bool;
template <> auto AdvancedProtocol::Handle(Context const& context)                     -> std::optional<WaitHandle>;
template <> auto AdvancedProtocol::Activate(Context const& context)                   -> bool;
template <> auto AdvancedProtocol::Install(ainput_server_context& server, Channel& channel) -> void;
using TouchProtocol    = InputProtocol<RdpeiServerContext, rdpei_server_context_free>;
template <> auto TouchProtocol::Open(PeerLink& link, Channel& channel)                -> Context;
template <> auto TouchProtocol::Service(Context const& context)                       -> bool;
template <> auto TouchProtocol::Handle(Context const& context)                        -> std::optional<WaitHandle>;
template <> auto TouchProtocol::Activate(Context const& context)                      -> bool;
template <> auto TouchProtocol::Install(RdpeiServerContext& server, Channel& channel) -> void;
}

namespace sdl_rdp::input {
using detail::protocol::AdvancedProtocol;
using detail::protocol::TouchProtocol;
}
