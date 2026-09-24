#pragma once
#include <sdl-rdp/sample-gate.test/process.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <sdl-rdp/headless-client.test/client.hpp>
#include <sdl-rdp/headless-client.test/logs.hpp>
#include <winpr/wlog.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace SampleGate {
using Headless::Client;
using Headless::Clock;
using utilities::Expects;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

class SampleProcess : public testing::Test {
protected:
  auto ThenInputEvent(std::string_view event, std::string_view text, unsigned& motion_frame)         -> void;
  auto GivenProcess(std::vector<std::string> arguments = { })                                        -> void;
  auto GivenFocus(Client& client)                                                                    -> void;
  auto WhenTextStops(rdpInput* input)                                                                -> void;
  auto WhenRelative(rdpInput* input)                                                                 -> void;
  auto SetUp()                                                                                       -> void override;
  auto ConnectLogs()                                                                                 -> std::string;
  auto Read(std::string_view expected, std::chrono::milliseconds timeout = 10s)                      -> bool;
  auto ReadInput(Client& client, std::string_view expected, std::chrono::milliseconds timeout = 10s) -> bool;
  auto Exposed()                                                                                     -> void;
  auto Input(Client& client)                                                                         -> void;
  auto TearDown()                                                                                    -> void override;
  auto Escape(Client const& client)                                                                  -> void;
  Headless::Logs               logs;
  oxbox::platform::ScratchArea certificates{ "certificates", "sdl-rdp" };
  std::unique_ptr<Process>     process;
  std::string                  line;

private:
  static auto CollectClientLog(wLogMessage const* message) -> BOOL;
  inline static std::mutex      log_guard;
  inline static Headless::Logs* client_logs = nullptr;
};

} // namespace SampleGate
