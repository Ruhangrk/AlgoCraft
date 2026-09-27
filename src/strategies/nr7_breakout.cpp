#include "algocraft/strategies/nr7_breakout.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
namespace algocraft {
namespace {
constexpr int kAlloc=40, kTp=45, kStop=30, kLast=14*60+30;
constexpr StrategyId kSid=StrategyId::from(26);
}
void Nr7Breakout::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_=c; n_=0; armed_=false; entry_=0; session_day_=-1;
}
void Nr7Breakout::on_fill(const FillEvent& f) {
  entry_ = f.side==Side::Buy ? f.fill_price.paise() : 0;
}
void Nr7Breakout::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id!=config_.symbol_id || bar.resolution!=BarResolution::OneMin) return;
  const auto day=bar.timestamp.nanos()/kNanosPerDay;
  if (day!=session_day_) { session_day_=day; n_=0; armed_=false; }
  const auto h=bar.high.paise(), l=bar.low.paise(), c=bar.close.paise();
  const bool long_pos=pf.position.shares()>0;
  if (long_pos && entry_>0) {
    if (c*10000>=entry_*(10000+kTp) || c*10000<=entry_*(10000-kStop) || c < nr_low_)
      out.push_back(make_intent(kSid,config_.symbol_id,Side::Sell,pf.position));
  } else if (!long_pos && armed_ && ist_minute_of_day(bar.timestamp)<kLast && c > nr_high_) {
    const auto qty=position_for_capital(Capital::from_paise(pf.cash.paise()*kAlloc/100), bar.close, 1);
    if (qty.shares()>0) out.push_back(make_intent(kSid,config_.symbol_id,Side::Buy,qty));
    armed_=false;
  }
  // push bar into window
  if (n_ < 7) { highs_[n_]=h; lows_[n_]=l; ++n_; }
  else {
    for (int i=0;i<6;++i) { highs_[i]=highs_[i+1]; lows_[i]=lows_[i+1]; }
    highs_[6]=h; lows_[6]=l;
  }
  if (n_ >= 7) {
    // Is last completed bar (index 6 before push was old — after push index 6 is current).
    // Check if bar at index 5 (previous) was NR7 among bars 0..5? Classic: current bar is NR7 of last 7.
    const auto cur_r = highs_[6]-lows_[6];
    bool nr7=true;
    for (int i=0;i<6;++i) {
      if (highs_[i]-lows_[i] <= cur_r) { nr7=false; break; }
    }
    if (nr7 && cur_r > 0) {
      armed_=true; nr_high_=highs_[6]; nr_low_=lows_[6];
    }
  }
}
StrategyMetadata Nr7Breakout::metadata() const {
  return {"nr7_breakout","1.0.0",TradingMode::Mis,BarResolution::OneMin,{}};
}
}  // namespace algocraft
