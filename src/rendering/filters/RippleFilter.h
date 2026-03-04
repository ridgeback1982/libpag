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

#pragma once

#include "LayerFilter.h"

namespace pag {
class RippleFilter : public LayerFilter {
 public:
  explicit RippleFilter(Effect* effect);
  ~RippleFilter() override = default;

 protected:
  std::string onBuildVertexShader() override;

  std::string onBuildFragmentShader() override;

  void onPrepareProgram(tgfx::Context* context, unsigned program) override;

  void onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                      const tgfx::Point& filterScale) override;

 private:
  Effect* effect = nullptr;

  int rippleCenterHandle = -1;
  int contentSizeHandle = -1;
  int radiusHandle = -1;
  int amplitudeHandle = -1;
  int wavelengthHandle = -1;
  int phaseHandle = -1;
  int pinningHandle = -1;
  int useFalloffHandle = -1;
};
}  // namespace pag
