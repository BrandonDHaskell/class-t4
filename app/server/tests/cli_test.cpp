#include "cli/cli.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace class_t4::server::cli;

auto parse_args(std::initializer_list<std::string_view> args) {
  const std::vector<std::string_view> list{args};
  return parse(list);
}

void expect_error(std::initializer_list<std::string_view> args,
                  std::string_view fragment) {
  const auto parsed = parse_args(args);
  ASSERT_FALSE(parsed.has_value());
  EXPECT_NE(parsed.error().message.find(fragment), std::string::npos)
      << "message: " << parsed.error().message;
}

TEST(cli_parse, serve_with_separate_config_argument) {
  const auto parsed = parse_args({"serve", "--config", "/etc/x.json"});
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
  const auto *serve = std::get_if<serve_command>(&*parsed);
  ASSERT_NE(serve, nullptr);
  EXPECT_EQ(serve->config_path, "/etc/x.json");
}

TEST(cli_parse, serve_with_equals_config_argument) {
  const auto parsed = parse_args({"serve", "--config=/etc/x.json"});
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
  EXPECT_EQ(std::get<serve_command>(*parsed).config_path, "/etc/x.json");
}

TEST(cli_parse, serve_errors) {
  expect_error({"serve"}, "--config <path> is required");
  expect_error({"serve", "--config"}, "--config needs a path");
  expect_error({"serve", "--config="}, "--config <path> is required");
  expect_error({"serve", "--config", "/x", "extra"},
               "unexpected argument 'extra'");
  expect_error({"serve", "--verbose"}, "unexpected argument '--verbose'");
}

TEST(cli_parse, version_and_help) {
  for (const auto arg : {"--version", "version"}) {
    const auto parsed = parse_args({arg});
    ASSERT_TRUE(parsed.has_value()) << arg;
    EXPECT_TRUE(std::holds_alternative<version_command>(*parsed)) << arg;
  }
  for (const auto arg : {"--help", "-h", "help"}) {
    const auto parsed = parse_args({arg});
    ASSERT_TRUE(parsed.has_value()) << arg;
    EXPECT_TRUE(std::holds_alternative<help_command>(*parsed)) << arg;
  }
}

TEST(cli_parse, rejects_extra_arguments_to_version_and_help) {
  expect_error({"--version", "x"}, "unknown command '--version'");
  expect_error({"--help", "x"}, "unknown command '--help'");
}

TEST(cli_parse, rejects_missing_and_unknown_commands) {
  expect_error({}, "missing command");
  expect_error({"migrate"}, "unknown command 'migrate'");
  expect_error({"--config", "/x"}, "unknown command '--config'");
}

TEST(cli_usage, lists_every_command) {
  const auto text = usage_text();
  for (const auto *command : {"serve --config <path>", "--version", "--help"}) {
    EXPECT_NE(text.find(command), std::string_view::npos) << command;
  }
}

} // namespace
