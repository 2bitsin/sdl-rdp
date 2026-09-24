#pragma once
#include <sdl-rdp/sample-gate.test/process.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <sdl-rdp/headless-client.test/client.hpp>
#include <sdl-rdp/headless-client.test/logs.hpp>
#include <winpr/wlog.h>
#include <chrono>
#include <cstdint>
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
  auto GivenProcess(Words const& environment = { }, Words const& options = { }) -> void;
  auto Launch(Words const& arguments)                                           -> void;
  auto AnnouncedClient(std::uint32_t width, std::uint32_t height)               -> Client;
  auto Connect(Client const& client)                                            -> void;
  auto ConnectAcknowledging(Client const& client)                               -> void;
  auto SetUp()                                                                  -> void override;
  auto Read(std::string_view expected, std::chrono::milliseconds timeout = 10s) -> bool;
  auto Exposed()                                                                -> void;
  auto TearDown()                                                               -> void override;
  auto Escape(Client const& client)                                             -> void;
  Headless::Logs               logs;
  oxbox::platform::ScratchArea certificates{ "certificates", "sdl-rdp" };
  std::unique_ptr<Process>     process;
  std::string                  line;

private:
  auto        ConnectLogs()                                -> std::string;
  static auto CollectClientLog(wLogMessage const* message) -> BOOL;
  inline static std::mutex      log_guard;
  inline static Headless::Logs* client_logs = nullptr;
};

} // namespace SampleGate
