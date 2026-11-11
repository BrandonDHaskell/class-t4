#pragma once

#include <string>

namespace class_t4::core {

// Identity of the running binary. Values are compiled into the server target
// only, so the core library does not rebuild on every commit.
struct build_info {
  std::string version;
  std::string commit;
};

} // namespace class_t4::core
