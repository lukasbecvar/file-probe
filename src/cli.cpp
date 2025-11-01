#include <vector>
#include <iostream>
#include "file_probe/cli.hpp"

namespace file_probe {

    namespace {
        void append_usage(std::ostream& out, const std::string& program_name) {
            out << "Usage: " << program_name << " [options] <path>\n"
                << "\n"
                << "Inspect a file or directory and display detailed information:\n"
                << "  - Type detection for regular files and directories\n"
                << "  - Unix-style permissions, ownership, and timestamps\n"
                << "  - Human-readable size for files and recursive totals for directories\n"
                << "  - SHA-256 checksum for regular files\n"
                << "  - Media insights (resolution, duration, codec, bitrate) via FFmpeg\n"
                << "  - Image metadata (channel count) via stb_image\n"
                << "\n"
                << "Options:\n"
                << "  -h, -help, --help    Show this help message and exit\n"
                << "  --json               Emit machine-readable JSON instead of colored text\n";
        }
    }

    void print_help(const std::string& program_name) {
        append_usage(std::cout, program_name);
    }

    CliParseResult parse_cli(int argc, char* argv[]) {
        CliParseResult result;
        bool literal_mode = false;
        std::vector<std::string> positional;

        for (int index = 1; index < argc; ++index) {
            std::string argument = argv[index];
            if (!literal_mode) {
                if (argument == "--") {
                    literal_mode = true;
                    continue;
                }
                if (argument == "-h" || argument == "--help" || argument == "-help") {
                    result.show_help = true;
                    continue;
                }
                if (argument == "--json") {
                    result.json_output = true;
                    continue;
                }
                if (!argument.empty() && argument.front() == '-') {
                    result.valid = false;
                    result.error_message = "Unknown option: " + argument;
                    return result;
                }
            }
            positional.push_back(argument);
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
}
