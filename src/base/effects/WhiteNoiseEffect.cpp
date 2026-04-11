#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {

WhiteNoiseEffect::~WhiteNoiseEffect() {
  delete noiseIntensity;
}

bool WhiteNoiseEffect::visibleAt(Frame layerFrame) const {
  if (noiseIntensity == nullptr || effectOpacity == nullptr) {
    return false;
  }
  auto intensity = noiseIntensity->getValueAt(layerFrame);
  auto opacity = effectOpacity->getValueAt(layerFrame);
  return intensity != 0.0f && opacity != Transparent;
}

void WhiteNoiseEffect::excludeVaryingRanges(std::vector<TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  noiseIntensity->excludeVaryingRanges(timeRanges);
}

bool WhiteNoiseEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(noiseIntensity != nullptr && effectOpacity != nullptr);
}

}  // namespace pag

