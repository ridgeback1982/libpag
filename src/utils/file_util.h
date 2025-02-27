#pragma once
#include <iostream>
#include "pag/defines.h"

#include <random>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

#define AliyunOssUrlPrefix ".aliyuncs.com"
#define InternalUrlPrefix "-internal.aliyuncs.com"

namespace pag {

std::string getPlatformTemporaryDirectory();

std::string findEnglishFontName(const std::string& chineseFontName);

void PAG_API writeStringToFile(const std::string& str, const fs::path& filePath);

fs::path PAG_API create_temp_directory(const std::string& prefix = "tmp_");

void PAG_API stringReplace(std::string& str, const std::string& old_value, const std::string& new_value);

bool PAG_API runOnServer();

}  // namespace pag
