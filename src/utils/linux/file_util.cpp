#include <iostream>
#include <cstdlib> // for getenv
#include <string>
#include "pag/defines.h"

namespace pag {

PAG_API std::string getPlatformTemporaryDirectory() {
    const char* tmpdir = std::getenv("TMPDIR");
    if (tmpdir != nullptr) {
        return std::string(tmpdir);
    } else {
        return "/tmp";
    }
}


}  // namespace pag