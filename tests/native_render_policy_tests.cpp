#include "native_render_policy.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  try {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    constexpr auto option = "radeonsi_inline_uniforms";
    ::unsetenv(option);
    observatory::configureNativeRenderPolicy();
    if (!std::getenv(option) || std::string(std::getenv(option)) != "false")
      throw std::runtime_error(
          "default native policy must avoid live uniform specialization");
    for (const char *overrideValue : {"true", "false", ""}) {
      ::setenv(option, overrideValue, 1);
      observatory::configureNativeRenderPolicy();
      if (std::string(std::getenv(option)) != overrideValue)
        throw std::runtime_error(
            "explicit operator policy must remain unchanged");
    }
#endif
    std::cout << "Native render policy and operator overrides passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
