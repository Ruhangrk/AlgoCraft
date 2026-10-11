#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/persistence/strategy_catalog_repository.hpp"
#include "algocraft/strategies/strategy_compiler.hpp"

#include <filesystem>
#include <gtest/gtest.h>

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

#ifndef ALGOCRAFT_SOURCE_DIR
#define ALGOCRAFT_SOURCE_DIR "."
#endif

TEST(StrategyCompiler, ValidNameAndClassName) {
  EXPECT_TRUE(algocraft::StrategyCompiler::valid_name("live_run_testing"));
  EXPECT_FALSE(algocraft::StrategyCompiler::valid_name("LiveRun"));
  EXPECT_FALSE(algocraft::StrategyCompiler::valid_name(""));
  EXPECT_FALSE(algocraft::StrategyCompiler::valid_name("1bad"));
  EXPECT_EQ(algocraft::StrategyCompiler::derive_class_name("live_run_testing"), "LiveRunTesting");
  EXPECT_EQ(algocraft::StrategyCompiler::derive_class_name("foo"), "Foo");
}

TEST(StrategyCompiler, CompileRejectsPrivateQuantityCtor) {
  const auto sandbox =
      std::filesystem::temp_directory_path() / "algocraft_agent_compile_bad";
  std::filesystem::remove_all(sandbox);
  algocraft::StrategyCompiler compiler({.source_root = ALGOCRAFT_SOURCE_DIR,
                                        .sandbox_root = sandbox});
  algocraft::StrategyCompiler::Request req;
  req.name = "agent_unit_bad";
  req.class_name = "AgentUnitBad";
  req.hpp = R"(#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
class AgentUnitBad final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override {}
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override {}
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override {
    return {"agent_unit_bad", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
  }
};
}
)";
  req.cpp = R"(#include "algocraft/strategies/agent_unit_bad.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
void AgentUnitBad::on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>& out) {
  out.push_back(make_intent(StrategyId::from(1), SymbolId{1}, Side::Buy, Quantity{1}));
}
}
)";
  const auto result = compiler.compile(req);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.log.find("Quantity"), std::string::npos);
  std::filesystem::remove_all(sandbox);
}

TEST(StrategyCompiler, CompileAcceptsFromShares) {
  const auto sandbox =
      std::filesystem::temp_directory_path() / "algocraft_agent_compile_ok";
  std::filesystem::remove_all(sandbox);
  algocraft::StrategyCompiler compiler({.source_root = ALGOCRAFT_SOURCE_DIR,
                                        .sandbox_root = sandbox});
  algocraft::StrategyCompiler::Request req;
  req.name = "agent_unit_ok";
  req.class_name = "AgentUnitOk";
  req.hpp = R"(#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
class AgentUnitOk final : public Strategy {
public:
  void configure(const StrategyConfig& c, IndicatorLibrary&) override { config_ = c; }
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override {}
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override {
    return {"agent_unit_ok", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
  }
private:
  StrategyConfig config_{};
};
}
)";
  req.cpp = R"(#include "algocraft/strategies/agent_unit_ok.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
void AgentUnitOk::on_bar(const BarEvent& bar, const PortfolioView& pf,
                         std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  if (pf.position.shares() == 0) {
    out.push_back(make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy,
                              Quantity::from_shares(1)));
  }
}
}
)";
  const auto result = compiler.compile(req);
  EXPECT_TRUE(result.ok) << result.log;
  std::filesystem::remove_all(sandbox);
}

TEST(StrategyCatalogRepository, UpsertPromoteThenActivate) {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = ":memory:";
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();

  algocraft::StrategyCatalogRepository repo(db.handle());
  algocraft::StrategyCatalogRepository::Row row{};
  row.kind = "strategy";
  row.name = "agent_demo";
  row.class_name = "AgentDemo";
  row.hpp_path = "include/algocraft/strategies/agent_demo.hpp";
  row.cpp_path = "src/strategies/agent_demo.cpp";
  row.sandbox_path = "data/agent_sandbox/agent_demo";
  row.compile_ok = true;
  row.provenance = "agent";

  const auto id = repo.upsert_promoted(row);
  EXPECT_GT(id, 0);

  auto found = repo.find_by_name("agent_demo");
  ASSERT_TRUE(found);
  EXPECT_FALSE(found->enabled);
  EXPECT_EQ(found->class_name, "AgentDemo");

  EXPECT_TRUE(repo.set_enabled("agent_demo", true));
  found = repo.find_by_name("agent_demo");
  ASSERT_TRUE(found);
  EXPECT_TRUE(found->enabled);

  const auto enabled = repo.list_enabled();
  ASSERT_EQ(enabled.size(), 1u);
  EXPECT_EQ(enabled[0].name, "agent_demo");

  EXPECT_TRUE(repo.set_enabled("agent_demo", false));
  EXPECT_TRUE(repo.list_enabled().empty());
}
