#include <vector>
#include <sstream>
#include <iostream>
#include <optional>
#include "file_probe/utils.hpp"
#include "file_probe/render.hpp"

namespace file_probe {

    namespace {
        constexpr const char* kColorReset = "\033[0m";
        constexpr const char* kColorKey = "\033[1;34m";
        constexpr const char* kColorValue = "\033[1;32m";
        constexpr const char* kColorError = "\033[1;31m";

        class JsonBuilder {
        public:
            void add_string(const std::string& key, const std::string& value) {
                add_separator();
                stream_ << "\"" << key << "\":\"" << json_escape(value) << "\"";
            }

            void add_number(const std::string& key, uintmax_t value) {
                add_separator();
                stream_ << "\"" << key << "\":" << value;
            }

            void add_bool(const std::string& key, bool value) {
                add_separator();
                stream_ << "\"" << key << "\":" << (value ? "true" : "false");
            }

            void add_optional_string(const std::string& key, const std::optional<std::string>& value) {
                add_separator();
                stream_ << "\"" << key << "\":";
                if (value && !value->empty()) {
                    stream_ << "\"" << json_escape(*value) << "\"";
                } else {
                    stream_ << "null";
                }
            }

            void add_array(const std::string& key, const std::vector<std::string>& values) {
                if (values.empty()) {
                    return;
                }
                add_separator();
                stream_ << "\"" << key << "\":[";
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (i > 0) {
                        stream_ << ",";
                    }
                    stream_ << "\"" << json_escape(values[i]) << "\"";
                }
                stream_ << "]";
            }

            std::string str() const {
                return stream_.str();
            }

        private:
            void add_separator() {
                if (first_) {
                    first_ = false;
                } else {
                    stream_ << ",";
                }
            }

            bool first_ = true;
            std::ostringstream stream_;
        };

        void render_symlink_text(const FileReport& report) {
            std::cout << kColorKey << "Symlink: " << kColorValue
                    << (report.symlink.is_symlink ? "Yes" : "No") << kColorReset << "\n";
            if (report.symlink.is_symlink) {
                std::cout << kColorKey << "Symlink Target: " << kColorValue;
                if (report.symlink.target) {
                    std::cout << *report.symlink.target;
                } else if (report.symlink.error) {
                    std::cout << *report.symlink.error;
                } else {
                    std::cout << "Unavailable";
                }
                std::cout << kColorReset << "\n";
            }
        }

        void render_file_detail_text(const FileDetail& detail) {
            std::cout << kColorKey << "Size: " << kColorValue << detail.size_human << kColorReset << "\n";
            std::cout << kColorKey << "Checksum (SHA-256): " << kColorValue << detail.checksum << kColorReset << "\n";
            if (detail.resolution) {
                std::cout << kColorKey << "Resolution: " << kColorValue << *detail.resolution << kColorReset << "\n";
            }
            if (detail.metadata) {
                std::cout << kColorKey << "Metadata: " << kColorValue << *detail.metadata << kColorReset << "\n";
            }
            if (detail.duration) {
                std::cout << kColorKey << "Duration: " << kColorValue << *detail.duration << kColorReset << "\n";
            }
        }

        void render_directory_detail_text(const DirectoryDetail& detail) {
            std::cout << kColorKey << "Total Size: " << kColorValue << detail.total_size_human << kColorReset << "\n";
            std::cout << kColorKey << "File Count: " << kColorValue << detail.file_count << kColorReset << "\n";
            std::cout << kColorKey << "Directory Count: " << kColorValue << detail.directory_count << kColorReset << "\n";
        }
    }

    void render_text(const FileReport& report) {
        if (!report.target_exists && !report.symlink.is_symlink) {
            std::cerr << kColorError << "Error: File does not exist!" << kColorReset << "\n";
            return;
        }

        std::cout << kColorKey << "Path: " << kColorValue << report.absolute_path << kColorReset << "\n";
        std::cout << kColorKey << "Type: " << kColorValue << report.type << kColorReset << "\n";
        render_symlink_text(report);

        if (report.permissions) {
            std::cout << kColorKey << "Permissions: " << kColorValue << *report.permissions << kColorReset << "\n";
        }

        if (report.ownership) {
            std::cout << kColorKey << "Owner: " << kColorValue << report.ownership->owner << kColorReset << "\n";
            std::cout << kColorKey << "Group: " << kColorValue << report.ownership->group << kColorReset << "\n";
        }

        if (report.timestamps) {
            std::cout << kColorKey << "Last Access Time: " << kColorValue << report.timestamps->last_access << kColorReset << "\n";
            std::cout << kColorKey << "Last Modify Time: " << kColorValue << report.timestamps->last_modify << kColorReset << "\n";
            std::cout << kColorKey << "Last Change Time: " << kColorValue << report.timestamps->last_change << kColorReset << "\n";
        }

        if (report.file_detail) {
            render_file_detail_text(*report.file_detail);
        } else if (report.directory_detail) {
            render_directory_detail_text(*report.directory_detail);
        }

        for (const auto& warning : report.warnings) {
            std::cerr << kColorError << "Warning: " << warning << kColorReset << "\n";
        }
    }

    void render_json(const FileReport& report) {
        if (!report.target_exists && !report.symlink.is_symlink) {
            std::ostringstream err;
            err << "{"
                << "\"path\":\"" << json_escape(report.absolute_path.string()) << "\","
                << "\"error\":\"File does not exist\""
                << "}";
            std::cout << err.str() << "\n";
            return;
        }

        JsonBuilder json;
        json.add_string("path", report.absolute_path.string());
        json.add_string("type", report.type);
        json.add_bool("isSymlink", report.symlink.is_symlink);
        json.add_bool("targetExists", report.target_exists);

        if (report.symlink.is_symlink) {
            if (report.symlink.target) {
                json.add_string("symlinkTarget", *report.symlink.target);
            } else if (report.symlink.error) {
                json.add_string("symlinkError", *report.symlink.error);
            } else {
                json.add_optional_string("symlinkTarget", std::nullopt);
            }
        }

        if (report.permissions) {
            json.add_string("permissions", *report.permissions);
        }

        if (report.ownership) {
            json.add_string("owner", report.ownership->owner);
            json.add_string("group", report.ownership->group);
        }

        if (report.timestamps) {
            json.add_string("lastAccess", report.timestamps->last_access);
            json.add_string("lastModify", report.timestamps->last_modify);
            json.add_string("lastChange", report.timestamps->last_change);
        }

        if (report.file_detail) {
            json.add_number("sizeBytes", report.file_detail->size_bytes);
            json.add_string("size", report.file_detail->size_human);
            json.add_string("checksumSha256", report.file_detail->checksum);
            json.add_optional_string("resolution", report.file_detail->resolution);
            json.add_optional_string("metadata", report.file_detail->metadata);
            json.add_optional_string("duration", report.file_detail->duration);
        }

        if (report.directory_detail) {
            json.add_number("totalSizeBytes", report.directory_detail->total_size_bytes);
            json.add_string("totalSize", report.directory_detail->total_size_human);
            json.add_number("fileCount", report.directory_detail->file_count);
            json.add_number("directoryCount", report.directory_detail->directory_count);
        }

        json.add_array("warnings", report.warnings);

        std::cout << '{' << json.str() << "}\n";
    }
}
