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

#include "RippleEffect.h"
#include "EffectCompositingOption.h"

namespace pag {
std::unique_ptr<BlockConfig> RippleEffectTag(RippleEffect* effect) {
  auto tagConfig = new BlockConfig(TagCode::RippleEffect);
  AddAttribute(tagConfig, &effect->rippleCenter, AttributeType::SpatialProperty,
               Point::Make(640.0f, 360.0f));
  AddAttribute(tagConfig, &effect->radius, AttributeType::SimpleProperty, 50.0f);
  AddAttribute(tagConfig, &effect->amplitude, AttributeType::SimpleProperty, 10.0f);
  AddAttribute(tagConfig, &effect->wavelength, AttributeType::SimpleProperty, 20.0f);
  AddAttribute(tagConfig, &effect->phase, AttributeType::SimpleProperty, 0.0f);
  AddAttribute(tagConfig, &effect->pinning, AttributeType::DiscreteProperty, false);
  AddAttribute(tagConfig, &effect->useFalloff, AttributeType::DiscreteProperty, true);
  EffectCompositingOptionTag(tagConfig, effect);
  return std::unique_ptr<BlockConfig>(tagConfig);
}
}  // namespace pag
