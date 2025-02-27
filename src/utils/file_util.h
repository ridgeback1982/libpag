#pragma once
#include <iostream>
#include "pag/defines.h"

#include <random>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

namespace pag {

std::string getPlatformTemporaryDirectory();

std::string findEnglishFontName(const std::string& chineseFontName);

void PAG_API writeStringToFile(const std::string& str, const fs::path& filePath);

fs::path PAG_API create_temp_directory(const std::string& prefix = "tmp_");

}  // namespace pag
