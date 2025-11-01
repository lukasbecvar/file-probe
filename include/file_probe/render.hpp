#pragma once
#include "file_probe/types.hpp"

namespace file_probe {
    void render_text(const FileReport& report);
    void render_json(const FileReport& report);
}
