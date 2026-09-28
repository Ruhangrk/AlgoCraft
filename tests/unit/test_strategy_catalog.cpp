#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/persistence/strategy_catalog_repository.hpp"
#include "algocraft/strategies/strategy_compiler.hpp"

#include <gtest/gtest.h>

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

TEST(StrategyCompiler, ValidNameAndClassName) {
  EXPECT_TRUE(algocraft::StrategyCompiler::valid_name("live_run_testing"));
  EXPECT_FALSE(algocraft::StrategyCompiler::valid_name("LiveRun"));
  EXPECT_FALSE(algocraft::StrategyCompiler::valid_name(""));
  EXPECT_FALSE(algocraft::StrategyCompiler::valid_name("1bad"));
  EXPECT_EQ(algocraft::StrategyCompiler::derive_class_name("live_run_testing"), "LiveRunTesting");
  EXPECT_EQ(algocraft::StrategyCompiler::derive_class_name("foo"), "Foo");
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
