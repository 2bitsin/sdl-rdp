#pragma once
#include "graphics-link.hpp"
#include "input.hpp"
#include "pinned.hpp"
#include "rdp-handles.hpp"
#include "redirection.hpp"

#include <span>

namespace Backend {
inline constexpr unsigned ChannelHandleLimit = InputHandleLimit + RedirectionHandleLimit + GraphicsHandleLimit;
class Activation;
class DisplayControl;
class PeerLink;
void ForgetChannelCreation(HANDLE manager);
using CreationRegistration = std::unique_ptr<void, Releases<ForgetChannelCreation>>;
class ChannelSet : private Pinned {
public:
  ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
             Redirection& redirection, Input& input);
  bool              Pump(std::span<HANDLE const> ready);
  std::span<HANDLE> Handles(std::span<HANDLE> out) const;
  BOOL              Created(UINT32 channel_id, INT32 status);

private:
  PeerLink&            _link;
  Activation const&    _activation;
  GraphicsLink&        _graphics;
  DisplayControl&      _display;
  Redirection&         _redirection;
  Input&               _input;
  CreationRegistration _registration;
};
}
