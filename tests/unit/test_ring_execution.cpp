#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/execution/live_order_request.hpp"
#include "algocraft/execution/ring_execution_venue.hpp"
#include "algocraft/execution/simulated_exchange.hpp"
#include "algocraft/strategies/make_intent.hpp"

TEST(RingExecutionVenue, OrderOutFillInRoundTrip) {
  constexpr std::size_t kCap = 64;
  algocraft::SpscRing<algocraft::LiveOrderRequest, kCap> order_out;
  algocraft::SpscRing<algocraft::FillEvent, kCap> fill_in;
  std::atomic<std::uint64_t> orders_dropped{0};
  std::atomic<std::uint64_t> fills_dropped{0};
  std::atomic<bool> stop{false};

  algocraft::RingExecutionVenue<kCap> venue(order_out, &orders_dropped);
  algocraft::SimulatedExchange sim;

  std::thread t3([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      algocraft::LiveOrderRequest req{};
      bool got = false;
      while (order_out.try_pop(req)) {
        got = true;
        auto fill = sim.submit(req.intent, req.bar, req.mode, req.cash, req.position,
                               req.avg_entry, req.container_id, req.workbook_id);
        if (!fill) {
          continue;
        }
        fill->container_id = req.container_id;
        fill->workbook_id = req.workbook_id;
        if (!fill_in.try_push(*fill)) {
          fills_dropped.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (!got) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
    algocraft::LiveOrderRequest req{};
    while (order_out.try_pop(req)) {
      auto fill = sim.submit(req.intent, req.bar, req.mode, req.cash, req.position, req.avg_entry,
                             req.container_id, req.workbook_id);
      if (fill) {
        fill->container_id = req.container_id;
        fill->workbook_id = req.workbook_id;
        (void)fill_in.try_push(*fill);
      }
    }
  });

  algocraft::BarEvent bar{};
  bar.symbol_id = algocraft::SymbolId{7};
  bar.close = algocraft::Price::from_paise(100'00);
  bar.timestamp = algocraft::Timestamp::from_nanos(1'000'000'000LL);

  const auto intent = algocraft::make_intent(algocraft::StrategyId::from(1), bar.symbol_id,
                                             algocraft::Side::Buy, algocraft::Quantity::from_shares(1),
                                             bar.close);
  const auto cid = algocraft::ContainerId::from(42);
  const auto wid = algocraft::WorkbookId::from_u64(9);
  const auto cash = algocraft::Capital::from_paise(10'00'000'00);

  EXPECT_FALSE(venue.submit(intent, bar, algocraft::TradingMode::Mis, cash, {}, {}, cid, wid));

  algocraft::FillEvent fill{};
  for (int i = 0; i < 100 && !fill_in.try_pop(fill); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(fill.container_id, cid);
  EXPECT_EQ(fill.workbook_id, wid);
  EXPECT_EQ(fill.symbol_id, bar.symbol_id);
  EXPECT_EQ(fill.filled_qty.shares(), 1);
  EXPECT_EQ(orders_dropped.load(), 0u);
  EXPECT_EQ(fills_dropped.load(), 0u);

  stop.store(true, std::memory_order_relaxed);
  t3.join();
}
