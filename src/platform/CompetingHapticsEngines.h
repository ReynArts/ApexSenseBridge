#pragma once

#include <string>
#include <vector>

namespace asb::platform {

[[nodiscard]] std::vector<std::string> findCompetingHapticsEngines(
    const std::vector<std::wstring>& runningExecutables);

[[nodiscard]] std::string competingHapticsEnginesWarning(
    const std::vector<std::string>& engines);

} // namespace asb::platform
