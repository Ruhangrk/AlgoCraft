#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/dummy_provider.hpp"

#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

TEST(DataSourceRegistry, RegistersDummyAndLoadsBars) {
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<algocraft::DummyProvider>());

  auto& provider = registry.active_provider();
  EXPECT_EQ(provider.name(), "dummy");
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
  registry.register_provider(std::make_unique<algocraft::DummyProvider>());
  EXPECT_THROW(registry.register_provider(std::make_unique<algocraft::DummyProvider>()),
               std::invalid_argument);
}
