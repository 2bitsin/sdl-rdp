#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>

namespace sdl_rdp::configuration::detail::exceptions {
using ::Backend::operator""_hash;
using ::Backend::RuntimeFailure;

using HomeUnavailable = RuntimeFailure<"HomeUnavailable"_hash, "User home directory unavailable.">;
}
namespace sdl_rdp::configuration {
using detail::exceptions::HomeUnavailable;
}
