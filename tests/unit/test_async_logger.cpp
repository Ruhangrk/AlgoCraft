#include "algocraft/persistence/async_logger.hpp"

#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

TEST(AsyncLogger, PushFromProducerPopFromConsumer) {
  algocraft::AsyncLogger::Ring ring;
  algocraft::AsyncLogger logger(ring);

  std::thread producer([&] {
    ASSERT_TRUE(logger.try_log(algocraft::LogLevel::Warn, "hello"));
  });
  producer.join();

  algocraft::LogEvent event{};
  ASSERT_TRUE(ring.try_pop(event));
  EXPECT_EQ(event.level, algocraft::LogLevel::Warn);
  EXPECT_EQ(std::string_view(event.message.data(), event.length), "hello");
}

TEST(AsyncLogger, TruncatesLongMessage) {
  algocraft::AsyncLogger::Ring ring;
  algocraft::AsyncLogger logger(ring);
  const std::string long_msg(300, 'x');
  ASSERT_TRUE(logger.try_log(algocraft::LogLevel::Info, long_msg));

  algocraft::LogEvent event{};
  ASSERT_TRUE(ring.try_pop(event));
  EXPECT_EQ(event.length, event.message.size());
}
