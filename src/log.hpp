#pragma once

#include "stdafx.h"

namespace Log {
void Init(const std::filesystem::path& path, bool enabled, std::uint64_t max_bytes, int max_files);
bool Enabled();
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);
void Shutdown();
}
