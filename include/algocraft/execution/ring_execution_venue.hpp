#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/execution/execution_venue.hpp"
#include "algocraft/execution/live_order_request.hpp"
#include "algocraft/log/log.hpp"

namespace algocraft {

// T0 → OrderOutRing. Always returns nullopt; fills arrive later on FillInRing.
template <std::size_t Capacity>
class RingExecutionVenue final : public ExecutionVenue {
public:
  explicit RingExecutionVenue(SpscRing<LiveOrderRequest, Capacity>& order_out,
                              std::atomic<std::uint64_t>* orders_dropped = nullptr)
      : order_out_(&order_out), orders_dropped_(orders_dropped) {}

  std::optional<FillEvent> submit(const OrderIntent& intent, const BarEvent& bar, TradingMode mode,
                                  Capital cash, Quantity position, Price avg_entry = {},
                                  ContainerId container_id = {},
                                  WorkbookId workbook_id = {}) override {
    LiveOrderRequest req{};
    req.intent = intent;
    req.bar = bar;
    req.mode = mode;
    req.cash = cash;
    req.position = position;
    req.avg_entry = avg_entry;
    req.container_id = container_id;
    req.workbook_id = workbook_id;
    if (!order_out_->try_push(req)) {
      const auto n =
          orders_dropped_ != nullptr
              ? orders_dropped_->fetch_add(1, std::memory_order_relaxed) + 1
              : 1;
      AC_LOG_WARN("live_order_out_ring_full dropped={}", n);
    }
    return std::nullopt;
  }

private:
  SpscRing<LiveOrderRequest, Capacity>* order_out_{nullptr};
  std::atomic<std::uint64_t>* orders_dropped_{nullptr};
};

}  // namespace algocraft
