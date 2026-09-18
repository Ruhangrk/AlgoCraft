#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/symbol.hpp"

#include <stdexcept>

#include <gtest/gtest.h>

TEST(SymbolTable, InternReturnsStableId) {
  algocraft::SymbolTable table;
  algocraft::Instrument inst{};
  inst.lot_size = algocraft::Quantity::from_shares(1);
  inst.tick_size = algocraft::Price::from_paise(5);

  const auto id1 = table.intern({.market = algocraft::Market::Nse,
                                 .segment = algocraft::Segment::Eq,
                                 .ticker = "RELIANCE"},
                                inst);
  const auto id2 = table.intern({.market = algocraft::Market::Nse,
                                 .segment = algocraft::Segment::Eq,
                                 .ticker = "RELIANCE"},
                                inst);
  const auto id3 = table.intern(
      {.market = algocraft::Market::Nse, .segment = algocraft::Segment::Eq, .ticker = "INFY"}, inst);

  EXPECT_EQ(id1, 1u);
  EXPECT_EQ(id1, id2);
  EXPECT_EQ(id3, 2u);
  ASSERT_TRUE(table.find("RELIANCE").has_value());
  EXPECT_EQ(*table.find("RELIANCE"), id1);
  EXPECT_FALSE(table.find("NOPE").has_value());
  EXPECT_EQ(table.symbol(id1).ticker, "RELIANCE");
  EXPECT_EQ(table.instrument(id1).tick_size.paise(), 5);
  EXPECT_EQ(table.instrument(id1).symbol_id, id1);
}

TEST(SymbolTable, EmptyTickerThrows) {
  algocraft::SymbolTable table;
  EXPECT_THROW(table.intern({.ticker = ""}, {}), std::invalid_argument);
}

TEST(SymbolTable, UnknownIdThrows) {
  algocraft::SymbolTable table;
  EXPECT_THROW((void)table.symbol(1), std::out_of_range);
}
