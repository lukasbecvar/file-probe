#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace file_probe {
    std::string format_size(uintmax_t size);
    std::string format_permissions(std::filesystem::perms perms);
    std::string format_time(std::time_t value);
    bool is_text_file(const std::filesystem::path& path);
    std::string json_escape(const std::string& input);
}
