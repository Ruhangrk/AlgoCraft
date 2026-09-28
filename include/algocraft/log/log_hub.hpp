#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "algocraft/engine/engine_thread.hpp"
#include "algocraft/persistence/log_event.hpp"

namespace algocraft {

// Multi-producer → PersistenceService (T2) or optional log_drain → spdlog.
// Prefer external drain via drain_once() from T2; start() only for tests / CLI
// without a PersistenceService.
class LogHub {
public:
  static constexpr std::size_t kCapacity = 8192;

  LogHub();
  ~LogHub();

  LogHub(const LogHub&) = delete;
  LogHub& operator=(const LogHub&) = delete;

  void start();
  void stop();

  // Drain queued events into spdlog. Called by PersistenceService (T2) when no
  // dedicated log_drain thread is running. Safe to call when empty.
  void drain_once();

  // Never blocks. false = queue full (event dropped).
  [[nodiscard]] bool try_enqueue(const LogEvent& event);

  [[nodiscard]] std::uint64_t dropped() const {
    return dropped_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t written() const {
    return written_.load(std::memory_order_relaxed);
  }

private:
  void drain_loop();
  void drain_available();
  void emit(const LogEvent& event);

  std::array<LogEvent, kCapacity> slots_{};
  std::size_t head_{0};  // next write (mod capacity)
  std::size_t tail_{0};  // next read
  std::size_t size_{0};
  std::mutex mu_{};

  std::atomic<bool> stop_{true};
  EngineThread drain_{"log_drain"};

  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint64_t> written_{0};
};

}  // namespace algocraft
