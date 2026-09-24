#pragma once
#include "client.hpp"
#include "drive-observer.hpp"
#include "sdl-rdp-backend.h"
#include "test-logs.hpp"

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace DriveGate {
using namespace std::chrono_literals;
class DriveSession : public testing::Test {
protected:
  auto ThenPartialReads(sdlrdp_file* file, std::string const& source, std::string& result) -> void;
  auto        SetUp()                                                   -> void override;
  auto        Connect(char const* name = "share", bool second = false)  -> void;
  auto        GivenHeldFile()                                           -> void;
  auto        HoldRequests()                                            -> void;
  auto        ThenVideoMatches()                                        -> void;
  auto        Disconnect()                                              -> void;
  auto        TearDown()                                                -> void override;
  auto        Logged(sdlrdp_log_level level, std::string_view text)     -> unsigned;
  static auto Pattern(size_t size, unsigned seed = 17)                  -> std::string;
  auto        Write(std::string const& name, std::string const& bytes)  -> void;
  auto        Open(char const* name, unsigned flags = SDLRDP_FILE_READ) -> sdlrdp_file*;
  auto        ThenRemovedDrive()                                        -> void;
  auto        ThenDriveFailure(sdlrdp_file* file, unsigned warnings)    -> void;
  auto        GivenDirectoryEntries()                                   -> std::set<std::string>;
  sdlrdp_file*                                            held_file = nullptr;
  Headless::Logs                                          logs;
  oxbox::platform::ScratchArea                            scratch   { "drive", "sdl-rdp"    };
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle    { nullptr, sdlrdp_close };
  std::unique_ptr<Headless::Client>                       client;
  std::unique_ptr<Headless::DriveObserver>                observer;
  std::jthread                                            pump;
  unsigned                                                drive     = 0;
};
}
