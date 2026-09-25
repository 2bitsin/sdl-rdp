#include "counting-heap.hpp"

#include <algorithm>
#include <cstddef>
#include <new>

// glibc's allocator entry points: the executable's definitions below interpose every library and forward here.
extern "C" auto __libc_malloc(std::size_t size) noexcept -> void*;
extern "C" auto __libc_calloc(std::size_t nmemb, std::size_t size) noexcept -> void*;
extern "C" auto __libc_realloc(void* ptr, std::size_t size) noexcept -> void*;
extern "C" auto __libc_memalign(std::size_t alignment, std::size_t size) noexcept -> void*;
extern "C" auto __libc_free(void* block) noexcept -> void;

namespace sdl_rdp::integration::allocations_test::detail::replacements {
using sdl_rdp::integration::allocations_test::CountingHeap;
namespace {
template <auto VCount, auto VNext, typename... VArguments> auto Counted(VArguments... arguments) noexcept -> void* {
  (CountingHeap::Shared().*VCount)();
  return VNext(arguments...);
}
auto Required(void* block) -> void* {
  if (block == nullptr) throw std::bad_alloc();
  return block;
}
}
// The C and C++ standards fix these signatures, pointers included; C linkage names the global symbol.
extern "C" [[gnu::visibility("default")]] auto malloc(std::size_t size) noexcept -> void* {
  return Counted<&CountingHeap::CountHeap, __libc_malloc>(size);
}
extern "C" [[gnu::visibility("default")]] auto calloc(std::size_t nmemb, std::size_t size) noexcept -> void* {
  return Counted<&CountingHeap::CountHeap, __libc_calloc>(nmemb, size);
}
extern "C" [[gnu::visibility("default")]] auto realloc(void* ptr, std::size_t size) noexcept -> void* {
  return Counted<&CountingHeap::CountHeap, __libc_realloc>(ptr, size);
}
}

using sdl_rdp::integration::allocations_test::CountingHeap;
using sdl_rdp::integration::allocations_test::detail::replacements::Counted;
using sdl_rdp::integration::allocations_test::detail::replacements::Required;

auto operator new(std::size_t size) -> void* {
  return Required(Counted<&CountingHeap::CountNew, __libc_malloc>(std::max(size, std::size_t{ 1 })));
}
auto operator new(std::size_t size, std::align_val_t alignment) -> void* {
  auto const align = static_cast<std::size_t>(alignment);
  return Required(Counted<&CountingHeap::CountNew, __libc_memalign>(align, std::max(size, align)));
}
auto operator delete(void* block) noexcept -> void {
  __libc_free(block);
}
auto operator delete(void* block, std::size_t /*size*/) noexcept -> void {
  __libc_free(block);
}
auto operator delete(void* block, std::align_val_t /*alignment*/) noexcept -> void {
  __libc_free(block);
}
auto operator delete(void* block, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept -> void {
  __libc_free(block);
}
