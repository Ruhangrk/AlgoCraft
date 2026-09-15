#include "algocraft/strategies/opening_range_breakout.hpp"

#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {

constexpr std::int64_t kNanosPerDay = 86'400'000'000'000LL;

}  // namespace

void OpeningRangeBreakout::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  (void)lib;
  bars_seen_ = 0;
  range_ready_ = false;
  traded_ = false;
  session_day_ = -1;
}

void OpeningRangeBreakout::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                                  std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id) {
    return;
  }

  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) {
    session_day_ = day;
    bars_seen_ = 0;
    range_ready_ = false;
    traded_ = false;
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
    return;
  }

  if (traded_ || portfolio.position.shares() != 0) {
    return;
  }
  if (bar.close > range_high_) {
    traded_ = true;
    out.push_back(make_intent(StrategyId::from(3), config_.symbol_id, Side::Buy, config_.order_qty));
  }
}

void OpeningRangeBreakout::on_fill(const FillEvent& fill) { (void)fill; }

StrategyMetadata OpeningRangeBreakout::metadata() const {
  return {.name = "opening_range_breakout",
          .version = "1.0.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {}};
}

}  // namespace algocraft
