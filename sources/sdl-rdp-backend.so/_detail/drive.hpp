#pragma once
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
  explicit DriveChannel(Peer&);
  ~DriveChannel();
  bool Open();
  bool Pump();
  HANDLE Event() const { return event; }
  void Disconnect();
  void Abort(std::string const&);
  int List(sdlrdp_drive*, unsigned);
  unsigned Device(unsigned id);
  std::shared_ptr<DriveRequest> Send(unsigned drive, unsigned file, unsigned major,
                                     DrivePacket body, unsigned minor = 0);
  DrivePacket Wait(std::shared_ptr<DriveRequest> const&, std::string const& path, bool end = false);
  size_t WaitAny(std::span<Slot const>);
  void Warn(std::string const&) const;
private:
  std::mutex mutex;
  void Write(DrivePacket const&);
  void Receive(DrivePacket&);
  void Capabilities();
  void ClientCapabilities(DrivePacket&);
  std::string Name(std::span<uint8_t const>, char const*);
  void Shutdown();
  void CloseTransport();
  void Fail(std::string const&);
  void Announce(DrivePacket&);
  void Remove(unsigned);
  void Complete(DrivePacket&);
  Peer& peer;
  HANDLE channel = nullptr, event = nullptr;
  std::atomic<bool> connected = true;
  unsigned next = 1, client_id = 1, drive_version = 0;
  struct DeviceEntry { unsigned wire; sdlrdp_drive drive; };
  std::map<unsigned, DeviceEntry> devices;
  std::map<unsigned, std::shared_ptr<DriveRequest>> pending;
  std::condition_variable_any changed;
};
}
struct sdlrdp_file {
  std::shared_ptr<Backend::DriveChannel> channel;
  unsigned drive, wire;
  std::string path;
  bool closed = false;
  void Close();
  ~sdlrdp_file();
};
