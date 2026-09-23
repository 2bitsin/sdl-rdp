#pragma once
#include <span>
#include "sdl-rdp-backend.h"
#include "drive-wire.hpp"
#include <winpr/wtypes.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>

namespace Backend {
class Peer;
struct DriveRequest {
  bool done = false, removed = false;
  unsigned drive = 0;
  uint32_t status = 0;
  DrivePacket response;
};
struct Slot {
  std::shared_ptr<DriveRequest> request;
  size_t offset = 0, count = 0;
};
// FreeRDP 3.15 Drive* uses 32-bit offsets and a private reader; this peer owns both directions.
class DriveChannel : public std::enable_shared_from_this<DriveChannel> {
public:
  DriveChannel(DriveChannel const &) = delete;
  DriveChannel &operator=(DriveChannel const &) = delete;
  DriveChannel(DriveChannel &&) = delete;
  DriveChannel &operator=(DriveChannel &&) = delete;
  explicit DriveChannel(Peer & /*value*/);
  ~DriveChannel();
  bool Open();
  bool Pump(std::span<HANDLE const> signaled);
  HANDLE Event() const { return event; }
  void Disconnect();
  void Abort(std::string const & /*cause*/);
  int List(sdlrdp_drive * /*out*/, unsigned /*max*/);
  unsigned Device(unsigned id);
  std::shared_ptr<DriveRequest> Send(unsigned drive, unsigned file, unsigned major,
                                     DrivePacket body, unsigned minor = 0);
  DrivePacket Wait(std::shared_ptr<DriveRequest> const & /*request*/, std::string const &path, bool end = false);
  size_t WaitAny(std::span<Slot const> /*slots*/);
  void Warn(std::string const & /*cause*/) const;

private:
  struct DeviceEntry {
    unsigned wire;
    sdlrdp_drive drive;
  };
  void Write(DrivePacket &packet);
  void Receive(DrivePacket & /*packet*/);
  void Capabilities();
  void ClientCapabilities(DrivePacket & /*packet*/);
  std::string Name(std::span<uint8_t const> /*bytes*/, char const * /*dos*/) const;
  void Shutdown();
  void CloseTransport();
  void Fail(std::string const & /*cause*/);
  void Announce(DrivePacket & /*packet*/);
  void Remove(unsigned /*wire*/);
  void Complete(DrivePacket & /*packet*/);
  std::mutex mutex;
  Peer &peer;
  HANDLE channel = nullptr, event = nullptr;
  std::atomic<bool> connected = true;
  unsigned next = 1, client_id = 1, drive_version = 0;
  std::map<unsigned, DeviceEntry> devices;
  std::map<unsigned, std::shared_ptr<DriveRequest>> pending;
  std::condition_variable_any changed;
};
} // namespace Backend
struct sdlrdp_file {
public:
  sdlrdp_file(sdlrdp_file const &) = delete;
  sdlrdp_file &operator=(sdlrdp_file const &) = delete;
  sdlrdp_file(sdlrdp_file &&) = delete;
  sdlrdp_file &operator=(sdlrdp_file &&) = delete;
  sdlrdp_file(std::shared_ptr<Backend::DriveChannel> source, unsigned device, unsigned file, std::string name)
      : channel{std::move(source)}, drive{device}, wire{file}, path{std::move(name)} {}
  ~sdlrdp_file();
  void Close();
  std::shared_ptr<Backend::DriveChannel> channel;
  unsigned drive, wire;
  std::string path;
  bool closed = false;
};
