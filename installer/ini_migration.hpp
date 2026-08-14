#pragma once

#include <cstddef>
#include <string>

namespace InstallerConfig {

struct MigrationResult {
    std::string text;
    std::size_t replacements = 0;
};

MigrationResult EnableResetRestartFallback(std::string text);

} // namespace InstallerConfig
