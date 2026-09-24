#pragma once
#include "drive-wire.hpp"
#include "rdp-handles.hpp"
#include "sdl-rdp-backend.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <winpr/wtsapi.h>
#include <winpr/wtypes.h>

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
// FreeRDP 3.15 Drive* uses 32-bit offsets and a private reader; this peer owns both directions.
class DriveChannel : public std::enable_shared_from_this<DriveChannel> {
public:
                                DriveChannel(DriveChannel const&) = delete;
                                DriveChannel(DriveChannel&&)      = delete;
  DriveChannel(PeerLink& link, EventQueue& events, Diagnostics const& diagnostics, SessionAccess& session) noexcept;
                                ~DriveChannel();
  DriveChannel&                 operator = (DriveChannel const&)  = delete;
  DriveChannel&                 operator = (DriveChannel&&)       = delete;
  bool                          Open();
  bool                          Pump(std::span<HANDLE const> signaled);
  HANDLE                        Event() const { return event; }
  void                          Disconnect();
  void                          Abort(std::string const& /*cause*/);
  int                           List(sdlrdp_drive* /*out*/, unsigned /*max*/);
  unsigned                      Device(unsigned id);
  std::shared_ptr<DriveRequest> Send(unsigned drive, unsigned file, unsigned major, DrivePacket const& body,
                                     unsigned minor = 0);
  DrivePacket Wait(std::shared_ptr<DriveRequest> const& /*request*/, std::string const& path, bool end = false);
  size_t                        WaitAny(std::span<Slot const> /*slots*/);
  void                          Warn(std::string const& /*cause*/) const;

private:
  struct DeviceEntry {
    unsigned     wire;
    sdlrdp_drive drive;
  };
  void        AnnounceDevice(unsigned wire, std::string const& label);
  void GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length, unsigned version) const;
  bool        PumpAvailable();
  void        Write(DrivePacket& packet);
  void        Receive(DrivePacket& /*packet*/);
  void        Capabilities();
  void        ClientCapabilities(DrivePacket& /*packet*/);
  std::string Name(std::span<uint8_t const> /*bytes*/, char const* /*dos*/) const;
  void        Shutdown();
  void        CloseTransport();
  void        Fail(std::string const& /*cause*/);
  void        Announce(DrivePacket& /*packet*/);
  void        Remove(unsigned /*wire*/);
  void        Complete(DrivePacket& /*packet*/);
  std::mutex                                              mutex;
  PeerLink&                                               _link;
  EventQueue&                                             _events;
  Diagnostics const&                                      _diagnostics;
  SessionAccess&                                          _session;
  std::unique_ptr<void, Releases<WTSVirtualChannelClose>> channel;
  HANDLE                                                  event        { };
  std::atomic<bool>                                       connected    { true };
  unsigned                                                next         { 1    };
  unsigned                                                client_id    { 1    };
  unsigned                                                drive_version{ };
  std::map<unsigned, DeviceEntry>                         devices;
  std::map<unsigned, std::shared_ptr<DriveRequest>>       pending;
  std::condition_variable_any                             changed;
};
} // namespace Backend
struct sdlrdp_file {
public:
                     sdlrdp_file(sdlrdp_file const&) = delete;
                     sdlrdp_file(sdlrdp_file&&)      = delete;
  sdlrdp_file(std::shared_ptr<Backend::DriveChannel> source, unsigned device, unsigned file, std::string name)
      : channel { std::move(source) }, drive{ device }, wire{ file }, path{ std::move(name) } { }
                     ~sdlrdp_file();
  sdlrdp_file&       operator = (sdlrdp_file const&) = delete;
  sdlrdp_file&       operator = (sdlrdp_file&&)      = delete;
  void               Close();
  auto const&        Channel() const { return channel; }
  unsigned           Drive() const { return drive; }
  unsigned           Id() const { return wire; }
  std::string const& Path() const { return path; }

private:
  std::shared_ptr<Backend::DriveChannel> channel;
  unsigned                               drive;
  unsigned                               wire;
  std::string                            path;
  bool                                   closed { };
};
