#include "algocraft/strategies/hammer_reversal.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
namespace algocraft {
namespace {
constexpr int kAlloc=40, kTp=40, kStop=30, kLast=14*60+30;
constexpr StrategyId kSid=StrategyId::from(20);
}
void HammerReversal::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_=c; red_streak_=0; entry_=0; session_day_=-1;
}
void HammerReversal::on_fill(const FillEvent& f) {
  entry_ = f.side==Side::Buy ? f.fill_price.paise() : 0;
}
void HammerReversal::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id!=config_.symbol_id || bar.resolution!=BarResolution::OneMin) return;
  const auto day=bar.timestamp.nanos()/kNanosPerDay;
  if (day!=session_day_) { session_day_=day; red_streak_=0; }
  const auto o=bar.open.paise(),h=bar.high.paise(),l=bar.low.paise(),c=bar.close.paise();
  if (c<o) ++red_streak_; else red_streak_=0;
  const bool long_pos=pf.position.shares()>0;
  if (long_pos && entry_>0) {
    if (c*10000>=entry_*(10000+kTp) || c*10000<=entry_*(10000-kStop))
      out.push_back(make_intent(kSid,config_.symbol_id,Side::Sell,pf.position));
    return;
  }
  if (long_pos || ist_minute_of_day(bar.timestamp)>=kLast || red_streak_<2) return;
  const auto body=std::max<std::int64_t>(1, std::llabs(c-o));
  const auto lower=std::min(o,c)-l;
  const auto upper=h-std::max(o,c);
  const auto range=std::max<std::int64_t>(1,h-l);
  // Hammer: lower wick ≥2× body, close in upper third, small upper wick.
  if (lower < 2*body || upper*2 > body) return;
  if ((c-l)*3 < range*2) return;
  const auto qty=position_for_capital(Capital::from_paise(pf.cash.paise()*kAlloc/100), bar.close, 1);
  if (qty.shares()>0) out.push_back(make_intent(kSid,config_.symbol_id,Side::Buy,qty));
}
StrategyMetadata HammerReversal::metadata() const {
  return {"hammer_reversal","1.0.0",TradingMode::Mis,BarResolution::OneMin,{}};
}
}  // namespace algocraft
