#include "class_t4/core/metrics.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <span>
#include <string>

namespace {

using class_t4::core::metric_family;
using class_t4::core::metric_sample;
using class_t4::core::metric_type;
using class_t4::core::render_prometheus;

std::string render(const metric_family &family) {
  return render_prometheus(std::span{&family, 1});
}

TEST(render_prometheus, empty_input_renders_nothing) {
  EXPECT_EQ(render_prometheus({}), "");
}

TEST(render_prometheus, gauge_with_labels) {
  const metric_family family{
      .name = "x_items",
      .help = "Items.",
      .type = metric_type::gauge,
      .samples = {{.labels = {{"a", "1"}, {"b", "two"}}, .value = 3.0}},
  };
  EXPECT_EQ(render(family), "# HELP x_items Items.\n"
                            "# TYPE x_items gauge\n"
                            "x_items{a=\"1\",b=\"two\"} 3\n");
}

TEST(render_prometheus, counter_without_labels_and_fractional_value) {
  const metric_family family{
      .name = "x_total",
      .help = "Total.",
      .type = metric_type::counter,
      .samples = {{.labels = {}, .value = 0.25}},
  };
  EXPECT_EQ(render(family), "# HELP x_total Total.\n"
                            "# TYPE x_total counter\n"
                            "x_total 0.25\n");
}

TEST(render_prometheus, family_with_no_samples_keeps_metadata) {
  const metric_family family{.name = "x", .help = "h", .samples = {}};
  EXPECT_EQ(render(family), "# HELP x h\n# TYPE x gauge\n");
}

TEST(render_prometheus, escapes_label_values) {
  const metric_family family{
      .name = "x",
      .help = "h",
      .samples = {{.labels = {{"v", "a\\b\"c\nd"}}, .value = 1.0}},
  };
  EXPECT_EQ(render(family),
            "# HELP x h\n# TYPE x gauge\nx{v=\"a\\\\b\\\"c\\nd\"} 1\n");
}

TEST(render_prometheus, escapes_help_but_not_quotes) {
  const metric_family family{.name = "x", .help = "a\\b\"c\nd", .samples = {}};
  EXPECT_EQ(render(family), "# HELP x a\\\\b\"c\\nd\n# TYPE x gauge\n");
}

TEST(render_prometheus, special_float_values) {
  constexpr auto inf = std::numeric_limits<double>::infinity();
  const metric_family family{
      .name = "x",
      .help = "h",
      .samples = {{.labels = {{"k", "nan"}},
                   .value = std::numeric_limits<double>::quiet_NaN()},
                  {.labels = {{"k", "pos"}}, .value = inf},
                  {.labels = {{"k", "neg"}}, .value = -inf}},
  };
  EXPECT_EQ(render(family), "# HELP x h\n# TYPE x gauge\n"
                            "x{k=\"nan\"} NaN\n"
                            "x{k=\"pos\"} +Inf\n"
                            "x{k=\"neg\"} -Inf\n");
}

TEST(render_prometheus, multiple_families_in_order) {
  const std::array families{
      metric_family{
          .name = "a", .help = "A", .samples = {{.labels = {}, .value = 1.0}}},
      metric_family{
          .name = "b", .help = "B", .samples = {{.labels = {}, .value = 2.0}}},
  };
  EXPECT_EQ(render_prometheus(families), "# HELP a A\n# TYPE a gauge\na 1\n"
                                         "# HELP b B\n# TYPE b gauge\nb 2\n");
}

TEST(build_info_metric, renders_version_and_commit) {
  const auto family = class_t4::core::build_info_metric(
      {.version = "0.1.0", .commit = "0123456789ab"});
  EXPECT_EQ(render(family),
            "# HELP class_t4_build_info Build information for the running "
            "class-t4 binary.\n"
            "# TYPE class_t4_build_info gauge\n"
            "class_t4_build_info{version=\"0.1.0\",commit=\"0123456789ab\"} "
            "1\n");
}

} // namespace
