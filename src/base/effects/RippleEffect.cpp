/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "base/utils/Verify.h"
#include "pag/file.h"

namespace pag {
RippleEffect::~RippleEffect() {
  delete rippleCenter;
  delete radius;
  delete amplitude;
  delete wavelength;
  delete phase;
  delete pinning;
  delete useFalloff;
}

bool RippleEffect::visibleAt(Frame layerFrame) const {
  auto radiusValue = radius->getValueAt(layerFrame);
  auto amplitudeValue = amplitude->getValueAt(layerFrame);
  return radiusValue != 0.0f && amplitudeValue != 0.0f;
}

void RippleEffect::transformBounds(Rect*, const Point&, Frame) const {
}

void RippleEffect::excludeVaryingRanges(std::vector<pag::TimeRange>* timeRanges) const {
  Effect::excludeVaryingRanges(timeRanges);
  rippleCenter->excludeVaryingRanges(timeRanges);
  radius->excludeVaryingRanges(timeRanges);
  amplitude->excludeVaryingRanges(timeRanges);
  wavelength->excludeVaryingRanges(timeRanges);
  phase->excludeVaryingRanges(timeRanges);
  pinning->excludeVaryingRanges(timeRanges);
  useFalloff->excludeVaryingRanges(timeRanges);
}

bool RippleEffect::verify() const {
  if (!Effect::verify()) {
    VerifyFailed();
    return false;
  }
  VerifyAndReturn(rippleCenter != nullptr && radius != nullptr && amplitude != nullptr &&
                  wavelength != nullptr && phase != nullptr && pinning != nullptr &&
                  useFalloff != nullptr);
}
}  // namespace pag
