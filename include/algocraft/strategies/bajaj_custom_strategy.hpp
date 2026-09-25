#pragma once

#include "algocraft/domain/price.hpp"
#include "algocraft/indicators/rsi.hpp"
#include "algocraft/indicators/vwap.hpp"
#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

// BAJFINANCE (INE296A01032) intraday long-only MIS:
// buy RSI oversold dips below VWAP, exit on RSI recovery / VWAP reclaim / TP / SL / 14:30 IST.
// Position size = moderate % of available cash (margin), × MIS leverage → share count.
// Defaults tuned on ~1 week of 1m bars (Sep 2026); not a profit guarantee.
class BajajCustomStrategy final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
              std::vector<OrderIntent>& out) override;
  void on_fill(const FillEvent& fill) override;
  void on_order_update(const OrderUpdate& update) override { (void)update; }
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;

private:
  void reset_session();
  [[nodiscard]] Quantity size_for_cash(Capital cash, Price px) const;

  StrategyConfig config_{};
  const Rsi* rsi_{nullptr};
  const Vwap* vwap_{nullptr};
  std::int64_t session_day_{-1};
  bool traded_today_{false};
  std::int64_t entry_paise_{0};
};

}  // namespace algocraft
