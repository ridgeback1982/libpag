#include "WhiteVeilFilter.h"

#include "base/utils/TGFXCast.h"

namespace pag {

static const char FRAGMENT_SHADER[] = R"(
        #version 100
        precision highp float;
        varying vec2 vertexColor;
        uniform sampler2D sTexture;
        uniform float uStrength;
        uniform float uEffectOpacity;
        uniform float uUniform;
        uniform float uTime;

        float hash12(vec2 p) {
            vec3 p3 = fract(vec3(p.xyx) * 0.1031);
            p3 += dot(p3, p3.yzx + 33.33);
            return fract((p3.x + p3.y) * p3.z);
        }

        float valueNoise(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            float a = hash12(i);
            float b = hash12(i + vec2(1.0, 0.0));
            float c = hash12(i + vec2(0.0, 1.0));
            float d = hash12(i + vec2(1.0, 1.0));
            vec2 u = f * f * (3.0 - 2.0 * f);
            return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
        }

        void main() {
            vec4 src = texture2D(sTexture, vertexColor);
            float a = max(src.a, 1e-6);
            vec3 base = src.rgb / a;

            float s = clamp(uStrength, 0.0, 1.0);
            float op = clamp(uEffectOpacity, 0.0, 1.0);

            float luma = dot(base, vec3(0.299, 0.587, 0.114));
            vec3 gray = vec3(luma);
            vec3 desat = mix(base, gray, s * 0.35);

            float n = valueNoise(vertexColor * 80.0 + vec2(uTime * 0.03, uTime * 0.02));
            float texture = (n - 0.5) * 2.0;
            texture *= (1.0 - step(0.5, uUniform));

            float lift = s * (0.18 + 0.10 * texture) * (1.0 - luma);
            vec3 lifted = clamp(desat + vec3(lift), 0.0, 1.0);

            vec3 veil = mix(lifted, vec3(1.0), s * (0.12 + 0.06 * texture));
            vec3 outRGB = mix(base, veil, op);

            gl_FragColor = vec4(outRGB * src.a, src.a);
        }
    )";

WhiteVeilFilter::WhiteVeilFilter(Effect* effect) : effect(effect) {
}

std::string WhiteVeilFilter::onBuildFragmentShader() {
  return FRAGMENT_SHADER;
}

void WhiteVeilFilter::onPrepareProgram(tgfx::Context* context, unsigned program) {
  auto gl = tgfx::GLFunctions::Get(context);
  strengthHandle = gl->getUniformLocation(program, "uStrength");
  effectOpacityHandle = gl->getUniformLocation(program, "uEffectOpacity");
  uniformHandle = gl->getUniformLocation(program, "uUniform");
  timeHandle = gl->getUniformLocation(program, "uTime");
}

void WhiteVeilFilter::onUpdateParams(tgfx::Context* context, const tgfx::Rect&, const tgfx::Point&) {
  auto* veilEffect = reinterpret_cast<const WhiteVeilEffect*>(effect);
  float strength = 0.0f;
  float opacity = 1.0f;
  float uniform = 0.0f;
  if (veilEffect->strength) {
    strength = veilEffect->strength->getValueAt(layerFrame);
  }
  if (veilEffect->effectOpacity) {
    opacity = ToAlpha(veilEffect->effectOpacity->getValueAt(layerFrame));
  }
  if (veilEffect->uniform) {
    uniform = veilEffect->uniform->getValueAt(layerFrame) ? 1.0f : 0.0f;
  }

  auto gl = tgfx::GLFunctions::Get(context);
  gl->uniform1f(strengthHandle, strength);
  gl->uniform1f(effectOpacityHandle, opacity);
  gl->uniform1f(uniformHandle, uniform);
  gl->uniform1f(timeHandle, static_cast<float>(layerFrame));
}

}  // namespace pag
