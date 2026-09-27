#include "algocraft/strategies/bull_harami_break.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
namespace algocraft {
namespace {
constexpr int kAlloc=40, kTp=50, kStop=35, kLast=14*60+30;
constexpr StrategyId kSid=StrategyId::from(22);
}
void BullHaramiBreak::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_=c; armed_=false; have_=false; entry_=0; session_day_=-1;
}
void BullHaramiBreak::on_fill(const FillEvent& f) {
  entry_ = f.side==Side::Buy ? f.fill_price.paise() : 0;
}
void BullHaramiBreak::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id!=config_.symbol_id || bar.resolution!=BarResolution::OneMin) return;
  const auto day=bar.timestamp.nanos()/kNanosPerDay;
  if (day!=session_day_) { session_day_=day; armed_=false; have_=false; }
  const auto o=bar.open.paise(), h=bar.high.paise(), c=bar.close.paise();
  const bool long_pos=pf.position.shares()>0;
  if (long_pos && entry_>0) {
    if (c*10000>=entry_*(10000+kTp) || c*10000<=entry_*(10000-kStop) || c < mother_low_)
      out.push_back(make_intent(kSid,config_.symbol_id,Side::Sell,pf.position));
  } else if (!long_pos && armed_ && ist_minute_of_day(bar.timestamp)<kLast && c > mother_high_) {
    const auto qty=position_for_capital(Capital::from_paise(pf.cash.paise()*kAlloc/100), bar.close, 1);
    if (qty.shares()>0) out.push_back(make_intent(kSid,config_.symbol_id,Side::Buy,qty));
    armed_=false;
  }
  // Detect harami this bar (as inside candle).
  if (have_ && pc_ < po_) {
    const auto hi=std::max(po_,pc_), lo=std::min(po_,pc_);
    const auto chi=std::max(o,c), clo=std::min(o,c);
    if (c>o && clo>=lo && chi<=hi && (chi-clo)*2 <= (hi-lo)) {
      armed_=true; mother_high_=std::max(h, std::max(po_, pc_)); // use highs of mother+inside
      // mother is previous bar — store its high from previous update: approximate with max(po,pc) + need prev high
      mother_high_ = std::max(mother_high_, h);
      mother_low_ = lo;
    } else if (armed_ && c < mother_low_) {
      armed_=false; // invalidated
    }
  }
  po_=o; pc_=c; have_=true;
}
StrategyMetadata BullHaramiBreak::metadata() const {
  return {"bull_harami_break","1.0.0",TradingMode::Mis,BarResolution::OneMin,{}};
}
}  // namespace algocraft
