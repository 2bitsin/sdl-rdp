#pragma once
#include "drive-wire.hpp"
#include "headless-drive.hpp"
#include "sdl-rdp-backend.h"
#include "test-config.hpp"
#include "test-io.hpp"
#include "test-logs.hpp"

#include <climits>
#include <cstring>
#include <freerdp/channels/rdpdr.h>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <oxbox/platform/file-writer.hpp>
#include <oxbox/platform/scratch-area.hpp>
#include <set>
#include <thread>

namespace DriveGate {
using namespace std::chrono_literals;
class DriveSession : public testing::Test {
protected:
  void ThenPartialReads(sdlrdp_file* file, std::string const& source, std::string& result) {
    for (size_t const offset : { 13u, 1048577u, 3145697u }) {
      result.resize(65536);
      auto count = sdlrdp_drive_read(handle.get(), file, offset, result.data(), result.size());
      ASSERT_GE(count, 0) << sdlrdp_last_error();
      EXPECT_EQ(result.substr(0, count), source.substr(offset, result.size()));
    }
  }
  void SetUp() override {
    auto path   = scratch.Path().string();
    auto config = Headless::LoopbackConfig(path);
    config.log_user = &logs;
    config.log      = Headless::Logs::Collect;
    sdlrdp_handle* opened = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &opened), 0) << sdlrdp_last_error();
    handle.reset(opened);
    Connect();
  }
  void Connect(char const* name = "share", bool second = false) {
    client = std::make_unique<Headless::Client>(sdlrdp_port(handle.get()), false);
    auto path = scratch.Path().string();
    Headless::ShareDrive(*client, path.c_str(), name);
    if (second) Headless::ShareDrive(*client, path.c_str(), "second");
    ASSERT_TRUE(freerdp_connect(client->Instance().get())) << logs.Text(true);
    ASSERT_TRUE(client->Until([&] {
      sdlrdp_drive value{ };
      if (sdlrdp_drive_list(handle.get(), &value, 1) != 1) return false;
      EXPECT_STREQ(value.name, name);
      drive = value.id;
      return true;
    }));
    pump = std::jthread([&](std::stop_token const& stop) {
      while (!stop.stop_requested() && client->Pump()) {
      }
    });
  }
  void GivenHeldFile() {
    Write("file", "data");
    held_file = Open("file");
    ASSERT_NE(held_file, nullptr);
    HoldRequests();
  }
  void HoldRequests() {
    pump.request_stop();
    pump.join();
    observer                  = std::make_unique<Headless::DriveObserver>(*client);
    observer->Observed().hold = true;
  }
  void ThenVideoMatches() {
    std::vector<UINT32> pixels(320uz * 200uz, 0x00446688);
    sdlrdp_rect const   damage{ 0, 0, 320, 200 };
    ASSERT_EQ(sdlrdp_present(handle.get(), pixels.data(), 1280, 320, 200, &damage, 1), 0);
    ASSERT_TRUE(client->Until([&] { return client->Matches(pixels); }));
  }
  void Disconnect() {
    pump.request_stop();
    if (pump.joinable()) pump.join();
    if (client) freerdp_disconnect(client->Instance().get());
    client.reset();
  }
  void TearDown() override {
    observer.reset();
    Disconnect();
  }
  unsigned           Logged(sdlrdp_log_level level, std::string_view text) { return logs.Count(level, text); }
  static std::string Pattern(size_t size, unsigned seed = 17) {
    std::string bytes(size, '\0');
    std::ranges::transform(std::views::iota(0uz, size), bytes.begin(),
                           [=](size_t i) { return char((i * 31 + i / 251 + seed) & 255); });
    return bytes;
  }
  void Write(std::string const& name, std::string const& bytes) {
    oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
  }
  sdlrdp_file* Open(char const* name, unsigned flags = SDLRDP_FILE_READ) {
    sdlrdp_file* file = nullptr;
    EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, name, flags, &file), 0) << sdlrdp_last_error();
    return file;
  }
  void ThenRemovedDrive() {
    std::array<sdlrdp_event, 32> events { };
    auto                         count  = sdlrdp_poll(handle.get(), events.data(), 32);
    EXPECT_TRUE(std::ranges::any_of(std::span(events.data(), count), [&](auto const& event) {
      return event.type == SDLRDP_DRIVE && !event.drive.added && event.drive.id == drive;
    }));
  }
  void ThenDriveFailure(sdlrdp_file* file, unsigned warnings) {
    sdlrdp_drive value{ };
    EXPECT_EQ(sdlrdp_drive_list(handle.get(), &value, 1), 0);
    EXPECT_EQ(sdlrdp_drive_close(handle.get(), file), -1);
    EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "") - warnings, 1u);
    EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
  }
  std::set<std::string> GivenDirectoryEntries() {
    std::set<std::string> expected;
    for (unsigned i = 0; i < 200; ++i) {
      auto name = std::to_string(i);
      expected.insert(name);
      Write("many/" + name, "data");
    }
    return expected;
  }
  sdlrdp_file*                                            held_file = nullptr;
  Headless::Logs                                          logs;
  oxbox::platform::ScratchArea                            scratch   { "drive", "sdl-rdp"    };
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle    { nullptr, sdlrdp_close };
  std::unique_ptr<Headless::Client>                       client;
  std::unique_ptr<Headless::DriveObserver>                observer;
  std::jthread                                            pump;
  unsigned                                                drive     = 0;
};
class DriveChecks : public DriveSession {
protected:
  void ThenReadRanges(sdlrdp_file* file, std::string const& source) {
    std::string result(source.size(), '\0');
    auto        start  = Headless::Clock::now();
    ASSERT_EQ(sdlrdp_drive_read(handle.get(), file, 0, result.data(), result.size()), result.size())
        << sdlrdp_last_error();
    auto seconds = std::chrono::duration<double>(Headless::Clock::now() - start).count();
    RecordProperty("read_3MiB_MBps", 3.145728 / seconds);
    EXPECT_EQ(result, source);
    ThenPartialReads(file, source, result);
  }
};
inline Backend::DrivePacket Completion(unsigned device, unsigned id, unsigned status) {
  Backend::DrivePacket response;
  response.Put(RDPDR_CTYP_CORE, 2);
  response.Put(PAKID_CORE_DEVICE_IOCOMPLETION, 2);
  response.Put(device);
  response.Put(id);
  response.Put(status);
  return response;
}
inline Backend::DrivePacket ReplyTo(Backend::DrivePacket request, unsigned status) {
  auto device = request.Get(4);
  request.Skip(4);
  return Completion(device, request.Get(4), status);
}
inline Backend::DrivePacket DeviceAnnouncement(unsigned type, unsigned id, std::span<uint8_t const> name) {
  Backend::DrivePacket packet;
  packet.Put(RDPDR_CTYP_CORE, 2);
  packet.Put(PAKID_CORE_DEVICELIST_ANNOUNCE, 2);
  packet.Put(1);
  packet.Put(type);
  packet.Put(id);
  packet.Append(std::array<uint8_t, 8>{ 'd', 'o', 's', 0, 0, 0, 0, 0 });
  packet.Put(name.size());
  packet.Append(name);
  return packet;
}
}
