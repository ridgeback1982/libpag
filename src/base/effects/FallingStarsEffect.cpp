#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {

FallingStarsEffect::~FallingStarsEffect() {
  delete starCount;
  delete starSize;
  delete fallSpeed;
  delete twinkleSpeed;
}

bool FallingStarsEffect::visibleAt(Frame layerFrame) const {
  if (starCount == nullptr || starSize == nullptr || fallSpeed == nullptr ||
      twinkleSpeed == nullptr || effectOpacity == nullptr) {
    return false;
  }
  if (effectOpacity->getValueAt(layerFrame) == Transparent) {
    return false;
  }
  return starCount->getValueAt(layerFrame) > 0.0f && starSize->getValueAt(layerFrame) > 0.0f;
}

void FallingStarsEffect::excludeVaryingRanges(std::vector<TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  starCount->excludeVaryingRanges(timeRanges);
  starSize->excludeVaryingRanges(timeRanges);
  fallSpeed->excludeVaryingRanges(timeRanges);
  twinkleSpeed->excludeVaryingRanges(timeRanges);
}

bool FallingStarsEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(starCount != nullptr && starSize != nullptr && fallSpeed != nullptr &&
                  twinkleSpeed != nullptr && effectOpacity != nullptr);
}

}  // namespace pag
