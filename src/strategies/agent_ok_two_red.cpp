#include "algocraft/strategies/agent_ok_two_red.hpp"

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {
constexpr StrategyId kSid = StrategyId::from(9001);
constexpr int kFlattenMin = 15 * 60 + 15;  // 15:15 IST
}

void AgentOkTwoRed::configure(const StrategyConfig& config, IndicatorLibrary&) {
  config_ = config;
}

void AgentOkTwoRed::on_bar(const BarEvent& bar, const PortfolioView& pf,
                          std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const int minute = ist_minute_of_day(bar.timestamp);
  if (minute >= kFlattenMin) {
    if (pf.position.shares() > 0)
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    red_streak_ = 0;
    return;
  }
  const bool red = bar.close.paise() < bar.open.paise();
  const bool green = bar.close.paise() > bar.open.paise();
  if (pf.position.shares() == 0) {
    red_streak_ = red ? red_streak_ + 1 : 0;
    if (red_streak_ >= 2) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
      red_streak_ = 0;
    }
  } else if (green) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    red_streak_ = 0;
  }
}

StrategyMetadata AgentOkTwoRed::metadata() const {
  return {"agent_ok_two_red", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}

}  // namespace algocraft
