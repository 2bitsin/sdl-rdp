#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>

#include <sdl-rdp/headless-client.test/backend/status.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::headless_client_test::backend::detail::await_acknowledged {
auto AwaitAllAcknowledged(Headless::Client& client, Headless::BackendInstance const& backend, Headless::Logs& logs)
    -> void {
  ASSERT_TRUE(client.Until([&] { return BackendGate::AllAcknowledged(*backend); })) << logs.Text(true);
}
}
