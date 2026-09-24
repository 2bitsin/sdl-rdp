#include <sdl-rdp/headless-client.test/await-acknowledged.hpp>

#include <sdl-rdp/headless-client.test/peer-status.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::headless_client_test::detail::await_acknowledged {
auto AwaitAllAcknowledged(Headless::Client& client, Headless::BackendInstance const& backend, Headless::Logs& logs)
    -> void {
  ASSERT_TRUE(client.Until([&] { return BackendGate::AllAcknowledged(*backend); })) << logs.Text(true);
}
}
