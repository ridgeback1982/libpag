#include "EllipseHazeFilter.h"

#include "base/utils/TGFXCast.h"

namespace pag {

static const char FRAGMENT_SHADER[] = R"(
        #version 100
        precision highp float;
        varying vec2 vertexColor;
        uniform sampler2D sTexture;

        uniform vec2 uCenter;
        uniform vec2 uRadius;
        uniform vec2 uBlur;
        uniform float uFeather;
        uniform vec2 uTexelSize;
        uniform float uEffectOpacity;

        float ellipseDistance(vec2 uv, vec2 center, vec2 radius) {
            vec2 d = (uv - center) / max(radius, vec2(1e-6));
            return length(d);
        }

        vec4 blur9(vec2 uv, float blurRadiusPx) {
            vec2 o = uTexelSize * blurRadiusPx;
            vec4 c = texture2D(sTexture, uv) * 0.28;
            c += texture2D(sTexture, uv + vec2( o.x, 0.0)) * 0.10;
            c += texture2D(sTexture, uv + vec2(-o.x, 0.0)) * 0.10;
            c += texture2D(sTexture, uv + vec2(0.0,  o.y)) * 0.10;
            c += texture2D(sTexture, uv + vec2(0.0, -o.y)) * 0.10;
            c += texture2D(sTexture, uv + vec2( o.x,  o.y)) * 0.08;
            c += texture2D(sTexture, uv + vec2(-o.x,  o.y)) * 0.08;
            c += texture2D(sTexture, uv + vec2( o.x, -o.y)) * 0.08;
            c += texture2D(sTexture, uv + vec2(-o.x, -o.y)) * 0.08;
            return c;
        }

        void main() {
            vec4 src = texture2D(sTexture, vertexColor);
            float a = max(src.a, 1e-6);
            vec3 base = src.rgb / a;

            float d = ellipseDistance(vertexColor, uCenter, uRadius);
            float feather = clamp(uFeather, 0.0, 1.0);
            float inner = 1.0 - feather;
            float w = smoothstep(inner, 1.0, d);

            float blurPx = mix(uBlur.x, uBlur.y, w);
            blurPx = clamp(blurPx, 0.0, 64.0);

            vec4 blurred = blur9(vertexColor, blurPx);
            vec3 blurredRGB = blurred.rgb / max(blurred.a, 1e-6);

            vec3 hazed = mix(base, blurredRGB, clamp(uEffectOpacity, 0.0, 1.0));
            gl_FragColor = vec4(hazed * src.a, src.a);
        }
    )";

EllipseHazeFilter::EllipseHazeFilter(Effect* effect) : effect(effect) {
}

std::string EllipseHazeFilter::onBuildFragmentShader() {
  return FRAGMENT_SHADER;
}

void EllipseHazeFilter::onPrepareProgram(tgfx::Context* context, unsigned program) {
  auto gl = tgfx::GLFunctions::Get(context);
  centerHandle = gl->getUniformLocation(program, "uCenter");
  radiusHandle = gl->getUniformLocation(program, "uRadius");
  blurHandle = gl->getUniformLocation(program, "uBlur");
  featherHandle = gl->getUniformLocation(program, "uFeather");
  texelSizeHandle = gl->getUniformLocation(program, "uTexelSize");
  effectOpacityHandle = gl->getUniformLocation(program, "uEffectOpacity");
}

void EllipseHazeFilter::onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                                      const tgfx::Point& filterScale) {
  auto* hazeEffect = reinterpret_cast<const EllipseHazeEffect*>(effect);

  auto center = hazeEffect->center->getValueAt(layerFrame);
  auto radius = hazeEffect->radius->getValueAt(layerFrame);
  float innerBlur = hazeEffect->innerBlur->getValueAt(layerFrame);
  float outerBlur = hazeEffect->outerBlur->getValueAt(layerFrame);
  float feather = hazeEffect->feather->getValueAt(layerFrame);
  float opacity = ToAlpha(hazeEffect->effectOpacity->getValueAt(layerFrame));

  innerBlur *= 0.5f * (filterScale.x + filterScale.y);
  outerBlur *= 0.5f * (filterScale.x + filterScale.y);

  float texelX = 1.0f / std::max(1.0f, contentBounds.width());
  float texelY = 1.0f / std::max(1.0f, contentBounds.height());

  auto gl = tgfx::GLFunctions::Get(context);
  gl->uniform2f(centerHandle, center.x, center.y);
  gl->uniform2f(radiusHandle, radius.x, radius.y);
  gl->uniform2f(blurHandle, innerBlur, outerBlur);
  gl->uniform1f(featherHandle, feather);
  gl->uniform2f(texelSizeHandle, texelX, texelY);
  gl->uniform1f(effectOpacityHandle, opacity);
}

}  // namespace pag

