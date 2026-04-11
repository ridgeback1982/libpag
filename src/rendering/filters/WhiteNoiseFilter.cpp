#include "WhiteNoiseFilter.h"

#include "base/utils/TGFXCast.h"

namespace pag {

static const char FRAGMENT_SHADER[] = R"(
        #version 100
        precision highp float;
        varying vec2 vertexColor;
        uniform sampler2D sTexture;
        uniform float uIntensity;
        uniform float uEffectOpacity;
        uniform float uTime;

        float hash12(vec2 p) {
            vec3 p3 = fract(vec3(p.xyx) * 0.1031);
            p3 += dot(p3, p3.yzx + 33.33);
            return fract((p3.x + p3.y) * p3.z);
        }

        void main() {
            vec4 color = texture2D(sTexture, vertexColor);
            float a = max(color.a, 1e-6);
            vec3 base = color.rgb / a;
            float n = hash12(vertexColor * 1024.0 + vec2(uTime, uTime * 0.37));
            float noise = (n - 0.5) * 2.0;
            vec3 noisy = clamp(base + noise * clamp(uIntensity, 0.0, 1.0), 0.0, 1.0);
            vec3 outRGB = mix(base, noisy, clamp(uEffectOpacity, 0.0, 1.0));
            gl_FragColor = vec4(outRGB * color.a, color.a);
        }
    )";

WhiteNoiseFilter::WhiteNoiseFilter(Effect* effect) : effect(effect) {
}

std::string WhiteNoiseFilter::onBuildFragmentShader() {
  return FRAGMENT_SHADER;
}

void WhiteNoiseFilter::onPrepareProgram(tgfx::Context* context, unsigned program) {
  auto gl = tgfx::GLFunctions::Get(context);
  intensityHandle = gl->getUniformLocation(program, "uIntensity");
  effectOpacityHandle = gl->getUniformLocation(program, "uEffectOpacity");
  timeHandle = gl->getUniformLocation(program, "uTime");
}

void WhiteNoiseFilter::onUpdateParams(tgfx::Context* context, const tgfx::Rect&, const tgfx::Point&) {
  auto* noiseEffect = reinterpret_cast<const WhiteNoiseEffect*>(effect);
  float intensity = 0.0f;
  float opacity = 1.0f;
  if (noiseEffect->noiseIntensity) {
    intensity = noiseEffect->noiseIntensity->getValueAt(layerFrame);
  }
  if (noiseEffect->effectOpacity) {
    opacity = ToAlpha(noiseEffect->effectOpacity->getValueAt(layerFrame));
  }
  auto gl = tgfx::GLFunctions::Get(context);
  gl->uniform1f(intensityHandle, intensity);
  gl->uniform1f(effectOpacityHandle, opacity);
  gl->uniform1f(timeHandle, static_cast<float>(layerFrame));
}

}  // namespace pag

