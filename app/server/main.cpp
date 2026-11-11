// Placeholder entry point for the M1 build skeleton. It links Drogon so the
// release link checks cover the real dependency set; the composition root
// replaces it in the next step.
#include <drogon/HttpAppFramework.h>

#include <cstdio>
#include <exception>
#include <print>
#include <string_view>

int main(int argc, char **argv) {
  try {
    if (argc > 1 && std::string_view{argv[1]} == "--version") {
      std::println("class-t4 {} ({}) drogon {}", CLASS_T4_VERSION,
                   CLASS_T4_COMMIT, drogon::getVersion());
      return 0;
    }
    std::println(stderr, "usage: class-t4 --version");
    return 2;
  } catch (const std::exception &e) {
    // Nothing useful is left to do if writing the error fails.
    static_cast<void>(std::fputs(e.what(), stderr));
    return 1;
  }
}
