#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "algocraft/engine/dummy_event.hpp"
#include "algocraft/engine/engine_thread.hpp"
#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/persistence/async_logger.hpp"
#include "algocraft/persistence/log_event.hpp"

namespace algocraft {

// Phase 0 wiring check: five threads, SPSC rings only, dummy payload.
//
//   (test / main) --market_data--> Thread 0 --routing--> Thread 1 --command--> Thread 0
//                         Thread 0 --order_out--> Thread 3 --fill_in--> Thread 0
//                         Thread 0 / 1 / 3 --persist* / log--> Thread 2
//                         Thread 4 idle until stop
class Phase0Runtime {
public:
  static constexpr std::size_t kRingCapacity = 1024;

  using EventRing = SpscRing<DummyEvent, kRingCapacity>;
  using LogRing = AsyncLogger::Ring;

  Phase0Runtime();
  ~Phase0Runtime();

  Phase0Runtime(const Phase0Runtime&) = delete;
  Phase0Runtime& operator=(const Phase0Runtime&) = delete;

  void start();
  void stop();

  [[nodiscard]] EventRing& market_data_ring() { return market_data_; }
  [[nodiscard]] LogRing& log_ring() { return log_ring_; }
  [[nodiscard]] AsyncLogger& logger() { return logger_; }

  [[nodiscard]] std::uint64_t bars_processed() const {
    return bars_processed_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t fills_processed() const {
    return fills_processed_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t commands_processed() const {
    return commands_processed_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t persist_events() const {
    return persist_events_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t logs_written() const {
    return logs_written_.load(std::memory_order_relaxed);
  }

private:
  void hot_path_loop();
  void routing_loop();
  void persistence_loop();
  void execution_loop();
  void api_loop();

  std::atomic<bool> stop_workers_{true};
  std::atomic<bool> stop_persist_{true};

  EventRing market_data_{};
  EventRing order_out_{};
  EventRing fill_in_{};
  EventRing routing_{};
  EventRing command_{};
  EventRing persist0_{};  // Thread 0 -> Thread 2
  EventRing persist1_{};  // Thread 1 -> Thread 2
  EventRing persist3_{};  // Thread 3 -> Thread 2
  LogRing log_ring_{};    // Thread 0 -> Thread 2

  AsyncLogger logger_{log_ring_};

  EngineThread thread0_{"thread0_hot_path"};
  EngineThread thread1_{"thread1_routing"};
  EngineThread thread2_{"thread2_persistence"};
  EngineThread thread3_{"thread3_execution"};
  EngineThread thread4_{"thread4_api"};

  std::atomic<std::uint64_t> bars_processed_{0};
  std::atomic<std::uint64_t> fills_processed_{0};
  std::atomic<std::uint64_t> commands_processed_{0};
  std::atomic<std::uint64_t> persist_events_{0};
  std::atomic<std::uint64_t> logs_written_{0};
};

}  // namespace algocraft
