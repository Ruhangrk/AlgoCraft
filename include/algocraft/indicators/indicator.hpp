#pragma once

#include "algocraft/domain/bar_event.hpp"

namespace algocraft {

class Indicator {
public:
  virtual ~Indicator() = default;

  virtual void update(const BarEvent& bar) = 0;
  [[nodiscard]] virtual double value() const = 0;
  [[nodiscard]] virtual bool ready() const = 0;
};

}  // namespace algocraft
