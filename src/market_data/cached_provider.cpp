#include "algocraft/market_data/cached_provider.hpp"

#include <utility>

namespace algocraft {

CachedProvider::CachedProvider(std::unique_ptr<DataProvider> inner, BarStore& store,
                               CoverageRepository& coverage, const SymbolTable& symbols)
    : inner_(std::move(inner)),
      fetch_(store, coverage, inner_->historical_loader(), symbols, std::string{inner_->name()}) {}

}  // namespace algocraft
