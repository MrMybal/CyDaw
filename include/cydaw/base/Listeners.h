#pragma once
#include "Thread.h"
#include <algorithm>
namespace cydaw::base {
// Control/UI observers. Removal from another thread waits for active calls;
// callbacks may remove themselves, or remove a later listener, safely.
template <class Listener> class ListenerList {
  CriticalSection lock_;
  std::vector<Listener *> listeners_;

public:
  void add(Listener *listener) {
    ScopedLock hold(lock_);
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) ==
                        listeners_.end())
      listeners_.push_back(listener);
  }
  void remove(Listener *listener) {
    ScopedLock hold(lock_);
    std::erase(listeners_, listener);
  }
  void clear() {
    ScopedLock hold(lock_);
    listeners_.clear();
  }
  template <class Callback> void call(Callback callback) {
    ScopedLock hold(lock_);
    const auto snapshot = listeners_;
    for (auto *listener : snapshot)
      if (std::find(listeners_.begin(), listeners_.end(), listener) !=
          listeners_.end())
        callback(*listener);
  }
};
} // namespace cydaw::base
