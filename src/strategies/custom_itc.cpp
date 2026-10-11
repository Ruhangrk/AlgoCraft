#include "algocraft/strategies/custom_itc.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"

using namespace algocraft;

void CustomItc::configure(const StrategyConfig& cfg, IndicatorLibrary&) {
  config_ = cfg;
}

void CustomItc::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin)
    return;

  bool green = bar.close > bar.open;
  bool red   = bar.close < bar.open;

  if (green) {
    green_streak_++; red_streak_ = 0;
  } else if (red) {
    red_streak_++; green_streak_ = 0;
  } else {
    green_streak_ = 0; red_streak_ = 0;
  }

  if (green_streak_ == 2 && pf.position.shares() == 0) {
    auto price = bar.close.paise();
    auto cash  = pf.cash.paise();
    if (price > 0) {
      int shares = static_cast<int>((cash / 10) / price);
      if (shares > 0)
        out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy,
                                  Quantity::from_shares(shares)));
    }
  }

  if (red_streak_ == 3 && pf.position.shares() > 0) {
    out.push_back(make_intent(StrategyId::from(2), config_.symbol_id, Side::Sell,
                              Quantity::from_shares(pf.position.shares())));
  }

  if (ist_minute_of_day(bar.timestamp) == 15 * 60 + 15 && pf.position.shares() > 0) {
    out.push_back(make_intent(StrategyId::from(3), config_.symbol_id, Side::Sell,
                              Quantity::from_shares(pf.position.shares())));
  }
}

StrategyMetadata CustomItc::metadata() const {
  return {"custom_itc", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
