#include <iostream>
#include <cstdlib> // for getenv
#include <string>
#include "pag/defines.h"

namespace pag {

PAG_API std::string getPlatformTemporaryDirectory() {
    const char* tmpdir = std::getenv("TEMP");
    if (tmpdir != nullptr) {
        return std::string(tmpdir);
    } else {
        return "C:/Users/Owner/Downloads/";
    }
}

PAG_API std::string findEnglishFontName(const std::string& chineseFontName) {
    // TODO: Implement this function for windows platform.
    return chineseFontName;
}


}  // namespace pag