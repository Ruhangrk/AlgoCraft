#pragma once

#include <cstdint>

#include "algocraft/indicators/indicator.hpp"

namespace algocraft {

class Vwap final : public Indicator {
public:
  static constexpr const char* kTypeName = "VWAP";

  Vwap() = default;

  void update(const BarEvent& bar) override;
  [[nodiscard]] double value() const override;
  [[nodiscard]] bool ready() const override { return volume_sum_ > 0; }

private:
  std::int64_t session_day_{-1};
  double pv_sum_{0};
  double volume_sum_{0};
};

}  // namespace algocraft
