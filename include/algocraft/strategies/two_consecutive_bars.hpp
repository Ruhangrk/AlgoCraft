#pragma once

#include <array>
#include <cstdint>

#include "algocraft/execution/cost_calculator.hpp"
#include "algocraft/indicators/sma.hpp"
#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

// Long-only 1m MIS:
// Buy (stack allowed): price > 200DMA AND (3 consecutive green OR 2 consecutive green with
//   (high2 - low1) / low1 >= 0.25%). Each buy spends 10% of initial allocation.
// Sell all: 3 consecutive red OR (2 consecutive red AND net proceeds after sell fees
// ≈ total buy cost incl. buy fees — exit on the 2nd red). No buys after 14:30 IST.
// 200DMA is seeded from daily bars via DataFetch/Rocks before trading starts.
class TwoConsecutiveBars final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
              std::vector<OrderIntent>& out) override;
  void on_fill(const FillEvent& fill) override;
  void on_order_update(const OrderUpdate& update) override { (void)update; }
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;

private:
  struct Candle {
    std::int64_t open{0};
    std::int64_t high{0};
    std::int64_t low{0};
    std::int64_t close{0};
  };

  void reset_cycle();
  void push_candle(const BarEvent& bar);
  [[nodiscard]] Quantity buy_clip(Price px) const;
  [[nodiscard]] bool three_green() const;
  [[nodiscard]] bool three_red() const;
  [[nodiscard]] bool two_red() const;
  [[nodiscard]] bool two_green_range() const;
  [[nodiscard]] bool break_even_at(Price sp, Quantity pos) const;
  [[nodiscard]] bool above_sma(Price px) const;

  StrategyConfig config_{};
  CostCalculator costs_{};
  Sma* sma200_{nullptr};
  std::array<Candle, 3> candles_{};
  int candle_count_{0};
  std::int64_t session_day_{-1};
  std::int64_t total_cost_paise_{0};  // Σ (buy notional + buy fees)
};

}  // namespace algocraft
