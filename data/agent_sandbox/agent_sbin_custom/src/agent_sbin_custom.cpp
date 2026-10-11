#include "algocraft/strategies/agent_sbin_custom.hpp"

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {
constexpr StrategyId kSid = StrategyId::from(9001);
constexpr int kFlattenMin = 15 * 60 + 15;  // 15:15 IST
}

void AgentSbinCustom::configure(const StrategyConfig& config, IndicatorLibrary&) {
  config_ = config;
}

void AgentSbinCustom::on_bar(const BarEvent& bar, const PortfolioView& pf,
                          std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) {
    return;
  }
  const int minute = ist_minute_of_day(bar.timestamp);
  if (minute >= kFlattenMin) {
    if (pf.position.shares() > 0) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    }
    return;
  }
  if (pf.position.shares() > 0) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
  }
  out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, Quantity{1}));
}

StrategyMetadata AgentSbinCustom::metadata() const {
  return {"agent_sbin_custom", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}

}  // namespace algocraft
