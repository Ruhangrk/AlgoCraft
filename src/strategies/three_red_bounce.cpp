#include "algocraft/strategies/three_red_bounce.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace {
constexpr int kAllocPct = 40;
constexpr int kStopBps = 40;
constexpr int kLastHour = 14 * 60 + 30;
constexpr StrategyId kSid = StrategyId::from(10);
}
void ThreeRedBounce::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_ = c; prev_close_ = 0; red_streak_ = 0; green_streak_ = 0; entry_paise_ = 0; session_day_ = -1;
}
void ThreeRedBounce::on_fill(const FillEvent& f) {
  if (f.side == Side::Buy) entry_paise_ = f.fill_price.paise();
  else entry_paise_ = 0;
}
void ThreeRedBounce::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) { session_day_ = day; red_streak_ = 0; green_streak_ = 0; prev_close_ = 0; }
  const auto c = bar.close.paise();
  const bool green = c > bar.open.paise();
  const bool red = c < bar.open.paise();
  if (red) { ++red_streak_; green_streak_ = 0; } else if (green) { ++green_streak_; red_streak_ = 0; }
  else { red_streak_ = 0; green_streak_ = 0; }
  prev_close_ = c;
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos) {
    const bool stop = entry_paise_ > 0 && c * 10000 < entry_paise_ * (10000 - kStopBps);
    if (green_streak_ >= 2 || stop) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    }
    return;
  }
  if (ist_minute_of_day(bar.timestamp) >= kLastHour) return;
  if (red_streak_ < 3) return;
  const auto clip = Capital::from_paise(pf.cash.paise() * kAllocPct / 100);
  const auto qty = position_for_capital(clip, bar.close, 1);
  if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
}
StrategyMetadata ThreeRedBounce::metadata() const {
  return {"three_red_bounce", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
}  // namespace algocraft
