#include <grp.h>
#include <pwd.h>
#include <array>
#include <cerrno>
#include <vector>
#include <cstring>
#include <optional>
#include <algorithm>
#include <sys/stat.h>
#include <string_view>
#include <system_error>
#include "file_probe/hash.hpp"
#include "file_probe/media.hpp"
#include "file_probe/utils.hpp"
#include "file_probe/collector.hpp"

namespace file_probe {

    namespace {
        using Path = std::filesystem::path;

        constexpr std::array<std::string_view, 11> kTextExtensions = {
            ".txt", ".csv", ".log", ".json", ".xml", ".html", ".htm", ".css", ".js", ".md", ".ini"};
        constexpr std::array<std::string_view, 7> kDocumentExtensions = {
            ".pdf", ".doc", ".docx", ".odt", ".rtf", ".ppt", ".pptx"};
        constexpr std::array<std::string_view, 5> kArchiveExtensions = {
            ".zip", ".rar", ".7z", ".tar", ".gz"};

        std::string to_lowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        template <std::size_t N>
        bool matches_extension(const Path& path, const std::array<std::string_view, N>& extensions) {
            std::string lowered = to_lowercase(path.extension().string());
            return std::any_of(extensions.begin(), extensions.end(), [&](std::string_view ext) {
                return lowered == ext;
            });
        }

        std::string classify_type(const Path& path, bool is_directory) {
            if (is_directory) {
                return "Directory";
            }
            if (is_image_extension(path)) {
                return "Image";
            }
            if (is_video_extension(path)) {
                return "Video";
            }
            if (is_audio_extension(path)) {
                return "Audio";
            }
            if (matches_extension(path, kTextExtensions)) {
                return "Text";
            }
            if (matches_extension(path, kDocumentExtensions)) {
                return "Document";
            }
            if (matches_extension(path, kArchiveExtensions)) {
                return "Archive";
            }

            if (is_text_file(path)) {
                return "Text";
            }

            return "Binary";
        }

        std::optional<OwnershipInfo> read_ownership(const Path& path, std::vector<std::string>& warnings) {
            struct stat info {};
            if (stat(path.c_str(), &info) != 0) {
                warnings.push_back("Unable to read ownership metadata: " + std::string(std::strerror(errno)));
                return std::nullopt;
            }

            OwnershipInfo ownership;
            if (struct passwd* pwd = getpwuid(info.st_uid); pwd && pwd->pw_name) {
                ownership.owner = pwd->pw_name;
            } else {
                ownership.owner = std::to_string(info.st_uid);
            }

            if (struct group* grp = getgrgid(info.st_gid); grp && grp->gr_name) {
                ownership.group = grp->gr_name;
            } else {
                ownership.group = std::to_string(info.st_gid);
            }

            return ownership;
        }

        std::optional<TimeInfo> read_timestamps(const Path& path, std::vector<std::string>& warnings) {
            struct stat info {};
            if (stat(path.c_str(), &info) != 0) {
                warnings.push_back("Unable to read timestamps: " + std::string(std::strerror(errno)));
                return std::nullopt;
            }

            TimeInfo timestamps;
            timestamps.last_access = format_time(info.st_atime);
            timestamps.last_modify = format_time(info.st_mtime);
            timestamps.last_change = format_time(info.st_ctime);
            return timestamps;
        }

        FileDetail collect_file_detail(const Path& path, std::vector<std::string>& warnings) {
            FileDetail detail;

            std::error_code size_ec;
            detail.size_bytes = std::filesystem::file_size(path, size_ec);
            if (size_ec) {
                warnings.push_back("Unable to read file size: " + size_ec.message());
                detail.size_bytes = 0;
            }
            detail.size_human = format_size(detail.size_bytes);

            if (auto checksum = compute_sha256(path)) {
                detail.checksum = *checksum;
            } else {
                detail.checksum = "Unavailable";
                warnings.push_back("Unable to compute SHA-256 checksum.");
            }

            const bool is_image = is_image_extension(path);
            const bool is_video = is_video_extension(path);
            const bool is_audio = is_audio_extension(path);

            if (is_image || is_video) {
                if (auto resolution = is_image ? image_resolution(path) : media_resolution(path)) {
                    detail.resolution = resolution;
                } else if (is_image) {
                    warnings.push_back("Unable to read image resolution.");
                } else if (is_video) {
                    warnings.push_back("Unable to read video resolution.");
                }
            }

            if (is_image) {
                if (auto meta = image_metadata(path)) {
                    detail.metadata = meta;
                } else {
                    warnings.push_back("Unable to read image metadata.");
                }
            } else if (is_audio || is_video) {
                if (auto meta = media_metadata(path)) {
                    detail.metadata = meta;
                } else {
                    warnings.push_back("Unable to read media metadata.");
                }
                if (auto duration = media_duration(path)) {
                    detail.duration = duration;
                } else {
                    warnings.push_back("Unable to read media duration.");
                }
            }

            return detail;
        }

        DirectoryDetail collect_directory_detail(const Path& path, std::vector<std::string>& warnings) {
            DirectoryDetail detail;

            std::error_code iterator_error;
            std::filesystem::recursive_directory_iterator it(
                path, std::filesystem::directory_options::skip_permission_denied, iterator_error);
            std::filesystem::recursive_directory_iterator end;

            if (iterator_error) {
                warnings.push_back("Unable to traverse directory: " + iterator_error.message());
                return detail;
            }

            while (it != end) {
                const auto& entry = *it;
                std::error_code status_error;

                if (entry.is_symlink(status_error)) {
                    it.disable_recursion_pending();
                }
                status_error.clear();

                if (entry.is_regular_file(status_error)) {
                    ++detail.file_count;
                    if (!status_error) {
                        std::error_code size_ec;
                        uintmax_t size = entry.file_size(size_ec);
                        if (!size_ec) {
                            detail.total_size_bytes += size;
                        } else {
                            warnings.push_back("Unable to read size of " + entry.path().string() + ": " + size_ec.message());
                        }
                    } else {
                        warnings.push_back("Unable to classify " + entry.path().string() + ": " + status_error.message());
                    }
                } else if (entry.is_directory(status_error)) {
                    if (!status_error) {
                        ++detail.directory_count;
                    }
                }

                it.increment(iterator_error);
                if (iterator_error) {
                    warnings.push_back("Directory traversal warning: " + iterator_error.message());
                    iterator_error.clear();
                }
            }

            detail.total_size_human = format_size(detail.total_size_bytes);
            return detail;
        }
    }

    FileReport collect_file_report(const Path& path) {
        FileReport report;
        report.input_path = path;

        std::error_code absolute_error;
        report.absolute_path = std::filesystem::absolute(path, absolute_error);
        if (absolute_error) {
            report.absolute_path = path;
        }

        std::error_code link_status_error;
        const auto link_status = std::filesystem::symlink_status(path, link_status_error);
        if (!link_status_error) {
            report.symlink.is_symlink = std::filesystem::is_symlink(link_status);
        } else {
            report.warnings.push_back("Unable to determine symlink status: " + link_status_error.message());
        }

        std::error_code exists_error;
        report.target_exists = std::filesystem::exists(path, exists_error);
        if (exists_error) {
            report.warnings.push_back("Unable to confirm path existence: " + exists_error.message());
            report.target_exists = false;
        }

        if (report.symlink.is_symlink) {
            std::error_code target_error;
            Path target = std::filesystem::read_symlink(path, target_error);
            if (!target_error) {
                report.symlink.target = target.string();
            } else {
                report.symlink.error = target_error.message();
            }
        }

        if (!report.target_exists) {
            if (report.symlink.is_symlink) {
                report.type = "Broken Symlink";
            }
            return report;
        }

        std::error_code status_error;
        const auto status = std::filesystem::status(path, status_error);
        if (!status_error) {
            report.permissions = format_permissions(status.permissions());
        } else {
            report.warnings.push_back("Unable to read permissions: " + status_error.message());
        }

        if (auto ownership = read_ownership(path, report.warnings)) {
            report.ownership = ownership;
        }
        if (auto timestamps = read_timestamps(path, report.warnings)) {
            report.timestamps = timestamps;
        }

        bool is_regular_file = false;
        bool is_directory = false;

        if (!status_error) {
            is_regular_file = std::filesystem::is_regular_file(status);
            is_directory = std::filesystem::is_directory(status);
            report.type = classify_type(path, is_directory);
        } else {
            report.warnings.push_back("Unable to determine file type: " + status_error.message());
            if (report.symlink.is_symlink) {
                report.type = "Symlink";
            } else {
                report.type = "Unknown";
            }
        }

        if (is_regular_file) {
            report.file_detail = collect_file_detail(path, report.warnings);
        } else if (is_directory) {
            report.directory_detail = collect_directory_detail(path, report.warnings);
        }

        return report;
    }
}
