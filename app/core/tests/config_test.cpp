#include "class_t4/core/config.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace {

using class_t4::core::config;
using class_t4::core::load_config;
using class_t4::core::log_level;
using class_t4::core::parse_config;

// Smallest valid document: only the TLS paths are required.
constexpr std::string_view minimal =
    R"({"https": {"cert_file": "/c.pem", "key_file": "/k.pem"}})";

// Builds a document with the given https members added to the required ones
// and the given extra top-level members.
std::string with(std::string_view https_extra,
                 std::string_view top_extra = {}) {
  return std::format(
      R"({{"https": {{"cert_file": "/c.pem", "key_file": "/k.pem"{}{}}}{}{}}})",
      https_extra.empty() ? "" : ", ", https_extra,
      top_extra.empty() ? "" : ", ", top_extra);
}

// Expects parsing to fail with a message containing every fragment.
void expect_error(std::string_view json, std::string_view fragment) {
  const auto parsed = parse_config(json);
  ASSERT_FALSE(parsed.has_value()) << "accepted: " << json;
  EXPECT_NE(parsed.error().message.find(fragment), std::string::npos)
      << "message: " << parsed.error().message;
}

TEST(config_parse, full_document) {
  const auto parsed = parse_config(R"({
    "https": {"address": "192.168.1.10", "port": 9443,
              "cert_file": "/etc/c.pem", "key_file": "/etc/k.pem"},
    "ops": {"address": "::1", "port": 9100},
    "threads": 4,
    "log_level": "debug"
  })");
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
  const config &c = *parsed;
  EXPECT_EQ(c.https.address, "192.168.1.10");
  EXPECT_EQ(c.https.port, 9443);
  EXPECT_EQ(c.https.cert_file, "/etc/c.pem");
  EXPECT_EQ(c.https.key_file, "/etc/k.pem");
  EXPECT_EQ(c.ops.address, "::1");
  EXPECT_EQ(c.ops.port, 9100);
  EXPECT_EQ(c.threads, 4U);
  EXPECT_EQ(c.log, log_level::debug);
}

TEST(config_parse, defaults_apply_to_minimal_document) {
  const auto parsed = parse_config(minimal);
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
  EXPECT_EQ(parsed->https.address, "0.0.0.0");
  EXPECT_EQ(parsed->https.port, 8443);
  EXPECT_EQ(parsed->ops.address, "0.0.0.0");
  EXPECT_EQ(parsed->ops.port, 9464);
  EXPECT_EQ(parsed->threads, 2U);
  EXPECT_EQ(parsed->log, log_level::info);
}

TEST(config_parse, example_config_in_repo_is_valid) {
  const auto loaded = load_config(CLASS_T4_EXAMPLE_CONFIG);
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  EXPECT_EQ(loaded->https.port, 8443);
  EXPECT_EQ(loaded->ops.port, 9464);
}

TEST(config_parse, rejects_malformed_documents) {
  expect_error("", "invalid JSON");
  expect_error("{", "invalid JSON");
  expect_error(R"({"https": {}} trailing)", "invalid JSON");
  expect_error("[]", "top level must be a JSON object");
  // jsoncpp's strict mode already rejects a scalar root as invalid JSON.
  expect_error(R"("text")", "invalid JSON");
}

TEST(config_parse, parse_errors_are_one_line) {
  const auto parsed = parse_config("{\n\"https\": ,}");
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().message.find('\n'), std::string::npos)
      << parsed.error().message;
  EXPECT_NE(parsed.error().message.find("Line 2"), std::string::npos)
      << parsed.error().message;
}

TEST(config_parse, rejects_duplicate_keys) {
  expect_error(
      R"({"https": {"cert_file": "/c", "key_file": "/k"}, "threads": 1, "threads": 2})",
      "invalid JSON");
}

TEST(config_parse, requires_https_and_tls_paths) {
  expect_error("{}", "https: is required");
  expect_error(R"({"https": 1})", "https: must be an object");
  expect_error(R"({"https": {"key_file": "/k"}})",
               "https.cert_file: is required");
  expect_error(R"({"https": {"cert_file": "/c"}})",
               "https.key_file: is required");
  expect_error(R"({"https": {"cert_file": "", "key_file": "/k"}})",
               "https.cert_file: must be a non-empty string");
  expect_error(R"({"https": {"cert_file": 7, "key_file": "/k"}})",
               "https.cert_file: must be a non-empty string");
}

TEST(config_parse, rejects_unknown_keys_at_every_level) {
  expect_error(with("", R"("thread": 2)"), "thread: unknown key");
  expect_error(with(R"("prot": 1)"), "https.prot: unknown key");
  expect_error(with("", R"("ops": {"adress": "::"})"),
               "ops.adress: unknown key");
}

TEST(config_parse, validates_ports) {
  expect_error(with(R"("port": 0)"),
               "https.port: must be an integer between 1 and 65535");
  expect_error(with(R"("port": 65536)"), "https.port");
  expect_error(with(R"("port": -1)"), "https.port");
  expect_error(with(R"("port": "8443")"), "https.port");
  expect_error(with(R"("port": 8443.5)"), "https.port");
  expect_error(with("", R"("ops": {"port": 70000})"), "ops.port");
  expect_error(with("", R"("ops": 5)"), "ops: must be an object");

  const auto edge =
      parse_config(with(R"("port": 65535)", R"("ops": {"port": 1})"));
  ASSERT_TRUE(edge.has_value()) << edge.error().message;
  EXPECT_EQ(edge->https.port, 65535);
  EXPECT_EQ(edge->ops.port, 1);
}

TEST(config_parse, rejects_same_port_for_both_listeners) {
  expect_error(with(R"("port": 9000)", R"("ops": {"port": 9000})"),
               "ops.port: must differ from https.port");
  expect_error(with("", R"("ops": {"port": 8443})"), "ops.port: must differ");
}

TEST(config_parse, validates_addresses) {
  for (const auto *good : {"127.0.0.1", "0.0.0.0", "::", "::1", "fe80::1"}) {
    const auto parsed =
        parse_config(with(std::format(R"("address": "{}")", good)));
    EXPECT_TRUE(parsed.has_value()) << good;
  }
  for (const auto *bad :
       {"localhost", "256.1.1.1", "1.2.3", "::g", " 127.0.0.1"}) {
    expect_error(with(std::format(R"("address": "{}")", bad)),
                 "https.address: must be an IPv4 or IPv6 address literal");
  }
  expect_error(with("", R"("ops": {"address": "lan"})"), "ops.address");
  expect_error(with(R"("address": "")"),
               "https.address: must be a non-empty string");
}

TEST(config_parse, validates_threads) {
  expect_error(with("", R"("threads": 0)"),
               "threads: must be an integer between 1 and 64");
  expect_error(with("", R"("threads": 65)"), "threads");
  expect_error(with("", R"("threads": -1)"), "threads");
  expect_error(with("", R"("threads": "2")"), "threads");

  const auto max = parse_config(with("", R"("threads": 64)"));
  ASSERT_TRUE(max.has_value()) << max.error().message;
  EXPECT_EQ(max->threads, 64U);
}

TEST(config_parse, accepts_every_log_level) {
  using level_case = std::pair<std::string_view, log_level>;
  const std::array levels{
      level_case{"trace", log_level::trace},
      level_case{"debug", log_level::debug},
      level_case{"info", log_level::info},
      level_case{"warn", log_level::warn},
      level_case{"error", log_level::error},
  };
  for (const auto &[text, level] : levels) {
    const auto parsed =
        parse_config(with("", std::format(R"("log_level": "{}")", text)));
    ASSERT_TRUE(parsed.has_value()) << text;
    EXPECT_EQ(parsed->log, level) << text;
  }
  expect_error(with("", R"("log_level": "verbose")"),
               "log_level: must be one of");
  expect_error(with("", R"("log_level": "INFO")"), "log_level");
  expect_error(with("", R"("log_level": 3)"), "log_level");
}

class config_file : public ::testing::Test {
protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           std::format("class-t4-config-test-{}", ::getpid());
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  std::filesystem::path write(std::string_view name,
                              std::string_view contents) {
    const auto path = dir_ / name;
    std::ofstream{path} << contents;
    return path;
  }

  std::filesystem::path dir_;
};

TEST_F(config_file, loads_a_file) {
  const auto loaded = load_config(write("ok.json", minimal));
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  EXPECT_EQ(loaded->https.cert_file, "/c.pem");
}

TEST_F(config_file, reports_missing_file) {
  const auto loaded = load_config(dir_ / "missing.json");
  ASSERT_FALSE(loaded.has_value());
  EXPECT_NE(loaded.error().message.find("cannot open"), std::string::npos);
  EXPECT_NE(loaded.error().message.find("missing.json"), std::string::npos);
}

TEST_F(config_file, reports_parse_errors_from_file) {
  const auto loaded = load_config(write("bad.json", "{"));
  ASSERT_FALSE(loaded.has_value());
  EXPECT_NE(loaded.error().message.find("invalid JSON"), std::string::npos);
}

} // namespace
