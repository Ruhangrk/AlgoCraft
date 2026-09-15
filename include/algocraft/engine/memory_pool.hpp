#pragma once

#include <cstddef>
#include <vector>

namespace algocraft {

// N objects allocated once. acquire/release only move pointers on the free list.
template <typename T>
class MemoryPool {
public:
  explicit MemoryPool(std::size_t slot_count) {
    storage_.resize(slot_count);
    free_list_.reserve(slot_count);
    for (auto& slot : storage_) {
      free_list_.push_back(&slot);
    }
  }

  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;

  // nullptr when the pool is empty.
  [[nodiscard]] T* acquire() {
    if (free_list_.empty()) {
      return nullptr;
    }
    T* slot = free_list_.back();
    free_list_.pop_back();
    return slot;
  }

  void release(T* slot) {
    if (slot == nullptr) {
      return;
    }
    free_list_.push_back(slot);
  }

  [[nodiscard]] std::size_t available() const { return free_list_.size(); }
  [[nodiscard]] std::size_t capacity() const { return storage_.size(); }

private:
  std::vector<T> storage_{};
  std::vector<T*> free_list_{};
};

}  // namespace algocraft
