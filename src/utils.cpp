#include <ctime>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include "file_probe/utils.hpp"

namespace file_probe {

    namespace {
        constexpr std::size_t kTextProbeLength = 512;
        constexpr std::size_t kUnitCount = 5;
    }

    std::string format_size(uintmax_t size) {
        static const char* kUnits[kUnitCount] = {"B", "KB", "MB", "GB", "TB"};
        std::size_t unit_index = 0;
        double value = static_cast<double>(size);
        while (value >= 1024.0 && unit_index < kUnitCount - 1) {
            value /= 1024.0;
            ++unit_index;
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(unit_index == 0 ? 0 : 2) << value << ' ' << kUnits[unit_index];
        return oss.str();
    }

    std::string format_permissions(std::filesystem::perms perms) {
        std::string symbols = "---------";
        if ((perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none) symbols[0] = 'r';
        if ((perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none) symbols[1] = 'w';
        if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) symbols[2] = 'x';
        if ((perms & std::filesystem::perms::group_read) != std::filesystem::perms::none) symbols[3] = 'r';
        if ((perms & std::filesystem::perms::group_write) != std::filesystem::perms::none) symbols[4] = 'w';
        if ((perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none) symbols[5] = 'x';
        if ((perms & std::filesystem::perms::others_read) != std::filesystem::perms::none) symbols[6] = 'r';
        if ((perms & std::filesystem::perms::others_write) != std::filesystem::perms::none) symbols[7] = 'w';
        if ((perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none) symbols[8] = 'x';
        return symbols;
    }

    std::string format_time(std::time_t value) {
        std::tm tm_snapshot {};
        if (std::tm* local = std::localtime(&value)) {
            tm_snapshot = *local;
        }

        std::ostringstream oss;
        oss << std::put_time(&tm_snapshot, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    bool is_text_file(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }

        char ch = 0;
        std::size_t samples = 0;
        std::size_t non_text = 0;
        while (file.get(ch) && samples < kTextProbeLength) {
            unsigned char byte = static_cast<unsigned char>(ch);
            if (!std::isprint(byte) && !std::isspace(byte)) {
                ++non_text;
            }
            ++samples;
        }

        if (samples == 0) {
            return true;
        }

        return static_cast<double>(non_text) / static_cast<double>(samples) < 0.3;
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
}
