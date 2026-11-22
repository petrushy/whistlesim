// The built-in preset files (.whistle, JSON), embedded at build time by Source/Whistle/CMakeLists.txt.

#pragma once

#include <cstddef>
#include <vector>

namespace whistle
{

struct EmbeddedFile
{
    const char* name;  // File name, without the folder
    const char* data;  // The file's bytes (followed by a 0, not counted in size)
    std::size_t size;
};

// In file-name order.
const std::vector<EmbeddedFile>& presetFiles();

} // namespace whistle
