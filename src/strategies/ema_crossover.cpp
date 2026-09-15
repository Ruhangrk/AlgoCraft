#include "algocraft/strategies/ema_crossover.hpp"

#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {

void EmaCrossover::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  fast_ = &lib.get<Ema>(config.symbol_id, BarResolution::OneMin, config.ema_fast);
  slow_ = &lib.get<Ema>(config.symbol_id, BarResolution::OneMin, config.ema_slow);
}

void EmaCrossover::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                          std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || fast_ == nullptr || slow_ == nullptr) {
    return;
  }
  if (!fast_->ready() || !slow_->ready()) {
    return;
  }

  const bool fast_above = fast_->value() > slow_->value();
  if (have_prev_) {
    if (!prev_fast_above_ && fast_above && portfolio.position.shares() == 0) {
      out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy,
                                config_.order_qty));
    } else if (prev_fast_above_ && !fast_above && portfolio.position.shares() > 0) {
      out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell,
                                portfolio.position));
    }
  }
  prev_fast_above_ = fast_above;
  have_prev_ = true;
}

void EmaCrossover::on_fill(const FillEvent& fill) { (void)fill; }

StrategyMetadata EmaCrossover::metadata() const {
  return {.name = "ema_crossover",
          .version = "1.0.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {"EMA"}};
}

}  // namespace algocraft
