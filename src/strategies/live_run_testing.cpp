#include "algocraft/strategies/live_run_testing.hpp"

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {

constexpr int kLastEntryMin = 15 * 60 + 15;  // trade through MIS square-off edge
constexpr StrategyId kSid = StrategyId::from(40);
constexpr std::int64_t kQty = 1;  // fixed 1 share — ignore router order_qty (full capital)

}  // namespace

void LiveRunTesting::configure(const StrategyConfig& c, IndicatorLibrary&) { config_ = c; }

void LiveRunTesting::on_fill(const FillEvent&) {}

void LiveRunTesting::on_bar(const BarEvent& bar, const PortfolioView& pf,
                            std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) {
    return;
  }
  if (ist_minute_of_day(bar.timestamp) >= kLastEntryMin) {
    // Still flatten if open.
    if (pf.position.shares() > 0) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    }
    return;
  }

  const auto one = Quantity::from_shares(kQty);
  // Every bar: if long, sell then re-buy (2 fills). If flat, buy (1 fill).
  // Container applies intents in order; hist fills sync so both land same bar.
  if (pf.position.shares() > 0) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
  }
  out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, one));
}

StrategyMetadata LiveRunTesting::metadata() const {
  return {"live_run_testing", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}

}  // namespace algocraft
