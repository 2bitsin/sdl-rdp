#pragma once
#include "graphics-link.hpp"
#include "input.hpp"
#include "pinned.hpp"
#include "rdp-handles.hpp"
#include "redirection.hpp"

#include <cstdint>
#include <span>

namespace Backend {
inline constexpr unsigned ChannelHandleLimit = InputHandleLimit + RedirectionHandleLimit + GraphicsHandleLimit;
class Activation;
class DisplayControl;
class PeerLink;
auto ForgetChannelCreation(HANDLE manager) -> void;
using CreationRegistration = std::unique_ptr<void, Releases<ForgetChannelCreation>>;
class ChannelSet : private Pinned {
public:
       ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
                  Redirection& redirection, Input& input);
  auto Pump(std::span<HANDLE const> ready)                    -> bool;
  auto Handles(std::span<HANDLE> out) const                   -> std::span<HANDLE>;
  auto Created(std::uint32_t channel_id, std::int32_t status) -> bool;

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
