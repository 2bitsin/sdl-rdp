#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <winpr/wtsapi.h>
#include <winpr/wtypes.h>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>

namespace Backend {
class Diagnostics;
class EventQueue;
class PeerLink;
class SessionAccess;
}
namespace sdl_rdp::drive::detail::channel {
struct DriveRequest {
  bool          done    { };
  bool          removed { };
  std::uint32_t drive   { };
  std::uint32_t status  { };
  DrivePacket   response;
};
struct Slot {
  std::shared_ptr<DriveRequest> request;
  std::size_t                   offset { };
  std::size_t                   count  { };
};
// FreeRDP 3.32 server/rdpdr.h:103 Drive* uses 32-bit offsets and a private reader; this peer owns both directions.
class DriveChannel : public std::enable_shared_from_this<DriveChannel> {
public:
       DriveChannel(DriveChannel const&)                                    = delete;
       DriveChannel(DriveChannel&&)                                         = delete;
       DriveChannel(Backend::PeerLink& link, Backend::EventQueue& events, Backend::Diagnostics const& diagnostics,
                    Backend::SessionAccess& session) noexcept;
       ~DriveChannel();
  auto operator=(DriveChannel const&)                      -> DriveChannel& = delete;
  auto operator=(DriveChannel&&)                           -> DriveChannel& = delete;
  auto Open()                                              -> bool;
  auto Pump(std::span<Backend::WaitHandle const> signaled) -> bool;
  auto Event() const                                       -> Backend::WaitHandle;
  auto Disconnect()                                        -> void;
  auto Abort(std::string const& cause)                     -> void;
  auto List(sdlrdp_drive* out, std::size_t max)            -> int;
  auto Device(std::uint32_t id)                            -> std::uint32_t;
  auto Send(std::uint32_t drive, std::uint32_t file, std::uint32_t major, DrivePacket const& body,
            std::uint32_t minor = 0) -> std::shared_ptr<DriveRequest>;
  auto Wait(std::shared_ptr<DriveRequest> const& request, std::string const& path, bool end = false) -> DrivePacket;
  auto WaitAny(std::span<Slot const> slots)                -> std::size_t;
  auto Warn(std::string const& cause) const                -> void;

private:
  struct DeviceEntry {
    std::uint32_t wire;
    sdlrdp_drive  drive;
  };
  auto AnnounceDevice(std::uint32_t wire, std::string const& label)  -> void;
  auto GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length, std::uint32_t version) const
      -> void;
  auto PumpAvailable()                                               -> bool;
  auto Write(DrivePacket& packet)                                    -> void;
  auto Receive(DrivePacket& packet)                                  -> void;
  auto Capabilities()                                                -> void;
  auto ClientCapabilities(DrivePacket& packet)                       -> void;
  auto Name(std::span<std::byte const> bytes, char const* dos) const -> std::string;
  auto Shutdown()                                                    -> void;
  auto CloseTransport()                                              -> void;
  auto Fail(std::string const& cause)                                -> void;
  auto Announce(DrivePacket& packet)                                 -> void;
  auto Remove(std::uint32_t wire)                                    -> void;
  auto Complete(DrivePacket& packet)                                 -> void;
  std::mutex                                             mutex;
  Backend::PeerLink&                                     _link;
  Backend::EventQueue&                                   _events;
  Backend::Diagnostics const&                            _diagnostics;
  Backend::SessionAccess&                                _session;
  Backend::VirtualChannel                                channel;
  Backend::WaitHandle                                    event        { };
  std::atomic<bool>                                      connected    { true };
  std::uint32_t                                          next         { 1    };
  std::uint32_t                                          client_id    { 1    };
  std::uint32_t                                          drive_version{ };
  std::map<std::uint32_t, DeviceEntry>                   devices;
  std::map<std::uint32_t, std::shared_ptr<DriveRequest>> pending;
  std::condition_variable_any                            changed;
};
}
namespace sdl_rdp::drive {
using detail::channel::DriveRequest;
using detail::channel::Slot;
using detail::channel::DriveChannel;
}
