#include "algocraft/strategies/agent_ok_two_green.hpp"

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {
constexpr StrategyId kSid = StrategyId::from(9001);
constexpr int kFlattenMin = 15 * 60 + 15;  // 15:15 IST
}

void AgentOkTwoGreen::configure(const StrategyConfig& config, IndicatorLibrary&) {
  config_ = config;
}

void AgentOkTwoGreen::on_bar(const BarEvent& bar, const PortfolioView& pf,
                          std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const int minute = ist_minute_of_day(bar.timestamp);
  if (minute >= kFlattenMin) {
    if (pf.position.shares() > 0)
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    green_streak_ = 0;
    return;
  }
  const bool green = bar.close.paise() > bar.open.paise();
  const bool red = bar.close.paise() < bar.open.paise();
  if (pf.position.shares() == 0) {
    green_streak_ = green ? green_streak_ + 1 : 0;
    if (green_streak_ >= 2) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
      green_streak_ = 0;
    }
  } else if (red) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    green_streak_ = 0;
  }
}

StrategyMetadata AgentOkTwoGreen::metadata() const {
  return {"agent_ok_two_green", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}

}  // namespace algocraft
