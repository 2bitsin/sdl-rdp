#include <sdl-rdp/headless-client.test/await-acknowledged.hpp>

#include <sdl-rdp/headless-client.test/peer-status.hpp>

#include <gtest/gtest.h>

namespace BackendGate {
auto AwaitAllAcknowledged(Headless::Client& client, Headless::BackendInstance const& backend, Headless::Logs& logs)
    -> void {
  ASSERT_TRUE(client.Until([&] { return AllAcknowledged(*backend); })) << logs.Text(true);
}
}
