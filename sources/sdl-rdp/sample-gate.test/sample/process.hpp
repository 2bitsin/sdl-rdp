#pragma once
#include <sdl-rdp/sample-gate.test/process/process.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <winpr/wlog.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::sample_gate_test::sample::detail::process {
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::sample_gate_test::process::Process;
using sdl_rdp::utilities::Expects;
using std::chrono_literals::operator""s;

class SampleProcess : public testing::Test {
protected:
  auto GivenProcess(Words const& environment = { }, Words const& options = { }) -> void;
  auto Launch(Words const& arguments)                                           -> void;
  auto AnnouncedClient(std::uint32_t width, std::uint32_t height)               -> Client;
  auto Connect(Client& client)                                                  -> void;
  auto ConnectAcknowledging(Client& client)                                     -> void;
  auto SetUp()                                                                  -> void override;
  auto Read(std::string_view expected, std::chrono::milliseconds timeout = 10s) -> bool;
  auto Exposed()                                                                -> void;
  auto TearDown()                                                               -> void override;
  auto Escape(Client& client)                                                   -> void;
  Logs                         logs;
  oxbox::platform::ScratchArea certificates{ "certificates", "sdl-rdp" };
  std::unique_ptr<Process>     process;
  std::string                  line;

private:
  auto        ConnectLogs()                                -> std::string;
  static auto CollectClientLog(wLogMessage const& message) -> void;
  inline static std::mutex log_guard;
  inline static Logs*      client_logs = nullptr;
};

}

namespace sdl_rdp::sample_gate_test::sample {
using detail::process::SampleProcess;
}
