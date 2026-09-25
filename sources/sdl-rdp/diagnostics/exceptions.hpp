#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>

namespace sdl_rdp::diagnostics::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::RuntimeFailure;

using LogCallbackFailed = RuntimeFailure<"LogCallbackFailed"_hash, "WLog callback installation failed.">;
}

namespace sdl_rdp::diagnostics {
using detail::exceptions::LogCallbackFailed;
}
