#pragma once

#include "class_t4/core/build_info.hpp"

#include <string>

namespace class_t4::core {

// JSON body for /health. Carries the version so a deploy health gate can
// confirm the expected release is the one answering.
std::string health_body(const build_info &info);

} // namespace class_t4::core
