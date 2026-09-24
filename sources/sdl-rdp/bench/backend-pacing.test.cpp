#include <sdl-rdp/headless-client.test/round-five.hpp>

#include <chrono>

namespace BackendGate {
TEST_F(RoundFive, NeverAcknowledgesClock) {
  ThenNeverAcknowledges([](auto run) {
    auto start = Clock::now();
    run();
    EXPECT_GE(Clock::now() - start, std::chrono::milliseconds(200));
  });
}
}
