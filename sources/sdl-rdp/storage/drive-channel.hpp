#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/storage/drive-packet.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <winpr/wtsapi.h>
#include <winpr/wtypes.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <span>

namespace Backend {
class Diagnostics;
class EventQueue;
class PeerLink;
class SessionAccess;
struct DriveRequest {
  bool        done    { };
  bool        removed { };
  unsigned    drive   { };
  uint32_t    status  { };
  DrivePacket response;
};
struct Slot {
  std::shared_ptr<DriveRequest> request;
  size_t                        offset { };
  size_t                        count  { };
};
// FreeRDP 3.32 server/rdpdr.h:103 Drive* uses 32-bit offsets and a private reader; this peer owns both directions.
class DriveChannel : public std::enable_shared_from_this<DriveChannel> {
public:
       DriveChannel(DriveChannel const&)                              = delete;
       DriveChannel(DriveChannel&&)                                   = delete;
  DriveChannel(PeerLink& link, EventQueue& events, Diagnostics const& diagnostics, SessionAccess& session) noexcept;
       ~DriveChannel();
  auto operator=(DriveChannel const&)                -> DriveChannel& = delete;
  auto operator=(DriveChannel&&)                     -> DriveChannel& = delete;
  auto Open()                                        -> bool;
  auto Pump(std::span<HANDLE const> signaled)        -> bool;
  auto Event() const                                 -> HANDLE;
  auto Disconnect()                                  -> void;
  auto Abort(std::string const& /*cause*/)           -> void;
  auto List(sdlrdp_drive* /*out*/, unsigned /*max*/) -> int;
  auto Device(unsigned id)                           -> unsigned;
  auto Send(unsigned drive, unsigned file, unsigned major, DrivePacket const& body, unsigned minor = 0)
      -> std::shared_ptr<DriveRequest>;
  auto Wait(std::shared_ptr<DriveRequest> const& /*request*/, std::string const& path, bool end = false) -> DrivePacket;
  auto WaitAny(std::span<Slot const> /*slots*/)      -> size_t;
  auto Warn(std::string const& /*cause*/) const      -> void;

private:
  struct DeviceEntry {
    unsigned     wire;
    sdlrdp_drive drive;
  };
  auto AnnounceDevice(unsigned wire, std::string const& label)             -> void;
  auto GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length, unsigned version) const
      -> void;
  auto PumpAvailable()                                                     -> bool;
  auto Write(DrivePacket& packet)                                          -> void;
  auto Receive(DrivePacket& /*packet*/)                                    -> void;
  auto Capabilities()                                                      -> void;
  auto ClientCapabilities(DrivePacket& /*packet*/)                         -> void;
  auto Name(std::span<uint8_t const> /*bytes*/, char const* /*dos*/) const -> std::string;
  auto Shutdown()                                                          -> void;
  auto CloseTransport()                                                    -> void;
  auto Fail(std::string const& /*cause*/)                                  -> void;
  auto Announce(DrivePacket& /*packet*/)                                   -> void;
  auto Remove(unsigned /*wire*/)                                           -> void;
  auto Complete(DrivePacket& /*packet*/)                                   -> void;
  std::mutex                                        mutex;
  PeerLink&                                         _link;
  EventQueue&                                       _events;
  Diagnostics const&                                _diagnostics;
  SessionAccess&                                    _session;
  VirtualChannel                                    channel;
  HANDLE                                            event        { };
  std::atomic<bool>                                 connected    { true };
  unsigned                                          next         { 1    };
  unsigned                                          client_id    { 1    };
  unsigned                                          drive_version{ };
  std::map<unsigned, DeviceEntry>                   devices;
  std::map<unsigned, std::shared_ptr<DriveRequest>> pending;
  std::condition_variable_any                       changed;
};
} // namespace Backend
