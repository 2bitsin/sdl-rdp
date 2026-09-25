#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::peer::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::RuntimeFailure;

using PeerSetupFailed = RuntimeFailure<"PeerSetupFailed"_hash, "Peer {} failed.", std::string_view>;
}

namespace sdl_rdp::peer {
using detail::exceptions::PeerSetupFailed;
}
