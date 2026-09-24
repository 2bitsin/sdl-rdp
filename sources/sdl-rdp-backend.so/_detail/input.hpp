#pragma once
#include "pinned.hpp"
#include "rdp-handles.hpp"

#include <freerdp/server/ainput.h>
#include <freerdp/server/rdpei.h>
#include <optional>
#include <span>

namespace Backend {
inline constexpr unsigned InputHandleLimit = 2;
class InputEvents;
class PeerLink;
class Input : private Pinned {
public:
                      Input(PeerLink& link, InputEvents& events) noexcept;
  bool                Channels(std::span<HANDLE const> ready);
  std::span<HANDLE>   Handles(std::span<HANDLE> out) const;
  std::optional<BOOL> Activate(UINT32 channel_id);

private:
  bool        Open();
  void        InstallChannels();
  static UINT Advanced(ainput_server_context* context, UINT64 /*unused*/, UINT64 flags, INT32 x, INT32 y);
  static UINT Touch(RdpeiServerContext* context, RDPINPUT_TOUCH_EVENT const* event);
  PeerLink&                                                                    _link;
  InputEvents&                                                                 _events;
  std::unique_ptr<ainput_server_context, Releases<ainput_server_context_free>> _advanced;
  std::unique_ptr<RdpeiServerContext, Releases<rdpei_server_context_free>>     _touch;
  HANDLE                                                                       _advanced_event{ };
  std::optional<UINT32>                                                        _advanced_id;
  std::optional<UINT32>                                                        _touch_id;
  bool                                                                         _opened        { };
  bool                                                                         _advanced_ready{ };
  bool                                                                         _touch_ready   { };
};
}
