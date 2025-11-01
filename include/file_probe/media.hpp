#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace file_probe {
    bool is_image_extension(const std::filesystem::path& path);
    bool is_video_extension(const std::filesystem::path& path);
    bool is_audio_extension(const std::filesystem::path& path);

    std::optional<std::string> image_resolution(const std::filesystem::path& path);
    std::optional<std::string> image_metadata(const std::filesystem::path& path);
    std::optional<std::string> media_resolution(const std::filesystem::path& path);
    std::optional<std::string> media_metadata(const std::filesystem::path& path);
    std::optional<std::string> media_duration(const std::filesystem::path& path);
}
