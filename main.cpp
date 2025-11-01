/**
 * This program is a file and directory information utility
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
 * Dependencies:
 * - stb_image library (for image resolution; included via the STB_IMAGE_IMPLEMENTATION macro).
 * - FFmpeg libraries: libavformat and libavutil (for video file analysis).
 * - ffprobe (from FFmpeg) must be installed and accessible via the system PATH for media duration.
 *
 * Functions:
 *  - format_size(uintmax_t size):
 *      Converts a byte size to a human-readable string with appropriate units.
 *
 *  - get_time(time_t t):
 *      Formats a time_t value into a string ("YYYY-MM-DD HH:MM:SS").
 *
 *  - get_permissions(fs::perms p):
 *      Returns a Unix-like string representation (e.g., "rwxr-xr-x") of file permissions.
 *
 *  - is_text_file(const fs::path &p):
 *      Reads the first 512 bytes of a file to decide if it is likely text (based on printable
 *      characters) or binary.
 *
 *  - get_file_type(const fs::path &p):
 *      Determines the file type based on its extension or content, returning types such as
 *      "Image", "Video", "Audio", "Text", "Document", "Archive", "Binary", or "Directory".
 *
 *  - get_image_resolution(const fs::path& p):
 *      Uses stb_image to load an image and return its resolution in the format "width x height".
 *
 *  - get_video_resolution(const fs::path& p):
 *      Uses FFmpeg libraries to open a video file and return its resolution ("width x height").
 *
 *  - get_media_duration(const fs::path& p):
 *      Invokes ffprobe to extract and format the duration of a media file.
 *
 *  - get_media_resolution(const fs::path& p):
 *      Determines the media resolution by calling the appropriate function based on file extension.
 *
 *  - print_file_info(const fs::path& p):
 *      Displays detailed information about the specified file or directory.
 *      For files, it prints attributes like size, resolution, and (for audio) duration.
 *      For directories, it recursively computes and prints the total size, file count, and
 *      directory count.
 *
 *  - main(int argc, char* argv[]):
 *      The program's entry point. It expects a single command-line argument which is the path to a file or directory.
 *
 * Usage:
 *      ./fileinfo <path> where <path> is the file or directory to inspect.
 *
 * Example:
 *      ./fileinfo /home/user/documents
 *
 * Note:
 * - This program uses ANSI escape sequences for colored output. It is best viewed in a terminal that supports color.
 *
 * Author: [Your Name]
 * Date: [Date]
 */
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cctype>
#include <arpa/inet.h>
#include <cstring>

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

std::string format_size(uintmax_t size) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    double size_in_units = size;
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
        << "Usage: " << program_name << " <path>\n"
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
        << "  -h, -help, --help    Show this help message and exit\n";
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

void print_file_info(const fs::path& p) {
    try {
        fs::file_status link_status = fs::symlink_status(p);
        bool is_symlink = fs::is_symlink(link_status);
        bool target_exists = fs::exists(p);

        if (!target_exists && !is_symlink) {
            std::cerr << COLOR_ERROR << "Error: File does not exist!" << COLOR_RESET << "\n";
            return;
        }

        fs::path abs_path = fs::absolute(p);
        std::cout << COLOR_KEY << "Path: " << COLOR_VALUE << abs_path << COLOR_RESET << "\n";
        std::string type_info = target_exists ? get_file_type(p) : (is_symlink ? "Broken Symlink" : "Unknown");
        std::cout << COLOR_KEY << "Type: " << COLOR_VALUE << type_info << COLOR_RESET << "\n";
        std::cout << COLOR_KEY << "Symlink: " << COLOR_VALUE << (is_symlink ? "Yes" : "No") << COLOR_RESET << "\n";
        if (is_symlink) {
            std::cout << COLOR_KEY << "Symlink Target: " << COLOR_VALUE;
            try {
                std::cout << fs::read_symlink(p);
            } catch (const fs::filesystem_error&) {
                std::cout << "Unable to resolve target";
            }
            std::cout << COLOR_RESET << "\n";
        }
        if (target_exists) {
            fs::perms perms = fs::status(p).permissions();
            std::cout << COLOR_KEY << "Permissions: " << COLOR_VALUE << get_permissions(perms) << COLOR_RESET << "\n";
        }

        struct stat fileStat;
        if (target_exists && stat(p.c_str(), &fileStat) == 0) {
            std::cout << COLOR_KEY << "Owner UID: " << COLOR_VALUE << fileStat.st_uid << COLOR_RESET << "\n";
            std::cout << COLOR_KEY << "Group GID: " << COLOR_VALUE << fileStat.st_gid << COLOR_RESET << "\n";
            std::cout << COLOR_KEY << "Last Access Time: " << COLOR_VALUE << get_time(fileStat.st_atime) << COLOR_RESET << "\n";
            std::cout << COLOR_KEY << "Last Modify Time: " << COLOR_VALUE << get_time(fileStat.st_mtime) << COLOR_RESET << "\n";
            std::cout << COLOR_KEY << "Last Change Time: " << COLOR_VALUE << get_time(fileStat.st_ctime) << COLOR_RESET << "\n";
        }

        if (target_exists && fs::is_regular_file(p)) {
            uintmax_t file_size = fs::file_size(p);
            std::cout << COLOR_KEY << "Size: " << COLOR_VALUE << format_size(file_size) << COLOR_RESET << "\n";
            std::string checksum = compute_sha256(p);
            std::cout << COLOR_KEY << "Checksum (SHA-256): " << COLOR_VALUE << checksum << COLOR_RESET << "\n";

            std::string file_extension = p.extension().string();
            std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);
            bool is_image = (file_extension == ".jpg" || file_extension == ".jpeg" ||
                             file_extension == ".png" || file_extension == ".gif" ||
                             file_extension == ".bmp" || file_extension == ".tiff");
            bool is_video = (file_extension == ".mp4" || file_extension == ".avi" ||
                             file_extension == ".mkv" || file_extension == ".mov" ||
                             file_extension == ".flv");
            bool is_audio = (file_extension == ".mp3" || file_extension == ".wav" ||
                             file_extension == ".flac" || file_extension == ".aac" ||
                             file_extension == ".ogg");

            if (is_image || is_video) {
                std::cout << COLOR_KEY << "Resolution: " << COLOR_VALUE << get_media_resolution(p) << COLOR_RESET << "\n";
            }

            if (is_image) {
                std::cout << COLOR_KEY << "Metadata: " << COLOR_VALUE << get_image_metadata(p) << COLOR_RESET << "\n";
            } else if (is_audio || is_video) {
                std::cout << COLOR_KEY << "Metadata: " << COLOR_VALUE << get_media_metadata(p) << COLOR_RESET << "\n";
            }

            if (file_extension == ".mp3" || file_extension == ".wav" || file_extension == ".flac" ||
                file_extension == ".aac" || file_extension == ".ogg" ||
                file_extension == ".mp4" || file_extension == ".avi" || file_extension == ".mkv" ||
                file_extension == ".mov" || file_extension == ".flv") {
                std::cout << COLOR_KEY << "Duration: " << COLOR_VALUE << get_media_duration(p) << COLOR_RESET << "\n";
            }
        } else if (target_exists && fs::is_directory(p)) {
            uintmax_t total_size = 0;
            size_t file_count = 0, dir_count = 0;
            for (const auto &entry : fs::recursive_directory_iterator(p)) {
                try {
                    if (fs::is_regular_file(entry.path())) {
                        total_size += fs::file_size(entry.path());
                        file_count++;
                    } else if (fs::is_directory(entry.path())) {
                        dir_count++;
                    }
                } catch (const fs::filesystem_error &e) {
                    // skip files/directories which cannot be accessed
                }
            }
            std::cout << COLOR_KEY << "Total Size: " << COLOR_VALUE << format_size(total_size) << COLOR_RESET << "\n";
            std::cout << COLOR_KEY << "File Count: " << COLOR_VALUE << file_count << COLOR_RESET << "\n";
            std::cout << COLOR_KEY << "Directory Count: " << COLOR_VALUE << dir_count << COLOR_RESET << "\n";
        }
    } catch (const fs::filesystem_error &e) {
        std::cerr << COLOR_ERROR << "Filesystem error: " << e.what() << COLOR_RESET << "\n";
    }
}

int main(int argc, char* argv[]) {
    // show command usage hint
    if (argc != 2) {
        std::cerr << COLOR_ERROR << "Usage: " << argv[0] << " <file_path>" << COLOR_RESET << "\n";
        return 1;
    }
    
    // get file path argument
    std::string file_path = argv[1];
    
    // show help
    if (file_path == "-h" || file_path == "--help" || file_path == "-help") {
        print_help(argv[0]);
        return 0;
    }
    
    // file info init
    print_file_info(file_path);
    return 0;
}
