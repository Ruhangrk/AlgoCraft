#include "algocraft/strategies/reliance_prev5_avg_break.hpp"

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {

constexpr int kLookback = 5;
// Wider bands + hysteresis vs the original 0.15%/0.10% (that overtraded into fees).
constexpr int kBuyBps = 30;        // enter only on +0.30% vs prior-5 avg
constexpr int kSellStopBps = 45;   // exit if −0.45% vs avg
constexpr int kTakeProfitBps = 55; // also exit if +0.55% vs avg (lock momentum)
constexpr int kBuyAllocPct = 50;   // one shot; no stacking
constexpr int kLastHourMinute = 14 * 60 + 30;
constexpr StrategyId kSid = StrategyId::from(7);

}  // namespace

void ReliancePrev5AvgBreak::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  avg5_ = &lib.get<LaggedSma>(config_.symbol_id, BarResolution::OneMin, kLookback);
}

Quantity ReliancePrev5AvgBreak::buy_clip(Price px, Capital available) const {
  if (available.paise() <= 0 || px.paise() <= 0) {
    return {};
  }
  const auto clip = Capital::from_paise(available.paise() * kBuyAllocPct / 100);
  if (clip.paise() <= 0) {
    return {};
  }
  return position_for_capital(clip, px, /*leverage=*/1);
}

void ReliancePrev5AvgBreak::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                           std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) {
    return;
  }
  if (avg5_ == nullptr || !avg5_->ready()) {
    return;
  }

  const auto close = static_cast<double>(bar.close.paise());
  const auto avg = avg5_->value();
  if (avg <= 0) {
    return;
  }

  const bool long_pos = portfolio.position.shares() > 0;
  const bool stop_hit = close * 10000.0 < avg * static_cast<double>(10000 - kSellStopBps);
  const bool take_profit = close * 10000.0 > avg * static_cast<double>(10000 + kTakeProfitBps);
  if (long_pos && (stop_hit || take_profit)) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, portfolio.position));
    return;
  }

  // Flat-only entries — stacking was fee-heavy on thin edges.
  if (long_pos) {
    return;
  }
  if (ist_minute_of_day(bar.timestamp) >= kLastHourMinute) {
    return;
  }

  const bool buy_signal = close * 10000.0 > avg * static_cast<double>(10000 + kBuyBps);
  if (!buy_signal) {
    return;
  }
  const auto qty = buy_clip(bar.close, portfolio.cash);
  if (qty.shares() <= 0) {
    return;
  }
  out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
}

StrategyMetadata ReliancePrev5AvgBreak::metadata() const {
  return {.name = "reliance_prev5_avg_break",
          .version = "1.1.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {"LAGGED_SMA"}};
}

}  // namespace algocraft
