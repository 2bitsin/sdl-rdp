#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sdl3::rdp::detail::exceptions {
using ::Backend::operator""_hash;
using ::Backend::ArgumentFailure;
using ::Backend::LogicFailure;
using ::Backend::RuntimeFailure;
using std::string_view;

using RelayedFailure   = RuntimeFailure<"RelayedFailure"_hash, "{}", string_view>;
using NoWindow         = LogicFailure<"NoWindow"_hash, "RDP video has no window">;
using TooManyDrives    = LogicFailure<"TooManyDrives"_hash, "Too many RDP drives: {}", std::size_t>;
using DriveUnavailable = RuntimeFailure<"DriveUnavailable"_hash, "RDP drive unavailable: {}", string_view>;
using InvalidFilePath  = ArgumentFailure<"InvalidFilePath"_hash, "Invalid RDP file path">;
using InvalidFileMode  = ArgumentFailure<"InvalidFileMode"_hash, "Invalid RDP file mode '{}'", string_view>;
using UnreadableIni    = RuntimeFailure<"UnreadableIni"_hash, "Could not read RDP settings file {}", string_view>;
using LeadTooLong      = RuntimeFailure<"LeadTooLong"_hash, "RDP audio lead must be below the audio latency window">;

using AbiMismatch = RuntimeFailure<"AbiMismatch"_hash, "RDP backend ABI version mismatch (found {}, expected {})",
                                   std::uint32_t, std::uint32_t>;

using UnknownName = RuntimeFailure<"UnknownName"_hash, "Invalid {} '{}'; valid names: {}", string_view, string_view,
                                   string_view>;

using InvalidAspect = RuntimeFailure<
    "InvalidAspect"_hash, "Invalid RDP aspect '{}': expected two positive whole numbers as N:D", string_view>;

using IntegerOutOfRange = RuntimeFailure<"IntegerOutOfRange"_hash,
                                         "Invalid {} '{}': expected a whole number from {} to {}", string_view,
                                         string_view, int, int>;
}
namespace sdl3::rdp {
using detail::exceptions::AbiMismatch;
using detail::exceptions::DriveUnavailable;
using detail::exceptions::IntegerOutOfRange;
using detail::exceptions::InvalidAspect;
using detail::exceptions::InvalidFileMode;
using detail::exceptions::InvalidFilePath;
using detail::exceptions::LeadTooLong;
using detail::exceptions::NoWindow;
using detail::exceptions::RelayedFailure;
using detail::exceptions::TooManyDrives;
using detail::exceptions::UnknownName;
using detail::exceptions::UnreadableIni;
}
