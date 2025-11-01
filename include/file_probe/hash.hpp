#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace file_probe {
    std::optional<std::string> compute_sha256(const std::filesystem::path& path);
}
