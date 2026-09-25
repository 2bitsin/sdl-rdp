#include <sdl-rdp/sample-gate.test/client/pointer-updates.hpp>

#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::sample_gate_test::client::detail::pointer_updates {
using sdl_rdp::headless_client_test::client::ClientUpdates;
using sdl_rdp::utilities::Expects;

auto PointerUpdates(Client& client) -> rdpPointerUpdate& {
  auto const& update = ClientUpdates(client);
  Expects(update.pointer != nullptr, "the client decodes pointer updates");
  return *update.pointer;
}
}
