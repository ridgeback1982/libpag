#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"


//zzy
namespace pag {

class CVImageTool {
public:
#if defined(__linux__) || defined(__APPLE__) && defined(__MACH__)
    static bool hasRectFrameInside(const std::string& path);
    static bool hasApproxRectFrameInside(const std::string& path);



#endif
};

}