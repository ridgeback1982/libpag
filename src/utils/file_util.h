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

void PAG_API writeStringToFile(const std::string& str, const fs::path& filePath, bool append = false);

fs::path PAG_API create_temp_directory(const std::string& prefix = "tmp_");

std::string PAG_API getFileNameWithoutExtension(const std::string& filePath);

std::string PAG_API getFileNameFromUrl(const std::string& url);

bool PAG_API remove_directory(const std::string& directory_path);

// bool PAG_API runOnServer();

}  // namespace pag
