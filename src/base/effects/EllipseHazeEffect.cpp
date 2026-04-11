#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {

EllipseHazeEffect::~EllipseHazeEffect() {
  delete center;
  delete radius;
  delete innerBlur;
  delete outerBlur;
  delete feather;
}

bool EllipseHazeEffect::visibleAt(Frame layerFrame) const {
  if (effectOpacity == nullptr || innerBlur == nullptr || outerBlur == nullptr) {
    return false;
  }
  auto opacity = effectOpacity->getValueAt(layerFrame);
  if (opacity == Transparent) {
    return false;
  }
  auto inBlur = innerBlur->getValueAt(layerFrame);
  auto outBlur = outerBlur->getValueAt(layerFrame);
  return inBlur != 0.0f || outBlur != 0.0f;
}

void EllipseHazeEffect::excludeVaryingRanges(std::vector<TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  center->excludeVaryingRanges(timeRanges);
  radius->excludeVaryingRanges(timeRanges);
  innerBlur->excludeVaryingRanges(timeRanges);
  outerBlur->excludeVaryingRanges(timeRanges);
  feather->excludeVaryingRanges(timeRanges);
}

bool EllipseHazeEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(center != nullptr && radius != nullptr && innerBlur != nullptr &&
                  outerBlur != nullptr && feather != nullptr && effectOpacity != nullptr);
}

}  // namespace pag

