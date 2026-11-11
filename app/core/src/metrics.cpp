#include "class_t4/core/metrics.hpp"

#include "class_t4/core/build_info.hpp"

#include <cmath>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace class_t4::core {
namespace {

// Label values escape backslash, double quote and newline.
void append_label_value(std::string &out, std::string_view value) {
  for (const char c : value) {
    switch (c) {
    case '\\':
      out += R"(\\)";
      break;
    case '"':
      out += R"(\")";
      break;
    case '\n':
      out += R"(\n)";
      break;
    default:
      out += c;
    }
  }
}

// HELP text escapes only backslash and newline.
void append_help(std::string &out, std::string_view help) {
  for (const char c : help) {
    if (c == '\\') {
      out += R"(\\)";
    } else if (c == '\n') {
      out += R"(\n)";
    } else {
      out += c;
    }
  }
}

void append_value(std::string &out, double value) {
  if (std::isnan(value)) {
    out += "NaN";
  } else if (std::isinf(value)) {
    out += value > 0 ? "+Inf" : "-Inf";
  } else {
    std::format_to(std::back_inserter(out), "{}", value);
  }
}

} // namespace

std::string render_prometheus(std::span<const metric_family> families) {
  std::string out;
  for (const auto &family : families) {
    out += "# HELP ";
    out += family.name;
    out += ' ';
    append_help(out, family.help);
    out += "\n# TYPE ";
    out += family.name;
    out += family.type == metric_type::counter ? " counter\n" : " gauge\n";
    for (const auto &sample : family.samples) {
      out += family.name;
      if (!sample.labels.empty()) {
        out += '{';
        bool first = true;
        for (const auto &[name, value] : sample.labels) {
          if (!first) {
            out += ',';
          }
          first = false;
          out += name;
          out += "=\"";
          append_label_value(out, value);
          out += '"';
        }
        out += '}';
      }
      out += ' ';
      append_value(out, sample.value);
      out += '\n';
    }
  }
  return out;
}

metric_family build_info_metric(const build_info &info) {
  return metric_family{
      .name = "class_t4_build_info",
      .help = "Build information for the running class-t4 binary.",
      .type = metric_type::gauge,
      .samples = {metric_sample{
          .labels = {{"version", info.version}, {"commit", info.commit}},
          .value = 1.0}},
  };
}

} // namespace class_t4::core
