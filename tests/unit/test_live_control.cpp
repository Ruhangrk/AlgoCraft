#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/engine/live_control.hpp"
#include "algocraft/engine/spsc_ring.hpp"

TEST(LiveControl, CommandAndRoutingRoundTrip) {
  constexpr std::size_t kCap = 64;
  algocraft::SpscRing<algocraft::LiveCommand, kCap> commands;
  algocraft::SpscRing<algocraft::RoutingSignal, kCap> routing;

  std::atomic<int> kills{0};
  std::atomic<int> stops{0};
  std::atomic<int> bars{0};
  std::atomic<bool> done{false};

  std::thread t0([&] {
    while (!done.load(std::memory_order_relaxed) || true) {
      algocraft::LiveCommand cmd{};
      while (commands.try_pop(cmd)) {
        if (cmd.type == algocraft::LiveCommandType::StopRun) {
          stops.fetch_add(1);
          done.store(true);
        } else if (cmd.type == algocraft::LiveCommandType::System &&
                   cmd.system.type == algocraft::SystemEventType::KillSwitch) {
          kills.fetch_add(1);
        }
      }
      algocraft::RoutingSignal sig{};
      while (routing.try_pop(sig)) {
        if (sig.kind == algocraft::RoutingSignalKind::Bar) {
          bars.fetch_add(1);
        }
      }
      if (done.load(std::memory_order_relaxed) && stops.load() > 0) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  });

  algocraft::RoutingSignal sig{};
  sig.kind = algocraft::RoutingSignalKind::Bar;
  sig.bar.symbol_id = algocraft::SymbolId{3};
  sig.bar.close = algocraft::Price::from_paise(100'00);
  ASSERT_TRUE(routing.try_push(sig));
  ASSERT_TRUE(commands.try_push(algocraft::LiveCommand::kill_switch()));
  ASSERT_TRUE(commands.try_push(algocraft::LiveCommand::stop_run()));

  t0.join();
  EXPECT_EQ(kills.load(), 1);
  EXPECT_EQ(stops.load(), 1);
  EXPECT_EQ(bars.load(), 1);
}
