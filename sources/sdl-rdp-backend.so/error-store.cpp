#include "_detail/error-store.hpp"

#include <utility>

namespace Backend {
char const* ErrorStore::Last() {
  return CallingThread().text->c_str();
}
void ErrorStore::Publish(ErrorStore* owner, std::string text) {
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
ErrorStore::Cursor& ErrorStore::CallingThread() {
  // The handle-free C ABI needs a per-thread cursor, also after a failed open or close.
  static thread_local Cursor caller;
  return caller;
}
}
