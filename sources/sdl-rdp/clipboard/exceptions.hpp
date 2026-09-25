#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <cstddef>

namespace Backend::detail::exceptions {
using UnterminatedClipboard = RuntimeFailure<"UnterminatedClipboard"_hash, "Clipboard text lacks a terminator.">;

using ClipboardTextTooLarge = ArgumentFailure<"ClipboardTextTooLarge"_hash, "Clipboard text of {} bytes is too large.",
                                              std::size_t>;

using InvalidClipboardLength = RuntimeFailure<"InvalidClipboardLength"_hash, "Invalid UTF-16LE clipboard length {}.",
                                              std::size_t>;
}
namespace Backend {
using detail::exceptions::ClipboardTextTooLarge;
using detail::exceptions::InvalidClipboardLength;
using detail::exceptions::UnterminatedClipboard;
}
