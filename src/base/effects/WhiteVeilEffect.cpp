#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {

WhiteVeilEffect::~WhiteVeilEffect() {
  delete strength;
  delete uniform;
}

bool WhiteVeilEffect::visibleAt(Frame layerFrame) const {
  if (strength == nullptr || effectOpacity == nullptr) {
    return false;
  }
  auto s = strength->getValueAt(layerFrame);
  auto opacity = effectOpacity->getValueAt(layerFrame);
  return s != 0.0f && opacity != Transparent;
}

void WhiteVeilEffect::excludeVaryingRanges(std::vector<TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  strength->excludeVaryingRanges(timeRanges);
  uniform->excludeVaryingRanges(timeRanges);
}

bool WhiteVeilEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(strength != nullptr && uniform != nullptr && effectOpacity != nullptr);
}

}  // namespace pag
