#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::settings::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::RuntimeFailure;
using std::string_view;

using InvalidSettingsFile = RuntimeFailure<"InvalidSettingsFile"_hash, "Invalid RDP settings file {}: {}", string_view,
                                           string_view>;
using AmbiguousSettings = RuntimeFailure<"AmbiguousSettings"_hash, "RDP settings files {} and {} are both present",
                                         string_view, string_view>;
using UnknownSettingsKey = RuntimeFailure<"UnknownSettingsKey"_hash, "unknown key '{}'", string_view>;
using UnknownSettingsFormat = RuntimeFailure<"UnknownSettingsFormat"_hash, "no serialization format reads '{}'",
                                             string_view>;
using InvalidSettingValue = RuntimeFailure<"InvalidSettingValue"_hash, "'{}' is not {}", string_view, string_view>;
}

namespace sdl_rdp::settings {
using detail::exceptions::AmbiguousSettings;
using detail::exceptions::InvalidSettingValue;
using detail::exceptions::InvalidSettingsFile;
using detail::exceptions::UnknownSettingsFormat;
using detail::exceptions::UnknownSettingsKey;
}
