#include "algocraft/strategies/ema_trend_pullback.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace {
constexpr int kAllocPct = 45; constexpr int kPullBps = 8; constexpr int kStopBps = 35;
constexpr int kLastHour = 14 * 60 + 30; constexpr StrategyId kSid = StrategyId::from(12);
}
void EmaTrendPullback::configure(const StrategyConfig& c, IndicatorLibrary& lib) {
  config_ = c; entry_paise_ = 0;
  fast_ = &lib.get<Ema>(c.symbol_id, BarResolution::OneMin, 9);
  slow_ = &lib.get<Ema>(c.symbol_id, BarResolution::OneMin, 21);
}
void EmaTrendPullback::on_fill(const FillEvent& f) {
  if (f.side == Side::Buy) entry_paise_ = f.fill_price.paise(); else entry_paise_ = 0;
}
void EmaTrendPullback::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  if (!fast_ || !slow_ || !fast_->ready() || !slow_->ready()) return;
  const auto c = static_cast<double>(bar.close.paise());
  const auto f = fast_->value(), s = slow_->value();
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos) {
    const bool cross_down = f < s;
    const bool stop = entry_paise_ > 0 && c * 10000.0 < entry_paise_ * (10000 - kStopBps);
    if (cross_down || stop) out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    return;
  }
  if (ist_minute_of_day(bar.timestamp) >= kLastHour) return;
  if (f <= s) return;
  // Pullback: close within kPullBps of EMA9 from above.
  if (c < f) return;
  if ((c - f) * 10000.0 > f * kPullBps) return;
  const auto qty = position_for_capital(Capital::from_paise(pf.cash.paise() * kAllocPct / 100), bar.close, 1);
  if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
}
StrategyMetadata EmaTrendPullback::metadata() const {
  return {"ema_trend_pullback", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {"EMA"}};
}
}  // namespace algocraft
