#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::session::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::ArgumentFailure;
using sdl_rdp::utilities::LogicFailure;
using std::string_view;

using AudioDeviceState = LogicFailure<"AudioDeviceState"_hash, "Audio device is {}.", string_view>;
using AddressNotIpv4   = ArgumentFailure<"AddressNotIpv4"_hash, "Listener address {} is not IPv4.", string_view>;
}

namespace sdl_rdp::session {
using detail::exceptions::AddressNotIpv4;
using detail::exceptions::AudioDeviceState;
}
