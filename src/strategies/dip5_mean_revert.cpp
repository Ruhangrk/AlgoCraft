#include "algocraft/strategies/dip5_mean_revert.hpp"

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {
namespace {

constexpr int kLookback = 5;
constexpr int kBuyDipBps = 25;   // buy ≤ −0.25% vs prior-5 avg
constexpr int kExitFlatBps = 0;  // sell when close ≥ avg
constexpr int kStopBps = 50;     // hard stop ≤ −0.50% vs avg
constexpr int kBuyAllocPct = 40;
constexpr int kLastHourMinute = 14 * 60 + 30;
constexpr StrategyId kSid = StrategyId::from(8);

}  // namespace

void Dip5MeanRevert::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  avg5_ = &lib.get<LaggedSma>(config_.symbol_id, BarResolution::OneMin, kLookback);
}

Quantity Dip5MeanRevert::buy_clip(Price px, Capital available) const {
  if (available.paise() <= 0 || px.paise() <= 0) {
    return {};
  }
  const auto clip = Capital::from_paise(available.paise() * kBuyAllocPct / 100);
  if (clip.paise() <= 0) {
    return {};
  }
  return position_for_capital(clip, px, /*leverage=*/1);
}

void Dip5MeanRevert::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
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
  const bool back_to_mean = close * 10000.0 >= avg * static_cast<double>(10000 + kExitFlatBps);
  const bool hard_stop = close * 10000.0 < avg * static_cast<double>(10000 - kStopBps);
  if (long_pos && (back_to_mean || hard_stop)) {
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, portfolio.position));
    return;
  }

  if (long_pos) {
    return;
  }
  if (ist_minute_of_day(bar.timestamp) >= kLastHourMinute) {
    return;
  }

  const bool dip = close * 10000.0 <= avg * static_cast<double>(10000 - kBuyDipBps);
  if (!dip) {
    return;
  }
  const auto qty = buy_clip(bar.close, portfolio.cash);
  if (qty.shares() <= 0) {
    return;
  }
  out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
}

StrategyMetadata Dip5MeanRevert::metadata() const {
  return {.name = "dip5_mean_revert",
          .version = "1.0.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {"LAGGED_SMA"}};
}

}  // namespace algocraft
