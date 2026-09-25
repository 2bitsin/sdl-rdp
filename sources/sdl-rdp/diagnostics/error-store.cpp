#include <sdl-rdp/diagnostics/error-store.hpp>

#include <utility>

namespace sdl_rdp::diagnostics::detail::error_store {
auto ErrorStore::Last() -> std::string const& {
  return *CallingThread().text;
}
auto ErrorStore::PublishDetached(std::string text) -> void {
  CallingThread().text = std::make_shared<std::string>(std::move(text));
}
auto ErrorStore::Publish(std::string text) -> void {
  std::scoped_lock const lock(guard);
  auto&                  slot = errors[std::this_thread::get_id()];
  if (!slot) slot = std::make_shared<std::string>();
  *slot                = std::move(text);
  CallingThread().text = slot;
}
auto ErrorStore::CallingThread() -> ErrorStore::Cursor& {
  // The handle-free C ABI needs a per-thread cursor, also after a failed open or close.
  static thread_local Cursor caller;
  return caller;
}
}
