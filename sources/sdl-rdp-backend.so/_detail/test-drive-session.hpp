#pragma once
#include "client.hpp"
#include "drive-observer.hpp"
#include "sdl-rdp-backend.h"
#include "test-logs.hpp"

#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <oxbox/platform/scratch-area.hpp>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace DriveGate {
using namespace std::chrono_literals;
class DriveSession : public testing::Test {
protected:
  void                  ThenPartialReads(sdlrdp_file* file, std::string const& source, std::string& result);
  void                  SetUp()    override;
  void                  Connect(char const* name = "share", bool second = false);
  void                  GivenHeldFile();
  void                  HoldRequests();
  void                  ThenVideoMatches();
  void                  Disconnect();
  void                  TearDown() override;
  unsigned              Logged(sdlrdp_log_level level, std::string_view text);
  static std::string    Pattern(size_t size, unsigned seed = 17);
  void                  Write(std::string const& name, std::string const& bytes);
  sdlrdp_file*          Open(char const* name, unsigned flags = SDLRDP_FILE_READ);
  void                  ThenRemovedDrive();
  void                  ThenDriveFailure(sdlrdp_file* file, unsigned warnings);
  std::set<std::string> GivenDirectoryEntries();
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
