#pragma once
#include <string>
#include "file_probe/types.hpp"

namespace file_probe {
    void print_help(const std::string& program_name);
    CliParseResult parse_cli(int argc, char* argv[]);
}
