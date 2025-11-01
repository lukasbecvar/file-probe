#pragma once
#include <filesystem>
#include "file_probe/types.hpp"

namespace file_probe {
    FileReport collect_file_report(const std::filesystem::path& path);
} 
