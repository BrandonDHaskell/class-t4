#pragma once

#include "class_t4/core/build_info.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace class_t4::core {

enum class metric_type : std::uint8_t { gauge, counter };

struct metric_sample {
  std::vector<std::pair<std::string, std::string>> labels;
  double value = 0.0;
};

struct metric_family {
  std::string name;
  std::string help;
  metric_type type = metric_type::gauge;
  std::vector<metric_sample> samples;
};

// Renders families in the Prometheus text exposition format (0.0.4).
std::string render_prometheus(std::span<const metric_family> families);

// class_t4_build_info{version, commit} 1
metric_family build_info_metric(const build_info &info);

} // namespace class_t4::core
