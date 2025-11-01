#include <array>
#include <cctype>
#include <memory>
#include <vector>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <string_view>
#include "file_probe/media.hpp"

extern "C" {
    #include <libavutil/avutil.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
}

#define STB_IMAGE_IMPLEMENTATION
#include "include/others/stb_image.h"

namespace file_probe {

    namespace {
        using Path = std::filesystem::path;

        constexpr std::array<std::string_view, 6> kImageExtensions = {
            ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff"};
        constexpr std::array<std::string_view, 5> kVideoExtensions = {
            ".mp4", ".avi", ".mkv", ".mov", ".flv"};
        constexpr std::array<std::string_view, 5> kAudioExtensions = {
            ".mp3", ".wav", ".flac", ".aac", ".ogg"};

        std::string to_lowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        template <std::size_t N>
        bool matches_extension(const std::string& extension, const std::array<std::string_view, N>& allowed) {
            std::string lowered = to_lowercase(extension);
            return std::any_of(allowed.begin(), allowed.end(), [&](std::string_view item) {
                return lowered == item;
            });
        }

        struct FormatContextDeleter {
            void operator()(AVFormatContext* ctx) const noexcept {
                if (!ctx) {
                    return;
                }
                AVFormatContext* to_close = ctx;
                avformat_close_input(&to_close);
            }
        };

        using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

        std::string to_utf8_path(const Path& path) {
#if defined(_WIN32)
            return path.u8string();
#else
            return path.string();
#endif
        }

        FormatContextPtr open_media_file(const Path& path) {
            AVFormatContext* raw = nullptr;
            const std::string native_path = to_utf8_path(path);
            if (avformat_open_input(&raw, native_path.c_str(), nullptr, nullptr) != 0) {
                return nullptr;
            }
            FormatContextPtr context(raw);
            if (avformat_find_stream_info(context.get(), nullptr) < 0) {
                return nullptr;
            }
            return context;
        }
    }

    bool is_image_extension(const Path& path) {
        return matches_extension(path.extension().string(), kImageExtensions);
    }

    bool is_video_extension(const Path& path) {
        return matches_extension(path.extension().string(), kVideoExtensions);
    }

    bool is_audio_extension(const Path& path) {
        return matches_extension(path.extension().string(), kAudioExtensions);
    }

    std::optional<std::string> image_resolution(const Path& path) {
        int width = 0;
        int height = 0;
        int channels = 0;
        const std::string native_path = to_utf8_path(path);
        if (stbi_info(native_path.c_str(), &width, &height, &channels) == 0) {
            return std::nullopt;
        }
        return std::to_string(width) + "x" + std::to_string(height);
    }

    std::optional<std::string> image_metadata(const Path& path) {
        int width = 0;
        int height = 0;
        int channels = 0;
        const std::string native_path = to_utf8_path(path);
        if (stbi_info(native_path.c_str(), &width, &height, &channels) == 0) {
            return std::nullopt;
        }
        std::ostringstream oss;
        oss << "Channels: " << channels;
        return oss.str();
    }

    std::optional<std::string> media_resolution(const Path& path) {
        auto context = open_media_file(path);
        if (!context) {
            return std::nullopt;
        }

        const AVStream* stream = nullptr;
        for (unsigned int idx = 0; idx < context->nb_streams; ++idx) {
            const AVStream* candidate = context->streams[idx];
            if (candidate->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                stream = candidate;
                break;
            }
        }

        if (!stream) {
            return std::nullopt;
        }

        int width = stream->codecpar->width;
        int height = stream->codecpar->height;
        if (width <= 0 || height <= 0) {
            return std::nullopt;
        }

        return std::to_string(width) + "x" + std::to_string(height);
    }

    std::optional<std::string> media_metadata(const Path& path) {
        auto context = open_media_file(path);
        if (!context) {
            return std::nullopt;
        }

        std::ostringstream oss;
        bool has_value = false;

        if (context->iformat && context->iformat->name) {
            oss << "Format: " << context->iformat->name;
            has_value = true;
        }

        if (context->bit_rate > 0) {
            if (has_value) {
                oss << " | ";
            }
            double rate = static_cast<double>(context->bit_rate);
            const char* units[] = {"b/s", "kb/s", "Mb/s", "Gb/s"};
            int unit_index = 0;
            while (rate >= 1000.0 && unit_index < 3) {
                rate /= 1000.0;
                ++unit_index;
            }
            oss << "Bitrate: " << std::fixed << std::setprecision(rate < 10.0 ? 2 : (rate < 100.0 ? 1 : 0))
                << rate << ' ' << units[unit_index];
            has_value = true;
        }

        std::vector<std::string> codecs;
        for (unsigned int idx = 0; idx < context->nb_streams; ++idx) {
            const AVStream* stream = context->streams[idx];
            if (stream->codecpar) {
                const char* codec_name = avcodec_get_name(stream->codecpar->codec_id);
                if (codec_name && std::strlen(codec_name) > 0) {
                    codecs.emplace_back(codec_name);
                }
            }
        }

        if (!codecs.empty()) {
            if (has_value) {
                oss << " | ";
            }
            oss << "Codec: ";
            for (std::size_t i = 0; i < codecs.size(); ++i) {
                if (i > 0) {
                    oss << ", ";
                }
                oss << codecs[i];
            }
            has_value = true;
        }

        if (!has_value) {
            return std::nullopt;
        }

        return oss.str();
    }

    std::optional<std::string> media_duration(const Path& path) {
        auto context = open_media_file(path);
        if (!context) {
            return std::nullopt;
        }

        if (context->duration == AV_NOPTS_VALUE || context->duration <= 0) {
            return std::nullopt;
        }

        const double total_seconds = static_cast<double>(context->duration) / AV_TIME_BASE;
        const int hours = static_cast<int>(total_seconds) / 3600;
        const int minutes = (static_cast<int>(total_seconds) % 3600) / 60;
        const int seconds = static_cast<int>(total_seconds) % 60;

        std::ostringstream oss;
        if (hours > 0) {
            oss << hours << " hours ";
        }
        if (minutes > 0) {
            oss << minutes << " minutes ";
        }
        if (seconds > 0 || (hours == 0 && minutes == 0)) {
            oss << seconds << " seconds";
        }

        return oss.str();
    }
}
