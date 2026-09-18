#include <gtest/gtest.h>
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "algocraft/auth/auth_service.hpp"
#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/sqlite_database.hpp"

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_auth" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

class AuthServiceTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_ = make_temp_dir();
    algocraft::PersistenceConfig cfg;
    cfg.db_path = dir_ / "auth.db";
    cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
    db_ = std::make_unique<algocraft::SqliteDatabase>(cfg);
    db_->open();
    db_->migrate();
    auth_ = std::make_unique<algocraft::AuthService>(db_->handle(), "test-secret", 3600);
  }

  void TearDown() override {
    auth_.reset();
    db_.reset();
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path dir_;
  std::unique_ptr<algocraft::SqliteDatabase> db_;
  std::unique_ptr<algocraft::AuthService> auth_;
};

TEST_F(AuthServiceTest, RegisterAndLogin) {
  const auto reg = auth_->register_user("alice", "secret1");
  ASSERT_TRUE(reg.ok) << reg.error;
  EXPECT_FALSE(reg.token.empty());
  EXPECT_EQ(reg.user.username, "alice");

  const auto login = auth_->login("alice", "secret1");
  ASSERT_TRUE(login.ok) << login.error;
  EXPECT_EQ(login.user.id, reg.user.id);

  const auto claims = auth_->validate_token(login.token);
  ASSERT_TRUE(claims.has_value());
  EXPECT_EQ(claims->user_id, reg.user.id);
  EXPECT_EQ(claims->role, "user");
}

TEST_F(AuthServiceTest, BadPasswordRejected) {
  ASSERT_TRUE(auth_->register_user("bob", "secret1").ok);
  const auto bad = auth_->login("bob", "wrongpass");
  EXPECT_FALSE(bad.ok);
  EXPECT_EQ(bad.error, "invalid credentials");
}

TEST_F(AuthServiceTest, ExpiredTokenRejected) {
  algocraft::AuthService short_ttl(db_->handle(), "test-secret", /*ttl=*/1);
  const auto reg = short_ttl.register_user("carol", "secret1");
  ASSERT_TRUE(reg.ok);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_FALSE(short_ttl.validate_token(reg.token).has_value());
}

TEST_F(AuthServiceTest, DuplicateUsernameRejected) {
  ASSERT_TRUE(auth_->register_user("dave", "secret1").ok);
  const auto again = auth_->register_user("dave", "secret2");
  EXPECT_FALSE(again.ok);
}
