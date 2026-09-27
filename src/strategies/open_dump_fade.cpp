#include "algocraft/strategies/open_dump_fade.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace {
constexpr int kAllocPct = 40; constexpr int kDumpBps = 30; constexpr int kTpBps = 40; constexpr int kStopBps = 30;
constexpr int kEntryWindow = 20; constexpr int kForceExitMin = 12 * 60;
constexpr StrategyId kSid = StrategyId::from(14);
}
void OpenDumpFade::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_ = c; session_day_ = -1; day_open_ = 0; bars_today_ = 0; bought_today_ = false; entry_paise_ = 0;
}
void OpenDumpFade::on_fill(const FillEvent& f) {
  if (f.side == Side::Buy) { entry_paise_ = f.fill_price.paise(); bought_today_ = true; }
  else entry_paise_ = 0;
}
void OpenDumpFade::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) {
    session_day_ = day; day_open_ = bar.open.paise(); bars_today_ = 0; bought_today_ = false;
  }
  ++bars_today_;
  const auto c = bar.close.paise();
  const auto minute = ist_minute_of_day(bar.timestamp);
  const bool long_pos = pf.position.shares() > 0;
  if (long_pos) {
    const bool tp = entry_paise_ > 0 && c * 10000 >= entry_paise_ * (10000 + kTpBps);
    const bool stop = entry_paise_ > 0 && c * 10000 <= entry_paise_ * (10000 - kStopBps);
    if (tp || stop || minute >= kForceExitMin) {
      out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
    }
    return;
  }
  if (bought_today_ || bars_today_ > kEntryWindow || day_open_ <= 0) return;
  if (c * 10000 > day_open_ * (10000 - kDumpBps)) return;
  const auto qty = position_for_capital(Capital::from_paise(pf.cash.paise() * kAllocPct / 100), bar.close, 1);
  if (qty.shares() > 0) out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, qty));
}
StrategyMetadata OpenDumpFade::metadata() const {
  return {"open_dump_fade", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
}  // namespace algocraft
