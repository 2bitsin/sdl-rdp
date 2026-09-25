#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::link::detail::exceptions {
using ::Backend::operator""_hash;
using ::Backend::RuntimeFailure;

using PeerContextFailed = RuntimeFailure<"PeerContextFailed"_hash, "{} peer context failed.", std::string_view>;
}
namespace sdl_rdp::link {
using detail::exceptions::PeerContextFailed;
}
