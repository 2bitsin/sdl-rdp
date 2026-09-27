#pragma once
#include <sdl-rdp/freerdp-facade/dynamic-creation-sink.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/virtual-channel.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <concepts>
#include <memory>
#include <string_view>
#include <type_traits>

struct rdp_context;

namespace sdl_rdp::freerdp_facade::detail::channel_manager {
using sdl_rdp::freerdp_facade::DynamicCreationSink;
using sdl_rdp::freerdp_facade::VirtualChannel;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::freerdp_facade::detail::rdp_handles::ChannelHandle;
using sdl_rdp::freerdp_facade::detail::rdp_handles::ServerHandle;
using sdl_rdp::utilities::Releases;

// abi: the release step of a dynamic channel creation slot, handed the manager it is set on.
auto ForgetChannelCreation(void* manager) noexcept -> void;
// Ends before its ChannelManager: its release writes into the manager.
using CreationRegistration = std::unique_ptr<void, Releases<ForgetChannelCreation>>;
template <class HandleTy>
concept OwningHandle = std::same_as<HandleTy,
                                    std::unique_ptr<typename HandleTy::element_type, typename HandleTy::deleter_type>>
                       && !std::same_as<typename HandleTy::deleter_type,
                                        std::default_delete<typename HandleTy::element_type>>;
// A connection's virtual channel manager: its static channels by name, its dynamic channel state, its send queue.
class ChannelManager {
public:
  explicit           ChannelManager(rdp_context& context);
  auto               Open(std::string_view name)                  -> VirtualChannel;
  auto               Reclaim(std::string_view name) noexcept      -> void;
  auto               Joined(std::string_view name) const          -> bool;
  auto               DynamicReady() const                         -> bool;
  auto               Pump()                                       -> bool;
  auto               Flush()                                      -> bool;
  auto               Handle() const                               -> WaitHandle;
  [[nodiscard]] auto OnDynamicCreation(DynamicCreationSink& sink) -> CreationRegistration;
  template <OwningHandle ContextTy, auto CREATE>
    requires std::same_as<std::invoke_result_t<decltype(CREATE), ServerHandle::pointer>, typename ContextTy::pointer>
  auto Create() -> ContextTy {
    return ContextTy{ CREATE(Server().get()) };
  }

private:
  auto Opened(std::string_view name) noexcept -> ChannelHandle;
  auto Server() const                         -> ServerHandle const&;
  ServerHandle _handle;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::channel_manager::ChannelManager;
using detail::channel_manager::CreationRegistration;
}
