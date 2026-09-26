#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/drive/drive.hpp>
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/forward.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sdl_rdp::drive::detail::channel {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::drive::Drive;
using sdl_rdp::freerdp_facade::IrpMajor;
using sdl_rdp::freerdp_facade::IrpMinor;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::VirtualChannel;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
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
       DriveChannel(DriveChannel const&)                     = delete;
       DriveChannel(DriveChannel&&)                          = delete;
  DriveChannel(PeerLink& link, EventQueue& events, Diagnostics const& diagnostics, SessionAccess& session) noexcept;
       ~DriveChannel();
  auto operator=(DriveChannel const&)       -> DriveChannel& = delete;
  auto operator=(DriveChannel&&)            -> DriveChannel& = delete;
  auto Open()                               -> bool;
  auto Pump(Signalled const& signaled)      -> bool;
  auto Event() const                        -> std::optional<WaitHandle>;
  auto Disconnect()                         -> void;
  auto Abort(std::string const& cause)      -> void;
  auto List()                               -> std::vector<Drive>;
  auto Send(std::uint32_t drive, std::uint32_t file, IrpMajor major, DrivePacket const& body,
            IrpMinor minor = IrpMinor::None) -> std::shared_ptr<DriveRequest>;
  auto Wait(std::shared_ptr<DriveRequest> const& request, std::string const& path, bool end = false) -> DrivePacket;
  auto WaitAny(std::span<Slot const> slots) -> std::size_t;
  auto Warn(std::string_view cause) const   -> void;

private:
  struct DeviceEntry {
    std::uint32_t wire { };
    Drive         drive;
  };
  auto AnnounceDevice(std::uint32_t wire, std::string label)              -> void;
  auto Device(std::uint32_t id)                                           -> std::uint32_t;
  auto GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length, std::uint32_t version) const
      -> void;
  auto PumpAvailable()                                                    -> bool;
  auto Write(DrivePacket& packet)                                         -> void;
  auto Receive(DrivePacket& packet)                                       -> void;
  auto Capabilities()                                                     -> void;
  auto ClientCapabilities(DrivePacket& packet)                            -> void;
  auto Name(std::span<std::byte const> bytes, std::string_view dos) const -> std::string;
  auto Shutdown()                                                         -> void;
  auto CloseTransport()                                                   -> void;
  auto Fail(std::string_view cause)                                       -> void;
  auto Announce(DrivePacket& packet)                                      -> void;
  auto Remove(std::uint32_t wire)                                         -> void;
  auto Complete(DrivePacket& packet)                                      -> void;
  std::mutex                                             mutex;
  PeerLink&                                              _link;
  EventQueue&                                            _events;
  Diagnostics const&                                     _diagnostics;
  SessionAccess&                                         _session;
  VirtualChannel                                         channel;
  std::optional<WaitHandle>                              event;
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
using detail::channel::DriveChannel;
using detail::channel::DriveRequest;
using detail::channel::Slot;
}
