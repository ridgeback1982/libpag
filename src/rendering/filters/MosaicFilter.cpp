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

#include "MosaicFilter.h"

namespace pag {
static const char VERTEX_SHADER[] = R"(
        #version 100
        attribute vec2 aPosition;
        attribute vec2 aTextureCoord;
        uniform mat3 uVertexMatrix;
        uniform mat3 uTextureMatrix;
        uniform vec2 uCenter;
        varying vec2 vertexColor;
        varying vec2 vCenter;
        void main() {
            vec3 position = uVertexMatrix * vec3(aPosition, 1);
            gl_Position = vec4(position.xy, 0, 1);
            vec3 colorPosition = uTextureMatrix * vec3(aTextureCoord, 1);
            vertexColor = colorPosition.xy;
            vec3 centerPosition = uTextureMatrix * vec3(uCenter, 1);
            vCenter = centerPosition.xy;
        }
    )";

static const char FRAGMENT_SHADER[] = R"(
        #version 100
        precision mediump float;
        varying vec2 vertexColor;
        varying vec2 vCenter;
        uniform sampler2D sTexture;
        uniform float mHorizontalBlocks;
        uniform float mVerticalBlocks;
        uniform bool mSharpColors;
        uniform float uRadius;
        uniform vec2 uContentSize;

        void main() {
            vec2 p = (vertexColor - vCenter) * uContentSize;
            float dist = length(p);
            if (uRadius > 0.0 && dist > uRadius) {
                 gl_FragColor = texture2D(sTexture, vertexColor);
            } else {
                vec2 blocks = vec2(mHorizontalBlocks, mVerticalBlocks);
                vec2 position = floor(vertexColor / blocks);
                vec2 target = blocks * position + blocks / 2.0;
                gl_FragColor = texture2D(sTexture, target);
            }
        }
    )";

MosaicFilter::MosaicFilter(pag::Effect* effect) : effect(effect) {
}

std::string MosaicFilter::onBuildVertexShader() {
  return VERTEX_SHADER;
}

std::string MosaicFilter::onBuildFragmentShader() {
  return FRAGMENT_SHADER;
}

void MosaicFilter::onPrepareProgram(tgfx::Context* context, unsigned int program) {
  auto gl = tgfx::GLFunctions::Get(context);
  horizontalBlocksHandle = gl->getUniformLocation(program, "mHorizontalBlocks");
  verticalBlocksHandle = gl->getUniformLocation(program, "mVerticalBlocks");
  sharpColorsHandle = gl->getUniformLocation(program, "mSharpColors");
  centerHandle = gl->getUniformLocation(program, "uCenter");
  radiusHandle = gl->getUniformLocation(program, "uRadius");
  contentSizeHandle = gl->getUniformLocation(program, "uContentSize");
}

void MosaicFilter::onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                                  const tgfx::Point&) {
  auto* mosaicEffect = reinterpret_cast<const MosaicEffect*>(effect);
  horizontalBlocks = 1.0f / mosaicEffect->horizontalBlocks->getValueAt(layerFrame);
  verticalBlocks = 1.0f / mosaicEffect->verticalBlocks->getValueAt(layerFrame);
  sharpColors = mosaicEffect->sharpColors->getValueAt(layerFrame);
  center = mosaicEffect->center->getValueAt(layerFrame);
  radius = mosaicEffect->radius->getValueAt(layerFrame);
  auto scale = (filterScale.x + filterScale.y) * 0.5f;
  radius *= scale;

  auto placeHolderWidth = static_cast<int>(contentBounds.left + contentBounds.right);
  auto placeHolderHeight = static_cast<int>(contentBounds.top + contentBounds.bottom);
  auto placeHolderRatio = 1.0f * placeHolderWidth / placeHolderHeight;

  auto contentWidth = static_cast<int>(contentBounds.width());
  auto contentHeight = static_cast<int>(contentBounds.height());
  auto contentRatio = 1.0f * contentWidth / contentHeight;

  if (placeHolderRatio > contentRatio) {
    horizontalBlocks *= 1.0f * placeHolderWidth / contentWidth;
  } else {
    verticalBlocks *= 1.0f * placeHolderHeight / contentHeight;
  }
  auto gl = tgfx::GLFunctions::Get(context);
  gl->uniform1f(horizontalBlocksHandle, horizontalBlocks);
  gl->uniform1f(verticalBlocksHandle, verticalBlocks);
  gl->uniform1f(sharpColorsHandle, sharpColors);
  gl->uniform2f(centerHandle, (center.x - contentBounds.x()) / contentBounds.width(),
                (center.y - contentBounds.y()) / contentBounds.height());
  gl->uniform1f(radiusHandle, radius);
  gl->uniform2f(contentSizeHandle, contentBounds.width(), contentBounds.height());
}
}  // namespace pag
