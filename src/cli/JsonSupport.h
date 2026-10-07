#pragma once

#include <string>
#include <string_view>

namespace asb::cli {

[[nodiscard]] std::string jsonEscape(std::string_view value);

} // namespace asb::cli
