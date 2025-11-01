/**
 * This application is a file and directory information utility
 *
 * Features:
 * - Displays the absolute path, file type (e.g., Directory, Image, Video, Audio, 
 *   Text, Document, Archive, or Binary), and Unix-like file permissions.
 * - For regular files:
 *   - Shows file size in a human-readable format (B, KB, MB, etc.).
 *   - For media files:
 *     - Images: Displays resolution (using stb_image).
 *     - Videos: Displays resolution (using FFmpeg).
 *     - Audio files: Displays duration (using ffprobe).
 * - For directories:
 *   - Recursively calculates the total size of all contained files.
 *   - Counts the number of regular files and subdirectories.
 * - Uses ANSI escape codes to provide styled (colored) terminal output.
 *
 * Usage:
 *      ./file-probe <path> where <path> is the file or directory to inspect.
 *
 * Example:
 *      ./file-probe /home/user/documents
 *
 * Note:
 * - This program uses ANSI escape sequences for colored output. It is best viewed in a terminal that supports color.
 */
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <optional>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <arpa/inet.h>
#include <cstring>
#include <pwd.h>
#include <grp.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavutil/opt.h>
}

namespace fs = std::filesystem;

#define COLOR_RESET "\033[0m"
#define COLOR_KEY "\033[1;34m"
#define COLOR_VALUE "\033[1;32m"
#define COLOR_ERROR "\033[1;31m"

struct CliResult {
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
    fs::path input_path;
    fs::path absolute_path;
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

std::string format_size(uintmax_t size) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    double size_in_units = static_cast<double>(size);
    while (size_in_units >= 1024 && i < 4) {
        size_in_units /= 1024;
        i++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(i == 0 ? 0 : 2);
    oss << size_in_units << " " << units[i];
    return oss.str();
}

void print_help(const std::string& program_name) {
    std::cout
        << "Usage: " << program_name << " [options] <path>\n"
        << "\n"
        << "Inspect a file or directory and display detailed information:\n"
        << "  - Type detection for regular files and directories\n"
        << "  - Unix-style permissions, ownership, and timestamps\n"
        << "  - Human-readable size for files and recursive totals for directories\n"
        << "  - SHA-256 checksum for regular files\n"
        << "  - Media insights (resolution, duration, codec, bitrate) via ffprobe\n"
        << "  - Image metadata (channel count) via stb_image\n"
        << "\n"
        << "Options:\n"
        << "  -h, -help, --help    Show this help message and exit\n"
        << "  --json               Emit machine-readable JSON instead of colored text\n";
}

std::string shell_escape(const std::string& input) {
    std::string escaped = "'";
    for (char c : input) {
        if (c == '\'')
            escaped += "'\\''";
        else
            escaped += c;
    }
    escaped += "'";
    return escaped;
}

std::string json_escape(const std::string& input) {
    std::ostringstream oss;
    for (unsigned char c : input) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20 || c > 0x7E) {
                    oss << "\\u"
                        << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c)
                        << std::dec << std::nouppercase;
                } else {
                    oss << static_cast<char>(c);
                }
        }
    }
    return oss.str();
}

std::string get_username(uid_t uid) {
    if (struct passwd* pwd = getpwuid(uid)) {
        if (pwd->pw_name && std::strlen(pwd->pw_name) > 0)
            return pwd->pw_name;
    }
    return std::to_string(uid);
}

std::string get_groupname(gid_t gid) {
    if (struct group* grp = getgrgid(gid)) {
        if (grp->gr_name && std::strlen(grp->gr_name) > 0)
            return grp->gr_name;
    }
    return std::to_string(gid);
}

std::string compute_sha256(const fs::path& p) {
    if (!fs::exists(p))
        return "File not accessible";

    std::string command = "sha256sum " + shell_escape(p.string());
    char buffer[256];
    std::string result;
    FILE* fp = popen(command.c_str(), "r");
    if (!fp)
        return "Unable to compute checksum";

    if (fgets(buffer, sizeof(buffer), fp))
        result = buffer;
    pclose(fp);

    if (result.empty())
        return "Unable to compute checksum";

    std::string checksum = result.substr(0, result.find_first_of(" \t"));
    checksum.erase(std::remove_if(checksum.begin(), checksum.end(), ::isspace), checksum.end());
    return checksum;
}

std::string get_time(time_t t) {
    struct tm *tm_info = localtime(&t);
    char buffer[26];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buffer);
}

std::string get_permissions(fs::perms p) {
    std::string permissions = "---------";
    if ((p & fs::perms::owner_read) != fs::perms::none) permissions[0] = 'r';
    if ((p & fs::perms::owner_write) != fs::perms::none) permissions[1] = 'w';
    if ((p & fs::perms::owner_exec) != fs::perms::none) permissions[2] = 'x';
    if ((p & fs::perms::group_read) != fs::perms::none) permissions[3] = 'r';
    if ((p & fs::perms::group_write) != fs::perms::none) permissions[4] = 'w';
    if ((p & fs::perms::group_exec) != fs::perms::none) permissions[5] = 'x';
    if ((p & fs::perms::others_read) != fs::perms::none) permissions[6] = 'r';
    if ((p & fs::perms::others_write) != fs::perms::none) permissions[7] = 'w';
    if ((p & fs::perms::others_exec) != fs::perms::none) permissions[8] = 'x';
    return permissions;
}

bool is_text_file(const fs::path &p) {
    std::ifstream file(p, std::ios::binary);
    if (!file)
        return false;

    char c;
    int count = 0, non_text = 0;
    while (file.get(c) && count < 512) {
        if (!std::isprint(static_cast<unsigned char>(c)) && !std::isspace(static_cast<unsigned char>(c)))
            non_text++;
        count++;
    }
    if (count == 0)
        return true;
    return (static_cast<double>(non_text) / count) < 0.3;
}

std::string get_file_type(const fs::path &p) {
    if (fs::is_directory(p))
        return "Directory";

    std::string file_extension = p.extension().string();
    std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);

    if (file_extension == ".jpg" || file_extension == ".jpeg" ||
        file_extension == ".png" || file_extension == ".gif" ||
        file_extension == ".bmp" || file_extension == ".tiff") {
        return "Image";
    } else if (file_extension == ".mp4" || file_extension == ".avi" ||
               file_extension == ".mkv" || file_extension == ".mov" ||
               file_extension == ".flv") {
        return "Video";
    } else if (file_extension == ".mp3" || file_extension == ".wav" ||
               file_extension == ".flac" || file_extension == ".aac" ||
               file_extension == ".ogg") {
        return "Audio";
    } else if (file_extension == ".txt" || file_extension == ".csv" ||
               file_extension == ".log" || file_extension == ".json" ||
               file_extension == ".xml" || file_extension == ".html" ||
               file_extension == ".css" || file_extension == ".js") {
        return "Text";
    } else if (file_extension == ".pdf" || file_extension == ".doc" ||
               file_extension == ".docx" || file_extension == ".odt") {
        return "Document";
    } else if (file_extension == ".zip" || file_extension == ".rar" ||
               file_extension == ".7z" || file_extension == ".tar" ||
               file_extension == ".gz") {
        return "Archive";
    } else {
        return is_text_file(p) ? "Text" : "Binary";
    }
}

std::string get_image_resolution(const fs::path& p) {
    int width, height, channels;
    unsigned char* image = stbi_load(p.c_str(), &width, &height, &channels, 0);
    if (image) {
        stbi_image_free(image);
        return std::to_string(width) + "x" + std::to_string(height);
    }
    return "Failed to load image";
}

std::string get_video_resolution(const fs::path& p) {
    AVFormatContext* formatContext = nullptr;
    if (avformat_open_input(&formatContext, p.c_str(), nullptr, nullptr) != 0)
        return "Failed to open video file";

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        avformat_close_input(&formatContext);
        return "Failed to find stream information";
    }

    int video_stream_index = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        avformat_close_input(&formatContext);
        return "No video stream found";
    }

    AVStream* video_stream = formatContext->streams[video_stream_index];
    int width = video_stream->codecpar->width;
    int height = video_stream->codecpar->height;
    avformat_close_input(&formatContext);
    return std::to_string(width) + "x" + std::to_string(height);
}

std::string get_media_duration(const fs::path& p) {
    std::string command = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 " + shell_escape(p.string());
    char buffer[128];
    std::string result;
    FILE* fp = popen(command.c_str(), "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp))
            result += buffer;
        pclose(fp);
    }
    if (result.empty())
        return "Unable to get duration";

    float duration = std::stof(result);
    int minutes = static_cast<int>(duration) / 60;
    int seconds = static_cast<int>(duration) % 60;
    int hours = minutes / 60;
    minutes %= 60;

    std::ostringstream formatted_duration;
    if (hours > 0)
        formatted_duration << hours << " hours ";
    if (minutes > 0)
        formatted_duration << minutes << " minutes ";
    if (seconds > 0)
        formatted_duration << seconds << " seconds";

    return formatted_duration.str();
}

std::string format_bit_rate(long long bits_per_sec) {
    if (bits_per_sec <= 0)
        return "Unknown";

    const char* units[] = { "b/s", "kb/s", "Mb/s", "Gb/s" };
    double rate = static_cast<double>(bits_per_sec);
    int unit_index = 0;
    while (rate >= 1000.0 && unit_index < 3) {
        rate /= 1000.0;
        unit_index++;
    }

    std::ostringstream oss;
    int precision = (rate < 10.0) ? 2 : (rate < 100.0 ? 1 : 0);
    oss << std::fixed << std::setprecision(precision) << rate << " " << units[unit_index];
    return oss.str();
}

std::string get_media_resolution(const fs::path& p) {
    std::string file_extension = p.extension().string();
    std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);
    
    if (file_extension == ".jpg" || file_extension == ".jpeg" || file_extension == ".png") {
        return get_image_resolution(p);
    } else if (file_extension == ".mp4" || file_extension == ".avi" ||
               file_extension == ".mkv" || file_extension == ".mov" ||
               file_extension == ".flv") {
        return get_video_resolution(p);
    }
    return "Unsupported file type";
}

std::string get_media_metadata(const fs::path& p) {
    std::string command = "ffprobe -v error -show_entries format=format_name,bit_rate -show_entries stream=codec_name "
                          "-of default=noprint_wrappers=1 " + shell_escape(p.string());
    char buffer[256];
    std::string result;
    FILE* fp = popen(command.c_str(), "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp))
            result += buffer;
        pclose(fp);
    }
    if (result.empty())
        return "No metadata available";

    std::istringstream iss(result);
    std::string line;
    std::string format_name;
    std::string bitrate_value;
    std::vector<std::string> codecs;

    while (std::getline(iss, line)) {
        if (line.rfind("format_name=", 0) == 0) {
            format_name = line.substr(std::string("format_name=").size());
        } else if (line.rfind("bit_rate=", 0) == 0) {
            bitrate_value = line.substr(std::string("bit_rate=").size());
        } else if (line.rfind("codec_name=", 0) == 0) {
            codecs.push_back(line.substr(std::string("codec_name=").size()));
        }
    }

    std::ostringstream metadata;
    bool has_value = false;
    if (!format_name.empty()) {
        metadata << "Format: " << format_name;
        has_value = true;
    }
    if (!codecs.empty()) {
        if (has_value)
            metadata << " | ";
        metadata << "Codec: ";
        for (size_t i = 0; i < codecs.size(); ++i) {
            if (i > 0)
                metadata << ", ";
            metadata << codecs[i];
        }
        has_value = true;
    }

    if (!bitrate_value.empty()) {
        long long bitrate_number = 0;
        try {
            bitrate_number = std::stoll(bitrate_value);
        } catch (...) {
            bitrate_number = 0;
        }
        if (has_value)
            metadata << " | ";
        metadata << "Bitrate: " << format_bit_rate(bitrate_number);
        has_value = true;
    }

    if (!has_value)
        return "No metadata available";

    return metadata.str();
}

std::string get_image_metadata(const fs::path& p) {
    int width = 0, height = 0, channels = 0;
    if (stbi_info(p.c_str(), &width, &height, &channels)) {
        std::ostringstream oss;
        oss << "Channels: " << channels;
        return oss.str();
    }
    return "Unable to read image metadata";
}

FileDetail collect_file_detail(const fs::path& path) {
    FileDetail detail;
    std::error_code ec;
    detail.size_bytes = fs::file_size(path, ec);
    if (ec)
        detail.size_bytes = 0;
    detail.size_human = format_size(detail.size_bytes);
    detail.checksum = compute_sha256(path);

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    bool is_image = (extension == ".jpg" || extension == ".jpeg" ||
                     extension == ".png" || extension == ".gif" ||
                     extension == ".bmp" || extension == ".tiff");
    bool is_video = (extension == ".mp4" || extension == ".avi" ||
                     extension == ".mkv" || extension == ".mov" ||
                     extension == ".flv");
    bool is_audio = (extension == ".mp3" || extension == ".wav" ||
                     extension == ".flac" || extension == ".aac" ||
                     extension == ".ogg");

    if (is_image || is_video) {
        std::string res = get_media_resolution(path);
        if (!res.empty())
            detail.resolution = res;
    }

    if (is_image) {
        std::string meta = get_image_metadata(path);
        if (!meta.empty())
            detail.metadata = meta;
    } else if (is_audio || is_video) {
        std::string meta = get_media_metadata(path);
        if (!meta.empty())
            detail.metadata = meta;
    }

    if (is_audio || is_video) {
        std::string dur = get_media_duration(path);
        if (!dur.empty())
            detail.duration = dur;
    }

    return detail;
}

DirectoryDetail collect_directory_detail(const fs::path& path, std::vector<std::string>& warnings) {
    DirectoryDetail detail;
    std::error_code ec;
    fs::directory_options opts = fs::directory_options::skip_permission_denied;
    fs::recursive_directory_iterator it(path, opts, ec);
    fs::recursive_directory_iterator end;

    if (ec) {
        warnings.push_back("Unable to traverse directory: " + ec.message());
        return detail;
    }

    while (it != end) {
        const fs::directory_entry& entry = *it;
        std::error_code entry_ec;
        if (entry.is_regular_file(entry_ec)) {
            detail.file_count++;
            if (!entry_ec) {
                std::error_code size_ec;
                uintmax_t size = entry.file_size(size_ec);
                if (!size_ec) {
                    detail.total_size_bytes += size;
                } else {
                    warnings.push_back("Unable to read size of " + entry.path().string() + ": " + size_ec.message());
                }
            } else {
                warnings.push_back("Unable to classify " + entry.path().string() + ": " + entry_ec.message());
            }
        } else if (entry.is_directory(entry_ec)) {
            if (!entry_ec)
                detail.directory_count++;
        }

        it.increment(ec);
        if (ec) {
            warnings.push_back("Directory traversal warning: " + ec.message());
            ec.clear();
        }
    }

    detail.total_size_human = format_size(detail.total_size_bytes);
    return detail;
}

FileReport collect_file_report(const fs::path& path) {
    FileReport report;
    report.input_path = path;

    std::error_code ec;
    report.absolute_path = fs::absolute(path, ec);
    if (ec)
        report.absolute_path = path;

    fs::file_status link_status = fs::symlink_status(path, ec);
    if (!ec) {
        report.symlink.is_symlink = fs::is_symlink(link_status);
    } else {
        report.warnings.push_back("Unable to determine symlink status: " + ec.message());
    }

    std::error_code exists_ec;
    report.target_exists = fs::exists(path, exists_ec);
    if (exists_ec) {
        report.warnings.push_back("Unable to confirm path existence: " + exists_ec.message());
        report.target_exists = false;
    }

    if (report.symlink.is_symlink) {
        std::error_code target_ec;
        fs::path target = fs::read_symlink(path, target_ec);
        if (!target_ec) {
            report.symlink.target = target.string();
        } else {
            report.symlink.error = target_ec.message();
        }
    }

    if (!report.target_exists) {
        if (report.symlink.is_symlink) {
            report.type = "Broken Symlink";
        }
        return report;
    }

    report.type = get_file_type(path);

    fs::file_status status = fs::status(path, ec);
    if (!ec) {
        report.permissions = get_permissions(status.permissions());
    } else {
        report.warnings.push_back("Unable to read permissions: " + ec.message());
    }

    struct stat fileStat {};
    if (stat(path.c_str(), &fileStat) == 0) {
        OwnershipInfo ownership {
            .owner = get_username(fileStat.st_uid),
            .group = get_groupname(fileStat.st_gid)
        };
        report.ownership = ownership;

        TimeInfo times {
            .last_access = get_time(fileStat.st_atime),
            .last_modify = get_time(fileStat.st_mtime),
            .last_change = get_time(fileStat.st_ctime)
        };
        report.timestamps = times;
    } else {
        int stat_error = errno;
        report.warnings.push_back("Unable to read ownership metadata: " + std::string(std::strerror(stat_error)));
    }

    std::error_code type_ec;
    bool is_regular = fs::is_regular_file(path, type_ec);
    if (type_ec) {
        report.warnings.push_back("Unable to determine if regular file: " + type_ec.message());
        is_regular = false;
    }
    type_ec.clear();
    bool is_directory = fs::is_directory(path, type_ec);
    if (type_ec) {
        report.warnings.push_back("Unable to determine if directory: " + type_ec.message());
        is_directory = false;
    }

    if (is_regular) {
        report.file_detail = collect_file_detail(path);
    } else if (is_directory) {
        report.directory_detail = collect_directory_detail(path, report.warnings);
    }

    return report;
}

void render_text(const FileReport& report) {
    if (!report.target_exists && !report.symlink.is_symlink) {
        std::cerr << COLOR_ERROR << "Error: File does not exist!" << COLOR_RESET << "\n";
        return;
    }

    std::cout << COLOR_KEY << "Path: " << COLOR_VALUE << report.absolute_path << COLOR_RESET << "\n";
    std::cout << COLOR_KEY << "Type: " << COLOR_VALUE << report.type << COLOR_RESET << "\n";
    std::cout << COLOR_KEY << "Symlink: " << COLOR_VALUE << (report.symlink.is_symlink ? "Yes" : "No") << COLOR_RESET << "\n";

    if (report.symlink.is_symlink) {
        std::cout << COLOR_KEY << "Symlink Target: " << COLOR_VALUE;
        if (report.symlink.target) {
            std::cout << *report.symlink.target;
        } else if (report.symlink.error) {
            std::cout << *report.symlink.error;
        } else {
            std::cout << "Unavailable";
        }
        std::cout << COLOR_RESET << "\n";
    }

    if (report.permissions) {
        std::cout << COLOR_KEY << "Permissions: " << COLOR_VALUE << *report.permissions << COLOR_RESET << "\n";
    }

    if (report.ownership) {
        std::cout << COLOR_KEY << "Owner: " << COLOR_VALUE << report.ownership->owner << COLOR_RESET << "\n";
        std::cout << COLOR_KEY << "Group: " << COLOR_VALUE << report.ownership->group << COLOR_RESET << "\n";
    }

    if (report.timestamps) {
        std::cout << COLOR_KEY << "Last Access Time: " << COLOR_VALUE << report.timestamps->last_access << COLOR_RESET << "\n";
        std::cout << COLOR_KEY << "Last Modify Time: " << COLOR_VALUE << report.timestamps->last_modify << COLOR_RESET << "\n";
        std::cout << COLOR_KEY << "Last Change Time: " << COLOR_VALUE << report.timestamps->last_change << COLOR_RESET << "\n";
    }

    if (report.file_detail) {
        const FileDetail& detail = *report.file_detail;
        std::cout << COLOR_KEY << "Size: " << COLOR_VALUE << detail.size_human << COLOR_RESET << "\n";
        std::cout << COLOR_KEY << "Checksum (SHA-256): " << COLOR_VALUE << detail.checksum << COLOR_RESET << "\n";
        if (detail.resolution) {
            std::cout << COLOR_KEY << "Resolution: " << COLOR_VALUE << *detail.resolution << COLOR_RESET << "\n";
        }
        if (detail.metadata) {
            std::cout << COLOR_KEY << "Metadata: " << COLOR_VALUE << *detail.metadata << COLOR_RESET << "\n";
        }
        if (detail.duration) {
            std::cout << COLOR_KEY << "Duration: " << COLOR_VALUE << *detail.duration << COLOR_RESET << "\n";
        }
    } else if (report.directory_detail) {
        const DirectoryDetail& detail = *report.directory_detail;
        std::cout << COLOR_KEY << "Total Size: " << COLOR_VALUE << detail.total_size_human << COLOR_RESET << "\n";
        std::cout << COLOR_KEY << "File Count: " << COLOR_VALUE << detail.file_count << COLOR_RESET << "\n";
        std::cout << COLOR_KEY << "Directory Count: " << COLOR_VALUE << detail.directory_count << COLOR_RESET << "\n";
    }

    for (const auto& warning : report.warnings) {
        std::cerr << COLOR_ERROR << "Warning: " << warning << COLOR_RESET << "\n";
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

    std::ostringstream json;
    json << "{";
    bool first = true;
    auto add_comma = [&]() {
        if (first)
            first = false;
        else
            json << ",";
    };
    auto add_string = [&](const std::string& key, const std::string& value) {
        add_comma();
        json << "\"" << key << "\":\"" << json_escape(value) << "\"";
    };
    auto add_bool = [&](const std::string& key, bool value) {
        add_comma();
        json << "\"" << key << "\":" << (value ? "true" : "false");
    };
    auto add_number = [&](const std::string& key, uintmax_t value) {
        add_comma();
        json << "\"" << key << "\":" << value;
    };
    auto add_nullable_string = [&](const std::string& key, const std::optional<std::string>& value) {
        add_comma();
        json << "\"" << key << "\":";
        if (value && !value->empty())
            json << "\"" << json_escape(*value) << "\"";
        else
            json << "null";
    };
    auto add_array = [&](const std::string& key, const std::vector<std::string>& values) {
        if (values.empty())
            return;
        add_comma();
        json << "\"" << key << "\":[";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0)
                json << ",";
            json << "\"" << json_escape(values[i]) << "\"";
        }
        json << "]";
    };

    add_string("path", report.absolute_path.string());
    add_string("type", report.type);
    add_bool("isSymlink", report.symlink.is_symlink);
    add_bool("targetExists", report.target_exists);

    if (report.symlink.is_symlink) {
        if (report.symlink.target) {
            add_string("symlinkTarget", *report.symlink.target);
        } else if (report.symlink.error) {
            add_string("symlinkError", *report.symlink.error);
        } else {
            add_nullable_string("symlinkTarget", std::nullopt);
        }
    }

    if (report.permissions) {
        add_string("permissions", *report.permissions);
    }

    if (report.ownership) {
        add_string("owner", report.ownership->owner);
        add_string("group", report.ownership->group);
    }

    if (report.timestamps) {
        add_string("lastAccess", report.timestamps->last_access);
        add_string("lastModify", report.timestamps->last_modify);
        add_string("lastChange", report.timestamps->last_change);
    }

    if (report.file_detail) {
        const FileDetail& detail = *report.file_detail;
        add_number("sizeBytes", detail.size_bytes);
        add_string("size", detail.size_human);
        add_string("checksumSha256", detail.checksum);
        if (detail.resolution)
            add_string("resolution", *detail.resolution);
        if (detail.metadata)
            add_string("metadata", *detail.metadata);
        if (detail.duration)
            add_string("duration", *detail.duration);
    }

    if (report.directory_detail) {
        const DirectoryDetail& detail = *report.directory_detail;
        add_number("totalSizeBytes", detail.total_size_bytes);
        add_string("totalSize", detail.total_size_human);
        add_number("fileCount", detail.file_count);
        add_number("directoryCount", detail.directory_count);
    }

    add_array("warnings", report.warnings);

    json << "}";
    std::cout << json.str() << "\n";
}

CliResult parse_cli(int argc, char* argv[]) {
    CliResult result;
    bool literal_mode = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (!literal_mode) {
            if (arg == "--") {
                literal_mode = true;
                continue;
            }
            if (arg == "-h" || arg == "--help" || arg == "-help") {
                result.show_help = true;
                continue;
            }
            if (arg == "--json") {
                result.json_output = true;
                continue;
            }
            if (!arg.empty() && arg[0] == '-') {
                result.valid = false;
                result.error_message = "Unknown option: " + arg;
                return result;
            }
        }
        positional.push_back(arg);
    }

    if (!result.show_help) {
        if (positional.empty()) {
            result.valid = false;
            result.error_message = "Missing path argument.";
        } else if (positional.size() > 1) {
            result.valid = false;
            result.error_message = "Unexpected extra argument: " + positional[1];
        } else {
            result.path = positional.front();
        }
    } else if (!positional.empty()) {
        result.path = positional.front();
    }

    return result;
}

int main(int argc, char* argv[]) {
    CliResult options = parse_cli(argc, argv);

    if (!options.valid) {
        if (options.json_output) {
            std::cout << "{\"error\":\"" << json_escape(options.error_message) << "\"}\n";
        } else {
            std::cerr << COLOR_ERROR << options.error_message << COLOR_RESET << "\n";
            std::cerr << COLOR_ERROR << "Usage: " << argv[0] << " [options] <path>" << COLOR_RESET << "\n";
        }
        return 1;
    }

    if (options.show_help || !options.path) {
        print_help(argv[0]);
        return 0;
    }

    fs::path target_path = *options.path;
    FileReport report = collect_file_report(target_path);

    if (!report.target_exists && !report.symlink.is_symlink) {
        if (options.json_output) {
            std::ostringstream err;
            err << "{"
                << "\"path\":\"" << json_escape(report.absolute_path.string()) << "\","
                << "\"error\":\"File does not exist\""
                << "}";
            std::cout << err.str() << "\n";
        } else {
            std::cerr << COLOR_ERROR << "Error: File does not exist!" << COLOR_RESET << "\n";
        }
        return 1;
    }

    if (options.json_output) {
        render_json(report);
    } else {
        render_text(report);
    }

    return 0;
}
