#include "algocraft/market_data/csv_provider.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {
namespace {

std::int64_t rupees_to_paise(double rupees) {
  return static_cast<std::int64_t>(std::llround(rupees * 100.0));
}

BarEvent parse_csv_line(const std::string& line, SymbolId symbol_id, BarResolution resolution) {
  std::istringstream in(line);
  std::string ts;
  std::string o;
  std::string h;
  std::string l;
  std::string c;
  std::string v;
  if (!std::getline(in, ts, ',') || !std::getline(in, o, ',') || !std::getline(in, h, ',') ||
      !std::getline(in, l, ',') || !std::getline(in, c, ',') || !std::getline(in, v, ',')) {
    throw std::invalid_argument("bad csv row: " + line);
  }

  BarEvent bar{};
  bar.symbol_id = symbol_id;
  bar.timestamp = Timestamp::from_nanos(std::stoll(ts));
  bar.resolution = resolution;
  bar.open = Price::from_paise(rupees_to_paise(std::stod(o)));
  bar.high = Price::from_paise(rupees_to_paise(std::stod(h)));
  bar.low = Price::from_paise(rupees_to_paise(std::stod(l)));
  bar.close = Price::from_paise(rupees_to_paise(std::stod(c)));
  bar.volume = Quantity::from_shares(static_cast<std::int64_t>(std::stoll(v)));
  return bar;
}

}  // namespace

CsvHistoricalLoader::CsvHistoricalLoader(std::filesystem::path data_dir, const SymbolTable* symbols)
    : data_dir_{std::move(data_dir)}, symbols_{symbols} {}

void CsvHistoricalLoader::set_file(SymbolId symbol_id, std::filesystem::path path) {
  files_[symbol_id] = std::move(path);
}

std::vector<BarEvent> CsvHistoricalLoader::load_bars(SymbolId symbol_id, Timestamp from,
                                                     Timestamp to, BarResolution resolution) {
  std::filesystem::path path;
  if (const auto it = files_.find(symbol_id); it != files_.end()) {
    path = it->second;
  } else if (symbols_ != nullptr && symbols_->contains(symbol_id)) {
    path = data_dir_ / (std::string{symbols_->symbol(symbol_id).ticker} + ".csv");
  } else {
    path = data_dir_ / (std::to_string(symbol_id) + ".csv");
  }

  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open csv: " + path.string());
  }

  std::vector<BarEvent> bars;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    if (header) {
      header = false;
      if (line.rfind("timestamp", 0) == 0) {
        continue;
      }
    }
    auto bar = parse_csv_line(line, symbol_id, resolution);
    if (from.nanos() != 0 && bar.timestamp < from) {
      continue;
    }
    if (to.nanos() != 0 && bar.timestamp > to) {
      continue;
    }
    bars.push_back(bar);
  }
  return bars;
}

CsvProvider::CsvProvider(std::filesystem::path data_dir, const SymbolTable* symbols)
    : loader_{std::move(data_dir), symbols} {}

DataProviderCapabilities CsvProvider::capabilities() const {
  return DataProviderCapabilities{
      .supported_resolutions = {BarResolution::OneMin},
      .max_historical_lookback_days = 0,
      .rate_limit_per_second = 0,
  };
}

}  // namespace algocraft
