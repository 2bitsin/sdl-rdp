#include <sdl-rdp/headless-client.test/client/handles.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::headless_client_test::client::detail::handles {
using sdl_rdp::utilities::Expects;

auto ClientHandle(Client& client) -> freerdp& {
  Expects(client.Instance() != nullptr, "the client has its instance");
  Expects(client.Instance()->context != nullptr, "the client has its context");
  return *client.Instance();
}
auto ClientContext(Client& client) -> rdpContext& {
  return *ClientHandle(client).context;
}
auto ClientUpdates(Client& client) -> rdpUpdate& {
  auto const& context = ClientContext(client);
  Expects(context.update != nullptr, "the client decodes updates");
  return *context.update;
}
}
