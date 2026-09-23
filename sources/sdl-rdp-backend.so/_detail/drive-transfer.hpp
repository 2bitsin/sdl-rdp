#pragma once
#include "contract.hpp"
#include "drive.hpp"

#include <array>
#include <cstring>
#include <freerdp/channels/rdpdr.h>
#include <winpr/nt.h>

namespace Backend {
using utilities::Expects;
template <class Byte> std::shared_ptr<DriveRequest> Submit(sdlrdp_file& file, uint64_t offset, std::span<Byte> bytes) {
  Expects(!bytes.empty(), "transfer chunk is nonempty");
  Expects(bytes.size() <= UINT32_MAX, "transfer length fits the wire field");
  constexpr bool     write                = std::is_const_v<Byte>;
  DrivePacket        packet;
  constexpr unsigned padding_after_offset = 20;
  packet.Put(bytes.size());
  packet.Put(offset, 8);
  packet.Zero(padding_after_offset);
  if constexpr (write) packet.Append(bytes);
  return file.Channel()->Send(file.Drive(), file.Id(), write ? IRP_MJ_WRITE : IRP_MJ_READ, packet);
}
template <class Byte>
size_t Finish(sdlrdp_file& file, std::shared_ptr<DriveRequest> const& request, std::span<Byte> bytes) {
  constexpr bool write    = std::is_const_v<Byte>;
  auto           response = file.Channel()->Wait(request, file.Path(), !write);
  auto           received = response.Get(4);
  if (received > bytes.size()) response.Invalid("Drive returned oversized transfer.");
  if constexpr (!write) {
    if (received > response.Bytes().size() - response.Position()) response.Invalid("Truncated drive read.");
    std::memcpy(bytes.data(), response.Bytes().data() + response.Position(), received);
  }
  return received;
}
struct TransferProgress {
  std::size_t        submitted = 0;
  std::size_t        active    = 0;
  std::size_t        limit;
  std::exception_ptr failure;
};
template <class Byte>
void SubmitSlot(sdlrdp_file& file, uint64_t offset, std::span<Byte> bytes, TransferProgress& progress, Slot& slot) {
  if (progress.failure || progress.submitted >= progress.limit) return;
  Expects(!slot.request, "submission slot is empty");
  slot.offset        =  progress.submitted;
  slot.count         =  std::min(std::size_t{ 65536 }, bytes.size() - progress.submitted);
  slot.request       =  Submit(file, offset + progress.submitted, bytes.subspan(progress.submitted, slot.count));
  progress.submitted += slot.count;
  ++progress.active;
}
template <class Byte>
void FinishSlot(sdlrdp_file& file, std::span<Byte> bytes, TransferProgress& progress, Slot& slot) {
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
template <class Byte> int Transfer(sdlrdp_file* file, uint64_t offset, Byte* buffer, size_t size) {
  std::array<Slot, 8> slots   {               };
  TransferProgress    progress{ .limit = size };
  auto                bytes    = std::span(buffer, size);
  std::ranges::for_each(slots, [&](Slot& slot) { SubmitSlot(*file, offset, bytes, progress, slot); });
  while (progress.active) {
    auto& slot = slots[file->Channel()->WaitAny(slots)];
    FinishSlot(*file, bytes, progress, slot);
    SubmitSlot(*file, offset, bytes, progress, slot);
  }
  if (progress.failure) std::rethrow_exception(progress.failure);
  return int(progress.limit);
}
}
