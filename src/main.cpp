#include <iostream>
#include <filesystem>
#include "file_probe/cli.hpp"
#include "file_probe/utils.hpp"
#include "file_probe/render.hpp"
#include "file_probe/collector.hpp"

int main(int argc, char* argv[]) {
    auto options = file_probe::parse_cli(argc, argv);

    if (!options.valid) {
        if (options.json_output) {
            std::cout << "{\"error\":\"" << file_probe::json_escape(options.error_message) << "\"}\n";
        } else {
            std::cerr << "\033[1;31m" << options.error_message << "\033[0m\n";
            std::cerr << "\033[1;31mUsage: " << argv[0] << " [options] <path>\033[0m\n";
        }
        return 1;
    }

    if (options.show_help || !options.path) {
        file_probe::print_help(argv[0]);
        return 0;
    }

    const std::filesystem::path target_path = *options.path;
    file_probe::FileReport report = file_probe::collect_file_report(target_path);

    if (!report.target_exists && !report.symlink.is_symlink) {
        if (options.json_output) {
            std::cout << "{"
                      << "\"path\":\"" << file_probe::json_escape(report.absolute_path.string()) << "\","
                      << "\"error\":\"File does not exist\""
                      << "}\n";
        } else {
            std::cerr << "\033[1;31mError: File does not exist!\033[0m\n";
        }
        return 1;
    }

    if (options.json_output) {
        file_probe::render_json(report);
    } else {
        file_probe::render_text(report);
    }

    return 0;
}
