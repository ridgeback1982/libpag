#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"


//zzy
namespace pag {

class CVImageTool {
public:
#if defined(__linux__)
    static bool hasRectFrameInside(const std::string& path);




#endif
};

}