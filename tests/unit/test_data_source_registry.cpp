#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/market_data/data_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/historical_loader.hpp"

#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

class StubLoader final : public algocraft::HistoricalDataLoader {
public:
  std::vector<algocraft::BarEvent> load_bars(algocraft::SymbolId symbol_id,
                                             algocraft::Timestamp /*from*/,
                                             algocraft::Timestamp /*to*/,
                                             algocraft::BarResolution resolution) override {
    std::vector<algocraft::BarEvent> bars(2);
    bars[0].symbol_id = symbol_id;
    bars[0].timestamp = algocraft::Timestamp::from_nanos(1);
    bars[0].resolution = resolution;
    bars[1].symbol_id = symbol_id;
    bars[1].timestamp = algocraft::Timestamp::from_nanos(2);
    bars[1].resolution = resolution;
    return bars;
  }
};

class StubProvider final : public algocraft::DataProvider {
public:
  std::string_view name() const override { return "stub"; }
  algocraft::HistoricalDataLoader& historical_loader() override { return loader_; }
  algocraft::MarketDataFeed* live_feed() override { return nullptr; }
  algocraft::DataProviderCapabilities capabilities() const override {
    return algocraft::DataProviderCapabilities{
        .supported_resolutions = {algocraft::BarResolution::OneMin},
    };
  }

private:
  StubLoader loader_{};
};

}  // namespace

TEST(DataSourceRegistry, RegistersStubAndLoadsBars) {
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<StubProvider>());

  auto& provider = registry.active_provider();
  EXPECT_EQ(provider.name(), "stub");
  EXPECT_EQ(provider.live_feed(), nullptr);
  EXPECT_EQ(provider.capabilities().supported_resolutions.size(), 1u);

  const auto bars = provider.historical_loader().load_bars(
      1, algocraft::Timestamp::from_nanos(1), algocraft::Timestamp::from_nanos(2),
      algocraft::BarResolution::OneMin);
  ASSERT_EQ(bars.size(), 2u);
  EXPECT_EQ(bars[0].symbol_id, 1u);
  EXPECT_EQ(bars[0].resolution, algocraft::BarResolution::OneMin);
}

TEST(DataSourceRegistry, UnknownActiveThrows) {
  algocraft::DataSourceRegistry registry;
  EXPECT_THROW(registry.set_active("upstox"), std::invalid_argument);
}

TEST(DataSourceRegistry, DuplicateRegisterThrows) {
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<StubProvider>());
  EXPECT_THROW(registry.register_provider(std::make_unique<StubProvider>()),
               std::invalid_argument);
}
