#pragma once
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/clipboard-channel-events.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <memory>
#include <span>
#include <string_view>

struct s_cliprdr_server_context;

namespace sdl_rdp::freerdp_facade::detail::clipboard_channel {
using sdl_rdp::freerdp_facade::ChannelManager;
using sdl_rdp::freerdp_facade::ClipboardChannelEvents;
using sdl_rdp::freerdp_facade::ClipboardFormat;
using sdl_rdp::freerdp_facade::FormatData;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Releases;

// abi: the release step of a clipboard server context, handed the context; the channel closes before it is freed.
auto ReleaseClipboard(s_cliprdr_server_context* context) noexcept -> void;
using ClipboardContext = std::unique_ptr<s_cliprdr_server_context, Releases<ReleaseClipboard>>;
inline constexpr std::string_view ClipboardChannelName{ "cliprdr" };
// The cliprdr static channel of a connection, driven by the server: no channel thread, no automatic initialisation.
class ClipboardChannel : private Pinned {
public:
       ClipboardChannel(ChannelManager& channels, ClipboardChannelEvents& events) noexcept;
  auto Open()                                                     -> bool;
  auto Pump()                                                     -> bool;
  auto Handle() const                                             -> WaitHandle;
  auto ServerCapabilities()                                       -> bool;
  auto MonitorReady()                                             -> bool;
  auto ServerFormatList(std::span<ClipboardFormat const> formats) -> bool;
  auto ServerFormatListResponse(bool accepted)                    -> bool;
  auto ServerFormatDataRequest(ClipboardFormat format)            -> bool;
  auto ServerFormatDataResponse(FormatData data)                  -> bool;

private:
  // The unit test drives the slots through the context.
  friend class ClipboardChannelProbe;
  auto Context() const -> s_cliprdr_server_context&;
  ChannelManager&         _channels;
  ClipboardChannelEvents& _events;
  ClipboardContext        _context;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::clipboard_channel::ClipboardChannel;
using detail::clipboard_channel::ClipboardChannelName;
}
