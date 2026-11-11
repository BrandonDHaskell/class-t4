#include "class_t4/core/health.hpp"

#include <gtest/gtest.h>
#include <json/reader.h>
#include <json/value.h>

#include <memory>
#include <string>

namespace {

TEST(health_body, is_compact_json_with_status_and_version) {
  const auto body =
      class_t4::core::health_body({.version = "1.2.3", .commit = "abc"});

  EXPECT_EQ(body.find('\n'), std::string::npos);

  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  Json::Value parsed;
  std::string errors;
  ASSERT_TRUE(
      reader->parse(body.data(), body.data() + body.size(), &parsed, &errors))
      << errors;
  ASSERT_TRUE(parsed.isObject());
  EXPECT_EQ(parsed.size(), 2U);
  EXPECT_EQ(parsed["status"].asString(), "ok");
  EXPECT_EQ(parsed["version"].asString(), "1.2.3");
}

TEST(health_body, escapes_the_version) {
  const auto body =
      class_t4::core::health_body({.version = R"(1"2\3)", .commit = ""});

  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  Json::Value parsed;
  std::string errors;
  ASSERT_TRUE(
      reader->parse(body.data(), body.data() + body.size(), &parsed, &errors))
      << errors;
  EXPECT_EQ(parsed["version"].asString(), R"(1"2\3)");
}

} // namespace
