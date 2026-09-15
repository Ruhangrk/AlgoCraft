#include "algocraft/strategies/opening_range_breakout.hpp"

#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {

void OpeningRangeBreakout::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  (void)lib;
  bars_seen_ = 0;
  range_ready_ = false;
  traded_ = false;
}

std::vector<OrderIntent> OpeningRangeBreakout::on_bar(const BarEvent& bar,
                                                      const PortfolioView& portfolio) {
  if (bar.symbol_id != config_.symbol_id) {
    return {};
  }

  if (!range_ready_) {
    if (bars_seen_ == 0) {
      range_high_ = bar.high;
      range_low_ = bar.low;
    } else {
      if (bar.high > range_high_) {
        range_high_ = bar.high;
      }
      if (bar.low < range_low_) {
        range_low_ = bar.low;
      }
    }
    ++bars_seen_;
    if (bars_seen_ >= config_.orb_bars) {
      range_ready_ = true;
    }
    return {};
  }

  if (traded_ || portfolio.position.shares() != 0) {
    return {};
  }
  if (bar.close > range_high_) {
    traded_ = true;
    return {make_intent(StrategyId::from(3), config_.symbol_id, Side::Buy, config_.order_qty)};
  }
  return {};
}

void OpeningRangeBreakout::on_fill(const FillEvent& fill) { (void)fill; }

StrategyMetadata OpeningRangeBreakout::metadata() const {
  return {.name = "opening_range_breakout",
          .version = "1.0.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin};
}

}  // namespace algocraft
