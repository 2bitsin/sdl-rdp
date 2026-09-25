#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/input/input.hpp>
#include <sdl-rdp/peer/redirection.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/graphics-link.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Backend {
inline constexpr std::size_t ChannelHandleLimit = InputHandleLimit + RedirectionHandleLimit + GraphicsHandleLimit;
class Activation;
class DisplayControl;
class PeerLink;
auto ForgetChannelCreation(WaitHandle manager) -> void;
using CreationRegistration = std::unique_ptr<void, Releases<ForgetChannelCreation>>;
class ChannelSet : private Pinned {
public:
       ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
                  Redirection& redirection, Input& input);
  auto Pump(std::span<WaitHandle const> ready)  -> bool;
  auto Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle>;

private:
  class Callbacks;
  auto Created(std::uint32_t channel_id, std::int32_t status) -> bool;
  auto FailureSource() const noexcept                         -> GraphicsLink const&;
  PeerLink&            _link;
  Activation const&    _activation;
  GraphicsLink&        _graphics;
  DisplayControl&      _display;
  Redirection&         _redirection;
  Input&               _input;
  CreationRegistration _registration;
};
}
