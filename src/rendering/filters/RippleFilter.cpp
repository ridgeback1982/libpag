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

#include "RippleFilter.h"

namespace pag {
static const char RIPPLE_VERTEX_SHADER[] = R"(
        #version 100
        attribute vec2 aPosition;
        attribute vec2 aTextureCoord;
        uniform mat3 uVertexMatrix;
        uniform mat3 uTextureMatrix;
        uniform vec2 uRippleCenter;
        varying vec2 vertexColor;
        varying vec2 rippleCenter;
        void main() {
            vec3 position = uVertexMatrix * vec3(aPosition, 1);
            gl_Position = vec4(position.xy, 0, 1);
            vec3 colorPosition = uTextureMatrix * vec3(aTextureCoord, 1);
            vertexColor = colorPosition.xy;
            vec3 centerPosition = uTextureMatrix * vec3(uRippleCenter, 1);
            rippleCenter = centerPosition.xy;
        }
    )";

static const char RIPPLE_FRAGMENT_SHADER[] = R"(
    #version 100
    precision highp float;
    varying highp vec2 vertexColor;
    varying highp vec2 rippleCenter;
    uniform sampler2D inputImageTexture;

    uniform vec2 uContentSize;
    uniform float uRadius;
    uniform float uAmplitude;
    uniform float uWavelength;
    uniform float uPhase;
    uniform bool uPinning;
    uniform bool uUseFalloff;

    float edge = 0.005;
    float PI2 = 6.28318530718;

    void main() {
        vec2 target = vertexColor;

        vec2 p = (target - rippleCenter) * uContentSize;
        float dist = length(p);

        if (uRadius > 0.0 && uWavelength > 0.0 && dist <= uRadius) {
            float wave = sin((dist / uWavelength) * PI2 + uPhase);
            float falloff = uUseFalloff ? (1.0 - dist / uRadius) : 1.0;
            float offsetAmount = wave * uAmplitude * falloff;
            vec2 dir = dist > 0.0001 ? (p / dist) : vec2(0.0);
            vec2 offsetUV = (dir * offsetAmount) / uContentSize;
            target = target + offsetUV;
        }

        if (uPinning) {
            target.x = clamp(target.x, edge, 1.0 - edge);
            target.y = clamp(target.y, edge, 1.0 - edge);
        }

        vec2 edgeDetect = abs(step(vec2(1.0), target) - vec2(1.0)) * step(vec2(0.0), target);
        gl_FragColor = texture2D(inputImageTexture, target) * edgeDetect.x * edgeDetect.y;
    }
    )";

RippleFilter::RippleFilter(Effect* effect) : effect(effect) {
}

std::string RippleFilter::onBuildVertexShader() {
  return RIPPLE_VERTEX_SHADER;
}

std::string RippleFilter::onBuildFragmentShader() {
  return RIPPLE_FRAGMENT_SHADER;
}

void RippleFilter::onPrepareProgram(tgfx::Context* context, unsigned program) {
  auto gl = tgfx::GLFunctions::Get(context);
  rippleCenterHandle = gl->getUniformLocation(program, "uRippleCenter");
  contentSizeHandle = gl->getUniformLocation(program, "uContentSize");
  radiusHandle = gl->getUniformLocation(program, "uRadius");
  amplitudeHandle = gl->getUniformLocation(program, "uAmplitude");
  wavelengthHandle = gl->getUniformLocation(program, "uWavelength");
  phaseHandle = gl->getUniformLocation(program, "uPhase");
  pinningHandle = gl->getUniformLocation(program, "uPinning");
  useFalloffHandle = gl->getUniformLocation(program, "uUseFalloff");
}

void RippleFilter::onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                                  const tgfx::Point& filterScale) {
  auto* rippleEffect = reinterpret_cast<const RippleEffect*>(effect);
  auto rippleCenter = rippleEffect->rippleCenter->getValueAt(layerFrame);
  auto radius = rippleEffect->radius->getValueAt(layerFrame);
  auto amplitude = rippleEffect->amplitude->getValueAt(layerFrame);
  auto wavelength = rippleEffect->wavelength->getValueAt(layerFrame);
  auto phase = rippleEffect->phase->getValueAt(layerFrame);
  auto pinning = rippleEffect->pinning->getValueAt(layerFrame);
  auto useFalloff = rippleEffect->useFalloff->getValueAt(layerFrame);

  auto scale = (filterScale.x + filterScale.y) * 0.5f;
  radius *= scale;
  amplitude *= scale;
  wavelength *= scale;

  auto gl = tgfx::GLFunctions::Get(context);
  gl->uniform2f(rippleCenterHandle, (rippleCenter.x - contentBounds.x()) / contentBounds.width(),
                (rippleCenter.y - contentBounds.y()) / contentBounds.height());
  gl->uniform2f(contentSizeHandle, contentBounds.width(), contentBounds.height());
  gl->uniform1f(radiusHandle, radius);
  gl->uniform1f(amplitudeHandle, amplitude);
  gl->uniform1f(wavelengthHandle, wavelength);
  gl->uniform1f(phaseHandle, phase);
  gl->uniform1i(pinningHandle, pinning);
  gl->uniform1i(useFalloffHandle, useFalloff);
}
}  // namespace pag
