#include <sdl-rdp/core/error-store.hpp>

#include <utility>

namespace Backend {
auto ErrorStore::Last() -> char const* {
  return CallingThread().text->c_str();
}
auto ErrorStore::Publish(ErrorStore* owner, std::string text) -> void {
  auto& caller = CallingThread();
  if (owner) {
    std::scoped_lock const lock(owner->guard);
    auto&                  slot = owner->errors[std::this_thread::get_id()];
    if (!slot) slot = std::make_shared<std::string>();
    *slot       = std::move(text);
    caller.text = slot;
  } else {
    caller.text = std::make_shared<std::string>(std::move(text));
  }
}
auto ErrorStore::CallingThread() -> ErrorStore::Cursor& {
  // The handle-free C ABI needs a per-thread cursor, also after a failed open or close.
  static thread_local Cursor caller;
  return caller;
}
}
