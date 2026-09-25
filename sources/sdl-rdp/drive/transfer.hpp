#pragma once
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sdl_rdp::drive::detail::transfer {
using utilities::Expects;
template <class Byte> auto Submit(sdlrdp_file& file, std::uint64_t offset, std::span<Byte> bytes)
    -> std::shared_ptr<DriveRequest> {
  Expects(!bytes.empty(), "transfer chunk is nonempty");
  Expects(bytes.size() <= UINT32_MAX, "transfer length fits the wire field");
  constexpr bool        write                = std::is_const_v<Byte>;
  DrivePacket           packet;
  constexpr std::size_t padding_after_offset = 20;
  packet.Write(Backend::Narrowed<std::uint32_t>(bytes.size()));
  packet.Write(offset);
  packet.Zero(padding_after_offset);
  if constexpr (write) packet.Append(bytes);
  return file.Channel()->Send(file.Drive(), file.Id(),
                              write ? freerdp_facade::IrpMajor::Write : freerdp_facade::IrpMajor::Read, packet);
}
template <class Byte>
auto Finish(sdlrdp_file& file, std::shared_ptr<DriveRequest> const& request, std::span<Byte> bytes) -> std::size_t {
  constexpr bool write    = std::is_const_v<Byte>;
  auto           response = file.Channel()->Wait(request, file.Path(), !write);
  auto           received = response.Read<std::uint32_t>();
  if (received > bytes.size()) response.Invalid("oversized transfer");
  if constexpr (!write) {
    if (received > response.Bytes().size() - response.Position()) response.Invalid("truncated read");
    std::memcpy(bytes.data(), response.Bytes().data() + response.Position(), received);
  }
  return received;
}
struct TransferProgress {
  std::size_t        submitted{ };
  std::size_t        active   { };
  std::size_t        limit;
  std::exception_ptr failure;
};
template <class Byte>
auto SubmitSlot(sdlrdp_file& file, std::uint64_t offset, std::span<Byte> bytes, TransferProgress& progress, Slot& slot)
    -> void {
  if (progress.failure || progress.submitted >= progress.limit) return;
  Expects(!slot.request, "submission slot is empty");
  slot.offset        =  progress.submitted;
  slot.count         =  std::min(std::size_t{ 65536 }, bytes.size() - progress.submitted);
  slot.request       =  Submit(file, offset + progress.submitted, bytes.subspan(progress.submitted, slot.count));
  progress.submitted += slot.count;
  ++progress.active;
}
template <class Byte>
auto FinishSlot(sdlrdp_file& file, std::span<Byte> bytes, TransferProgress& progress, Slot& slot) -> void {
  try {
    auto received = Finish(file, slot.request, bytes.subspan(slot.offset, slot.count));
    if (received != slot.count) progress.limit = std::min(progress.limit, slot.offset + received);
  } catch (MalformedResponse const&) {
    throw;
  } catch (...) {
    if (!progress.failure) progress.failure = std::current_exception();
  }
  slot.request.reset();
  --progress.active;
}
template <class Byte> auto Transfer(sdlrdp_file& file, std::uint64_t offset, std::span<Byte> bytes) -> int {
  std::array<Slot, 8> slots   { };
  TransferProgress    progress{ .limit = bytes.size() };
  std::ranges::for_each(slots, [&](Slot& slot) { SubmitSlot(file, offset, bytes, progress, slot); });
  while (progress.active) {
    auto& slot = slots[file.Channel()->WaitAny(slots)];
    FinishSlot(file, bytes, progress, slot);
    SubmitSlot(file, offset, bytes, progress, slot);
  }
  if (progress.failure) std::rethrow_exception(progress.failure);
  return Backend::Narrowed<int>(progress.limit);
}
}
namespace sdl_rdp::drive {
using detail::transfer::Submit;
using detail::transfer::Finish;
using detail::transfer::TransferProgress;
using detail::transfer::SubmitSlot;
using detail::transfer::FinishSlot;
using detail::transfer::Transfer;
}
