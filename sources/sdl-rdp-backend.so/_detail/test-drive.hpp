#pragma once
#include "sdl-rdp-backend.h"
#include "headless-drive.hpp"
#include "test-io.hpp"
#include "test-logs.hpp"
#include <oxbox/platform/scratch-area.hpp>
#include <oxbox/platform/file-writer.hpp>
#include <gtest/gtest.h>
#include <future>
#include <thread>
#include <cstring>
#include <climits>
#include <set>
#include <mutex>
#include "drive-wire.hpp"
#include <freerdp/channels/rdpdr.h>

namespace DriveGate {
using namespace std::chrono_literals;
class Drive : public testing::Test {
protected:
  void SetUp() override
  {
    auto          path   = scratch.Path().string();
    sdlrdp_config config{ };
    config.bind           = "127.0.0.1";
    config.cert_dir       = path.c_str();
    config.width          = 320;
    config.height         = 200;
    config.log_user       = &logs;
    config.log            = Headless::Logs::Collect;
    sdlrdp_handle* opened = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &opened), 0) << sdlrdp_last_error();
    handle.reset(opened);
    Connect();
  }
  void Connect(char const* name = "share", bool second = false)
  {
    client    = std::make_unique<Headless::Client>(sdlrdp_port(handle.get()), false);
    auto path = scratch.Path().string();
    Headless::ShareDrive(*client, path.c_str(), name);
    if (second) Headless::ShareDrive(*client, path.c_str(), "second");
    ASSERT_TRUE(freerdp_connect(client->instance.get())) << logs.Text(true);
    ASSERT_TRUE(client->Until([&] {
      sdlrdp_drive value{ };
      if (sdlrdp_drive_list(handle.get(), &value, 1) != 1) return false;
      EXPECT_STREQ(value.name, name);
      drive = value.id;
      return true;
    }));
    pump = std::jthread([&](const std::stop_token& stop) { while (!stop.stop_requested() && client->Pump()) {} });
  }
  void Disconnect()
  {
    pump.request_stop();
    if (pump.joinable()) pump.join();
    if (client) freerdp_disconnect(client->instance.get());
    client.reset();
  }
  void     TearDown() override { Disconnect(); }
  unsigned Logged(sdlrdp_log_level level, std::string_view text)
  {
    return logs.Count(level, text);
  }
  static std::string Pattern(size_t size, unsigned seed = 17)
  {
    std::string bytes(size, '\0');
    for (size_t i = 0; i < size; ++i) bytes[i] = char((i * 31 + i / 251 + seed) & 255);
    return bytes;
  }
  void Write(std::string const& name, std::string const& bytes)
  {
    oxbox::platform::WriteBinaryFile(scratch.Path() / name, std::as_bytes(std::span(bytes)));
  }
  sdlrdp_file* Open(char const* name, unsigned flags = SDLRDP_FILE_READ)
  {
    sdlrdp_file* file = nullptr;
    EXPECT_EQ(sdlrdp_drive_open(handle.get(), drive, name, flags, &file), 0) << sdlrdp_last_error();
    return file;
  }
  Headless::Logs               logs;
  oxbox::platform::ScratchArea scratch{ "drive", "sdl-rdp" };
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle{ nullptr, sdlrdp_close };
  std::unique_ptr<Headless::Client> client;
  std::jthread                      pump;
  unsigned                          drive  = 0;
};
inline Backend::DrivePacket DeviceAnnouncement(unsigned type, unsigned id, std::span<uint8_t const> name)
{
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
