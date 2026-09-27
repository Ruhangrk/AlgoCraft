#include "algocraft/strategies/morning_star_long.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/routing/position_sizer.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include <algorithm>
namespace algocraft {
namespace {
constexpr int kAlloc=40, kTp=50, kStop=35, kLast=14*60+30;
constexpr StrategyId kSid=StrategyId::from(23);
}
void MorningStarLong::configure(const StrategyConfig& c, IndicatorLibrary&) {
  config_=c; n_=0; entry_=0; session_day_=-1;
}
void MorningStarLong::on_fill(const FillEvent& f) {
  entry_ = f.side==Side::Buy ? f.fill_price.paise() : 0;
}
void MorningStarLong::on_bar(const BarEvent& bar, const PortfolioView& pf, std::vector<OrderIntent>& out) {
  if (bar.symbol_id!=config_.symbol_id || bar.resolution!=BarResolution::OneMin) return;
  const auto day=bar.timestamp.nanos()/kNanosPerDay;
  if (day!=session_day_) { session_day_=day; n_=0; }
  const auto o=bar.open.paise(), c=bar.close.paise();
  const bool long_pos=pf.position.shares()>0;
  if (long_pos && entry_>0) {
    if (c*10000>=entry_*(10000+kTp) || c*10000<=entry_*(10000-kStop))
      out.push_back(make_intent(kSid,config_.symbol_id,Side::Sell,pf.position));
  } else if (!long_pos && n_>=2 && ist_minute_of_day(bar.timestamp)<kLast) {
    const bool star1 = c1_ < o1_;
    const auto b1=std::llabs(c1_-o1_), b2=std::llabs(c2_-o2_), b3=std::llabs(c-o);
    const bool small2 = b2*2 <= b1;
    const bool star3 = c>o && c > (o1_+c1_)/2;
    if (star1 && small2 && star3 && b3 > 0) {
      const auto qty=position_for_capital(Capital::from_paise(pf.cash.paise()*kAlloc/100), bar.close, 1);
      if (qty.shares()>0) out.push_back(make_intent(kSid,config_.symbol_id,Side::Buy,qty));
    }
  }
  o1_=o2_; c1_=c2_; o2_=o; c2_=c; n_=std::min(n_+1, 2);
}
StrategyMetadata MorningStarLong::metadata() const {
  return {"morning_star_long","1.0.0",TradingMode::Mis,BarResolution::OneMin,{}};
}
}  // namespace algocraft
