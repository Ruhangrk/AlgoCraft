#include "algocraft/strategies/agent_reliance_custom.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"

using namespace algocraft;

void AgentRelianceCustom::configure(const StrategyConfig& cfg, IndicatorLibrary&) {
  config_ = cfg;
}

void AgentRelianceCustom::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  if (ist_minute_of_day(bar.timestamp) >= 15 * 60 + 15) {
    if (pf.position.shares() > 0) {
      out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell, Quantity::from_shares(1)));
    }
    return;
  }
  if (pf.position.shares() == 0) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
  }
}

StrategyMetadata AgentRelianceCustom::metadata() const {
  return {"agent_reliance_custom", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
