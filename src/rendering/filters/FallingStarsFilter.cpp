#include "FallingStarsFilter.h"

#include "base/utils/TGFXCast.h"

namespace pag {

static const char FRAGMENT_SHADER[] = R"(
        #version 100
        precision highp float;
        varying vec2 vertexColor;
        uniform sampler2D sTexture;
        uniform float uStarCount;
        uniform float uStarSize;
        uniform float uFallSpeed;
        uniform float uTwinkleSpeed;
        uniform float uAspect;
        uniform float uTime;
        uniform float uEffectOpacity;

        float hash12(vec2 p) {
            vec3 p3 = fract(vec3(p.xyx) * 0.1031);
            p3 += dot(p3, p3.yzx + 33.33);
            return fract((p3.x + p3.y) * p3.z);
        }

        // 单个星层：在网格中按随机位置生成星点，随时间下落并闪烁。
        // 累加 3x3 邻域格子的星星，避免光晕在格子边界被截断而产生矩形硬边。
        float starLayer(vec2 uv, float count, float seed) {
            vec2 grid = uv * count;
            vec2 baseCell = floor(grid);
            float acc = 0.0;
            for (int oy = -1; oy <= 1; oy++) {
                for (int ox = -1; ox <= 1; ox++) {
                    vec2 cell = baseCell + vec2(float(ox), float(oy));
                    vec2 cellUv = grid - cell;

                    float r1 = hash12(cell + seed);
                    float r2 = hash12(cell + seed + 17.0);
                    float r3 = hash12(cell + seed + 43.0);

                    // 下落：每颗星的初始高度与下落速度都不同
                    float speed = 0.5 + r1;
                    float phase = 0.15 + 0.7 * r2 + uTime * uFallSpeed * speed;
                    // 每次下落循环开始时重新随机出生位置（水平方向），
                    // 否则每颗星都会反复从同一个点出现
                    vec2 starPos =
                        vec2(0.15 + 0.7 * hash12(cell + seed + mod(floor(phase), 97.0) * 13.0),
                             fract(phase));

                    vec2 d = cellUv - starPos;
                    d.x *= uAspect;  // 按宽高比校正，让星星保持圆形
                    float dist = length(d);

                    float size = uStarSize * (0.6 + 0.8 * r3);
                    float core = smoothstep(size, 0.0, dist);
                    float glow = smoothstep(size * 3.0, 0.0, dist);

                    // 一闪一闪：每颗星有随机相位的闪烁
                    float twinkle =
                        0.35 + 0.65 * (0.5 + 0.5 * sin(uTime * uTwinkleSpeed + r3 * 6.2831));
                    acc += clamp(core + glow * 0.5, 0.0, 1.0) * twinkle;
                }
            }
            return acc;
        }

        void main() {
            vec4 color = texture2D(sTexture, vertexColor);
            float a = max(color.a, 1e-6);
            vec3 base = color.rgb / a;

            float opacity = clamp(uEffectOpacity, 0.0, 1.0);
            float intensity = 0.0;
            intensity += starLayer(vertexColor, max(uStarCount, 1.0), 0.0);
            intensity += starLayer(vertexColor, max(uStarCount * 1.7, 1.0), 91.0) * 0.7;
            intensity = clamp(intensity, 0.0, 1.0) * opacity;

            vec3 starColor = vec3(1.0, 0.97, 0.85);   // 暖白色星光
            vec3 outRGB = clamp(base + starColor * intensity, 0.0, 1.0);
            gl_FragColor = vec4(outRGB * color.a, color.a);
        }
    )";

FallingStarsFilter::FallingStarsFilter(Effect* effect) : effect(effect) {
}

std::string FallingStarsFilter::onBuildFragmentShader() {
  return FRAGMENT_SHADER;
}

void FallingStarsFilter::onPrepareProgram(tgfx::Context* context, unsigned program) {
  auto gl = tgfx::GLFunctions::Get(context);
  starCountHandle = gl->getUniformLocation(program, "uStarCount");
  starSizeHandle = gl->getUniformLocation(program, "uStarSize");
  fallSpeedHandle = gl->getUniformLocation(program, "uFallSpeed");
  twinkleSpeedHandle = gl->getUniformLocation(program, "uTwinkleSpeed");
  aspectHandle = gl->getUniformLocation(program, "uAspect");
  timeHandle = gl->getUniformLocation(program, "uTime");
  effectOpacityHandle = gl->getUniformLocation(program, "uEffectOpacity");
}

void FallingStarsFilter::onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                                        const tgfx::Point&) {
  auto* starsEffect = reinterpret_cast<const FallingStarsEffect*>(effect);
  float starCount = starsEffect->starCount ? starsEffect->starCount->getValueAt(layerFrame) : 6.0f;
  float starSize = starsEffect->starSize ? starsEffect->starSize->getValueAt(layerFrame) : 0.08f;
  float fallSpeed = starsEffect->fallSpeed ? starsEffect->fallSpeed->getValueAt(layerFrame) : 0.05f;
  float twinkleSpeed =
      starsEffect->twinkleSpeed ? starsEffect->twinkleSpeed->getValueAt(layerFrame) : 6.0f;
  float opacity =
      starsEffect->effectOpacity ? ToAlpha(starsEffect->effectOpacity->getValueAt(layerFrame)) : 1.0f;
  float aspect =
      contentBounds.height() > 0.0f ? contentBounds.width() / contentBounds.height() : 1.0f;

  auto gl = tgfx::GLFunctions::Get(context);
  gl->uniform1f(starCountHandle, starCount);
  gl->uniform1f(starSizeHandle, starSize);
  gl->uniform1f(fallSpeedHandle, fallSpeed);
  gl->uniform1f(twinkleSpeedHandle, twinkleSpeed);
  gl->uniform1f(aspectHandle, aspect);
  gl->uniform1f(timeHandle, static_cast<float>(layerFrame));
  gl->uniform1f(effectOpacityHandle, opacity);
}

}  // namespace pag
