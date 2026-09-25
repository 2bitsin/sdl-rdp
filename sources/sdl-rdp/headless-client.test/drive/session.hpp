#pragma once
#include "observer.hpp"
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace sdl_rdp::headless_client_test::drive::detail::session {
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;

auto Pattern(std::size_t size, std::uint32_t seed = 17) -> std::string;

class DriveSession : public testing::Test {
protected:
  auto ThenPartialReads(sdlrdp_file* file, std::string const& source, std::string& result) -> void;
  auto SetUp()                                                        -> void override;
  auto Connect(char const* name = "share", bool second = false)       -> void;
  auto GivenHeldFile()                                                -> void;
  auto HoldRequests()                                                 -> void;
  auto ThenVideoMatches()                                             -> void;
  auto Disconnect()                                                   -> void;
  auto TearDown()                                                     -> void override;
  auto Logged(sdlrdp_log_level level, std::string_view text)          -> std::size_t;
  auto Write(std::string const& name, std::string const& bytes)       -> void;
  auto Open(char const* name, std::uint32_t flags = SDLRDP_FILE_READ) -> sdlrdp_file*;
  auto ThenRemovedDrive()                                             -> void;
  auto PolledDriveName(bool added, std::uint32_t id)                  -> std::optional<std::string>;
  auto ThenDriveFailure(sdlrdp_file* file, std::size_t warnings)      -> void;
  auto GivenDirectoryEntries()                                        -> std::set<std::string>;
  sdlrdp_file*                   held_file = nullptr;
  Logs                           logs;
  oxbox::platform::ScratchArea   scratch   { "drive", "sdl-rdp" };
  BackendInstance                handle;
  std::unique_ptr<Client>        client;
  std::unique_ptr<DriveObserver> observer;
  std::jthread                   pump;
  std::uint32_t                  drive     = 0;
};
}

namespace sdl_rdp::headless_client_test::drive {
using detail::session::DriveSession;
using detail::session::Pattern;
}
