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
  auto Channels(std::span<HANDLE const> ready) -> bool;
  auto Handles(std::span<HANDLE> out) const    -> std::span<HANDLE>;
  auto Activate(UINT32 channel_id)             -> std::optional<BOOL>;

private:
  auto        Open()                                                                                      -> bool;
  auto        InstallChannels()                                                                           -> void;
  static auto Advanced(ainput_server_context* context, UINT64 /*unused*/, UINT64 flags, INT32 x, INT32 y) -> UINT;
  static auto Touch(RdpeiServerContext* context, RDPINPUT_TOUCH_EVENT const* event)                       -> UINT;
  using AdvancedInputContext = std::unique_ptr<ainput_server_context, Releases<ainput_server_context_free>>;
  using TouchContext         = std::unique_ptr<RdpeiServerContext, Releases<rdpei_server_context_free>>;
  PeerLink&             _link;
  InputEvents&          _events;
  AdvancedInputContext  _advanced;
  TouchContext          _touch;
  HANDLE                _advanced_event{ };
  std::optional<UINT32> _advanced_id;
  std::optional<UINT32> _touch_id;
  bool                  _opened        { };
  bool                  _advanced_ready{ };
  bool                  _touch_ready   { };
};
}
