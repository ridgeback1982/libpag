#pragma once

#include "base/utils/MatrixUtil.h"
#include "base/utils/TimeUtil.h"
#include "base/utils/UniqueID.h"
#include "pag/pag.h"

#include <vector>

#if defined(__APPLE__) && defined(__MACH__)
#include <CoreVideo/CoreVideo.h>
#endif


//zzy
namespace pag {

class CVImageTool {
public:
#if defined(__APPLE__) && defined(__MACH__)
    static bool hasApproxRectFrameInside(const std::string& path);
    static PAG_API std::vector<Rect> detectSubtitleRegions(CVPixelBufferRef pixelBuffer);
#endif
};

}
