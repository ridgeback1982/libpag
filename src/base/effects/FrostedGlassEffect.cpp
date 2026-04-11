#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {

FrostedGlassEffect::~FrostedGlassEffect() {
  delete blurRadius;
  delete translucency;
  delete colorBleed;
}

bool FrostedGlassEffect::visibleAt(Frame layerFrame) const {
  if (blurRadius == nullptr || translucency == nullptr || colorBleed == nullptr || effectOpacity == nullptr) {
    return false;
  }
  if (effectOpacity->getValueAt(layerFrame) == Transparent) {
    return false;
  }
  return blurRadius->getValueAt(layerFrame) != 0.0f || colorBleed->getValueAt(layerFrame) != 0.0f;
}

void FrostedGlassEffect::excludeVaryingRanges(std::vector<TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  blurRadius->excludeVaryingRanges(timeRanges);
  translucency->excludeVaryingRanges(timeRanges);
  colorBleed->excludeVaryingRanges(timeRanges);
}

bool FrostedGlassEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(blurRadius != nullptr && translucency != nullptr && colorBleed != nullptr &&
                  effectOpacity != nullptr);
}

}  // namespace pag

