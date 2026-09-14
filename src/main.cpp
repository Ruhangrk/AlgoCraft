#include "algocraft/engine/phase0_runtime.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/dummy_provider.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <spdlog/spdlog.h>

int main() {
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<algocraft::DummyProvider>());
  spdlog::info("active data source: {}", registry.active_provider().name());

  algocraft::Phase0Runtime runtime;
  runtime.start();

  for (std::uint64_t i = 0; i < 8; ++i) {
    algocraft::DummyEvent event{};
    event.kind = algocraft::kEventBar;
    event.symbol_id = 1;
    event.seq = i;
    while (!runtime.market_data_ring().try_push(event)) {
      std::this_thread::yield();
    }
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (runtime.bars_processed() < 8 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  runtime.stop();
  spdlog::info("bars={} fills={} commands={} persist={} logs={}", runtime.bars_processed(),
               runtime.fills_processed(), runtime.commands_processed(), runtime.persist_events(),
               runtime.logs_written());
  return runtime.bars_processed() >= 8 ? 0 : 1;
}
