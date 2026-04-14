#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {

LowFreqBleedEffect::~LowFreqBleedEffect() {
  delete radius;
  delete strength;
}

bool LowFreqBleedEffect::visibleAt(Frame layerFrame) const {
  if (radius == nullptr || strength == nullptr || effectOpacity == nullptr) {
    return false;
  }
  if (effectOpacity->getValueAt(layerFrame) == Transparent) {
    return false;
  }
  return radius->getValueAt(layerFrame) > 0.0f && strength->getValueAt(layerFrame) > 0.0f;
}

void LowFreqBleedEffect::excludeVaryingRanges(std::vector<TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  radius->excludeVaryingRanges(timeRanges);
  strength->excludeVaryingRanges(timeRanges);
}

bool LowFreqBleedEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(radius != nullptr && strength != nullptr && effectOpacity != nullptr);
}

}  // namespace pag

