#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::clipboard_channel_events {
// The standard clipboard format ids the server speaks (MS-RDPECLIP 1.3.1.2); a client may offer any other id.
enum class ClipboardFormat : std::uint32_t { Text = 1, UnicodeText = 13 };
// A format data response's payload, absent when the response reports failure.
using FormatData = std::optional<std::span<std::byte const>>;
// What a clipboard channel's client PDUs report; false: the handler's reply was refused.
class ClipboardChannelEvents : public FailureSink {
public:
  virtual auto ClientFormatList(std::span<ClipboardFormat const> formats) -> bool = 0;
  virtual auto ClientFormatDataRequest(ClipboardFormat format)            -> bool = 0;
  virtual auto ClientFormatDataResponse(FormatData data)                  -> bool = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::clipboard_channel_events::ClipboardChannelEvents;
using detail::clipboard_channel_events::ClipboardFormat;
using detail::clipboard_channel_events::FormatData;
}
