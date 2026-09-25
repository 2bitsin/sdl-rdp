#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::configuration::detail::exceptions {
using ::Backend::operator""_hash;
using ::Backend::ArgumentFailure;
using ::Backend::RuntimeFailure;

using HomeUnavailable = RuntimeFailure<"HomeUnavailable"_hash, "User home directory unavailable.">;
using InvalidChoice   = ArgumentFailure<"InvalidChoice"_hash, "Invalid {}.", std::string_view>;
}
namespace sdl_rdp::configuration {
using detail::exceptions::HomeUnavailable;
using detail::exceptions::InvalidChoice;
}
