#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {

EllipseHazeEffect::~EllipseHazeEffect() {
  delete blurRadius;
  delete bloom;
  delete whiten;
}

bool EllipseHazeEffect::visibleAt(Frame layerFrame) const {
  if (effectOpacity == nullptr || blurRadius == nullptr || bloom == nullptr || whiten == nullptr) {
    return false;
  }
  auto opacity = effectOpacity->getValueAt(layerFrame);
  if (opacity == Transparent) {
    return false;
  }
  auto r = blurRadius->getValueAt(layerFrame);
  auto b = bloom->getValueAt(layerFrame);
  auto w = whiten->getValueAt(layerFrame);
  return r != 0.0f || b != 0.0f || w != 0.0f;
}

void EllipseHazeEffect::excludeVaryingRanges(std::vector<TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  blurRadius->excludeVaryingRanges(timeRanges);
  bloom->excludeVaryingRanges(timeRanges);
  whiten->excludeVaryingRanges(timeRanges);
}

bool EllipseHazeEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(blurRadius != nullptr && bloom != nullptr && whiten != nullptr &&
                  effectOpacity != nullptr);
}

}  // namespace pag
