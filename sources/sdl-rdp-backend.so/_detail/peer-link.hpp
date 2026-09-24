#pragma once
#include "pinned.hpp"
#include "rdp-handles.hpp"
#include "wake-event.hpp"

#include <concepts>
#include <freerdp/freerdp.h>
#include <winpr/wtsapi.h>

namespace Backend {
using ChannelManager = std::unique_ptr<void, Releases<WTSCloseServer>>;
class PeerLink : private Pinned {
public:
  explicit      PeerLink(PeerHandle accepted);
  freerdp_peer& Client() const   noexcept;
  rdpContext&   Context() const  noexcept;
  rdpSettings&  Settings() const noexcept;
  HANDLE        Channels() const noexcept;
  int           Socket() const   noexcept;
  bool          WriteBlocked() const;
  void          Signal();
  void          Settle();
  HANDLE        Wake() const     noexcept;
  void          Invalidate()     noexcept;
  DWORD         Handles(std::invocable auto collect) {
    if (!_handle_count) _handle_count = collect();
    return _handle_count;
  }
  void Refuse(UINT32 reason);
  void Close();

private:
  PeerHandle     _client;
  int            _socket;
  WakeEvent      _wake;
  ChannelManager _channels;
  DWORD          _handle_count{ };
};
bool DynamicChannelsReady(PeerLink const& link);
bool Joined(PeerLink const& link, char const* name);
}
