#pragma once
#include "observer.hpp"
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/drive/drive.hpp>
#include <sdl-rdp/drive/file-access.hpp>
#include <sdl-rdp/drive/file-request.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/files.hpp>
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
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace sdl_rdp::headless_client_test::drive::detail::session {
using sdl_rdp::drive::DriveFiles;
using sdl_rdp::drive::File;
using sdl_rdp::drive::FileAccess;
using sdl_rdp::drive::FileKind;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;

using OpenedFile = std::unique_ptr<File>;
auto Pattern(std::size_t size, std::uint32_t seed = 17)                -> std::string;
auto ReadAt(File& file, std::uint64_t offset, std::span<char> bytes)   -> std::size_t;
auto WriteAt(File& file, std::uint64_t offset, std::string_view bytes) -> std::size_t;

class DriveSession : public testing::Test {
protected:
  static auto ThenPartialReads(File& file, std::string const& source, std::string& result) -> void;
  auto SetUp()                                                         -> void override;
  auto Connect(std::string const& name = "share", bool second = false) -> void;
  auto GivenHeldFile()                                                 -> void;
  auto HoldRequests()                                                  -> void;
  auto ThenVideoMatches()                                              -> void;
  auto Disconnect()                                                    -> void;
  auto TearDown()                                                      -> void override;
  auto Write(std::string const& name, std::string const& bytes)        -> void;
  auto Open(std::string const& name, FileAccess access = { .read = true }, FileKind kind = FileKind::File)
      -> OpenedFile;
  auto Files()                                                         -> DriveFiles;
  auto ThenRemovedDrive()                                              -> void;
  auto PolledDriveName(bool added, std::uint32_t id)                   -> std::optional<std::string>;
  auto ThenDriveFailure(File& file, std::size_t warnings)              -> void;
  auto GivenDirectoryEntries()                                         -> std::set<std::string>;
  OpenedFile                     held_file;
  Logs                           logs;
  oxbox::platform::ScratchArea   scratch   { "drive", "sdl-rdp" };
  BackendInstance                backend;
  std::unique_ptr<Client>        client;
  std::unique_ptr<DriveObserver> observer;
  std::jthread                   pump;
  std::uint32_t                  drive     = 0;
};
}

namespace sdl_rdp::headless_client_test::drive {
using detail::session::DriveSession;
using detail::session::OpenedFile;
using detail::session::Pattern;
using detail::session::ReadAt;
using detail::session::WriteAt;
}
