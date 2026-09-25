#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sdl_rdp::drive::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::ArgumentFailure;
using sdl_rdp::utilities::RuntimeFailure;
using std::string_view;

using MalformedResponse      = RuntimeFailure<"MalformedResponse"_hash, "Malformed drive response: {}.", string_view>;
using InvalidDriveName       = RuntimeFailure<"InvalidDriveName"_hash, "Invalid drive name: {}.", string_view>;
using DriveChannelFailed     = RuntimeFailure<"DriveChannelFailed"_hash, "Drive channel {} failed.", string_view>;
using TransportDisconnected  = RuntimeFailure<"TransportDisconnected"_hash, "Drive transport disconnected.">;
using CompletionIdsExhausted = RuntimeFailure<"CompletionIdsExhausted"_hash, "Drive completion ids exhausted.">;
using PeerDisconnected       = RuntimeFailure<"PeerDisconnected"_hash, "Drive peer disconnected: {}", string_view>;
using DriveRemoved           = RuntimeFailure<"DriveRemoved"_hash, "Drive removed: {}", string_view>;
using NoDriveChannel         = RuntimeFailure<"NoDriveChannel"_hash, "Drive peer disconnected or no drives shared.">;

using InvalidOpenFlags = ArgumentFailure<"InvalidOpenFlags"_hash, "Invalid drive open flags 0x{:x}: {}.", std::uint32_t,
                                         string_view>;

using EntryNameTooLong = RuntimeFailure<
    "EntryNameTooLong"_hash, "Drive entry name of {} bytes exceeds ABI capacity {}.", std::size_t, std::size_t>;

using ShortCapability = RuntimeFailure<"ShortCapability"_hash, "Drive capability {} of {} bytes is too short.",
                                       std::uint16_t, std::size_t>;

using StatusFailure = RuntimeFailure<"StatusFailure"_hash, "Drive '{}' failed: {} (0x{:08x})", string_view, string_view,
                                     std::uint32_t>;
}

namespace sdl_rdp::drive {
using detail::exceptions::CompletionIdsExhausted;
using detail::exceptions::DriveChannelFailed;
using detail::exceptions::DriveRemoved;
using detail::exceptions::EntryNameTooLong;
using detail::exceptions::InvalidDriveName;
using detail::exceptions::InvalidOpenFlags;
using detail::exceptions::MalformedResponse;
using detail::exceptions::NoDriveChannel;
using detail::exceptions::PeerDisconnected;
using detail::exceptions::ShortCapability;
using detail::exceptions::StatusFailure;
using detail::exceptions::TransportDisconnected;
}
