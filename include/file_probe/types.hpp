#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace file_probe {
    struct CliParseResult {
        bool valid = true;
        bool show_help = false;
        bool json_output = false;
        std::optional<std::string> path;
        std::string error_message;
    };

    struct SymlinkInfo {
        bool is_symlink = false;
        std::optional<std::string> target;
        std::optional<std::string> error;
    };

    struct OwnershipInfo {
        std::string owner;
        std::string group;
    };

    struct TimeInfo {
        std::string last_access;
        std::string last_modify;
        std::string last_change;
    };

    struct FileDetail {
        uintmax_t size_bytes = 0;
        std::string size_human;
        std::string checksum;
        std::optional<std::string> resolution;
        std::optional<std::string> metadata;
        std::optional<std::string> duration;
    };

    struct DirectoryDetail {
        uintmax_t total_size_bytes = 0;
        std::string total_size_human;
        size_t file_count = 0;
        size_t directory_count = 0;
    };

    struct FileReport {
        std::filesystem::path input_path;
        std::filesystem::path absolute_path;
        bool target_exists = false;
        std::string type = "Unknown";
        SymlinkInfo symlink;
        std::optional<std::string> permissions;
        std::optional<OwnershipInfo> ownership;
        std::optional<TimeInfo> timestamps;
        std::optional<FileDetail> file_detail;
        std::optional<DirectoryDetail> directory_detail;
        std::vector<std::string> warnings;
    };
}
