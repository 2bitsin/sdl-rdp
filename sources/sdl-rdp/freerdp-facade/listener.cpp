#include <sdl-rdp/freerdp-facade/listener.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/listener.h>
#include <tuple>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::listener {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::DescriptorOf;
using sdl_rdp::utilities::OperationName;

auto ReleaseListener(rdp_freerdp_listener* listener) noexcept -> void {
  listener->Close(listener);
  freerdp_listener_free(listener);
}
namespace {
constexpr OperationName PeerConstruction{ "Peer construction" };
auto Events(freerdp_listener const& listener) -> ListenerEvents& {
  return CallbackOwner<ListenerEvents, &freerdp_listener::info>(listener);
}
auto Opened(ListenerEvents& events) -> ListenerHandle {
  ListenerHandle listener{ freerdp_listener_new() };
  if (!listener) throw AllocationFailed{ "Listener" };
  constexpr auto accepted = [](ListenerEvents& owner, freerdp_peer& client) {
    owner.Accepted(Connection{ PeerHandle{ &client } });
    return true;
  };
  listener->info = &events;
  // abi: psPeerAccepted; true hands the peer over even when construction failed and RAII released it.
  listener->PeerAccepted = Handled<Events, accepted, PeerConstruction, SinkFailures, true>;
  return listener;
}
auto Adopting(ListenerHandle listener, Socket socket) -> ListenerHandle {
  if (!listener->OpenFromSocket(listener.get(), DescriptorOf(socket.Native())))
    throw ListenerFailed{ "socket adoption" };
  std::ignore = socket.Release();
  return listener;
}
}
Listener::Listener(Socket socket, ListenerEvents& events)
    : _port{ socket.Port() }, _listener{ Adopting(Opened(events), std::move(socket)) } { }
auto Listener::Port() const noexcept -> std::uint16_t {
  return _port;
}
auto Listener::EventHandles(std::span<WaitHandle> budget) const -> std::span<WaitHandle> {
  return WaitHandle::Collected<&freerdp_listener::GetEventHandles>(*_listener, budget);
}
auto Listener::Pump() -> bool {
  return _listener->CheckFileDescriptor(_listener.get());
}
}
