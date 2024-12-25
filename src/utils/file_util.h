#pragma once
#include <iostream>

namespace pag {

std::string getPlatformTemporaryDirectory();

std::string findEnglishFontName(const std::string& chineseFontName);

}  // namespace pag