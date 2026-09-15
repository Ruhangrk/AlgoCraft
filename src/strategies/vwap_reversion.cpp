#include "algocraft/strategies/vwap_reversion.hpp"

#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {

void VwapReversion::configure(const StrategyConfig& config, IndicatorLibrary& lib) {
  config_ = config;
  vwap_ = &lib.get<Vwap>(config.symbol_id, BarResolution::OneMin);
}

void VwapReversion::on_bar(const BarEvent& bar, const PortfolioView& portfolio,
                           std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || vwap_ == nullptr || !vwap_->ready()) {
    return;
  }

  const auto close = static_cast<double>(bar.close.paise());
  const auto vwap = vwap_->value();
  const auto dev = close - vwap;
  const auto thresh = static_cast<double>(config_.vwap_dev_paise);

  if (portfolio.position.shares() == 0 && dev <= -thresh) {
    out.push_back(make_intent(StrategyId::from(2), config_.symbol_id, Side::Buy, config_.order_qty));
  } else if (portfolio.position.shares() > 0 && dev >= thresh) {
    out.push_back(
        make_intent(StrategyId::from(2), config_.symbol_id, Side::Sell, portfolio.position));
  }
}

void VwapReversion::on_fill(const FillEvent& fill) { (void)fill; }

StrategyMetadata VwapReversion::metadata() const {
  return {.name = "vwap_reversion",
          .version = "1.0.0",
          .trading_mode = TradingMode::Mis,
          .required_resolution = BarResolution::OneMin,
          .required_indicators = {"VWAP"}};
}

}  // namespace algocraft
