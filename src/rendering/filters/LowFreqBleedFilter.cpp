#include "LowFreqBleedFilter.h"

#include "base/utils/TGFXCast.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/gpu/Surface.h"

namespace pag {

static const char FRAGMENT_SHADER[] = R"(
        #version 100
        precision highp float;
        varying vec2 vertexColor;
        uniform sampler2D sTexture;
        uniform sampler2D lowTexture;
        uniform vec2 uTexelSize;
        uniform float uStrength;
        uniform float uEffectOpacity;

        vec3 unpremul(vec4 c) {
            float a = max(c.a, 1e-6);
            return c.rgb / a;
        }

        vec3 smallBlur3x3(vec2 uv) {
            vec2 o = uTexelSize;
            vec3 sum = vec3(0.0);
            sum += unpremul(texture2D(sTexture, uv + vec2(-o.x, -o.y)));
            sum += unpremul(texture2D(sTexture, uv + vec2( 0.0, -o.y)));
            sum += unpremul(texture2D(sTexture, uv + vec2( o.x, -o.y)));
            sum += unpremul(texture2D(sTexture, uv + vec2(-o.x,  0.0)));
            sum += unpremul(texture2D(sTexture, uv));
            sum += unpremul(texture2D(sTexture, uv + vec2( o.x,  0.0)));
            sum += unpremul(texture2D(sTexture, uv + vec2(-o.x,  o.y)));
            sum += unpremul(texture2D(sTexture, uv + vec2( 0.0,  o.y)));
            sum += unpremul(texture2D(sTexture, uv + vec2( o.x,  o.y)));
            return sum / 9.0;
        }

        void main() {
            vec4 src = texture2D(sTexture, vertexColor);
            vec3 base = unpremul(src);
            vec3 low = unpremul(texture2D(lowTexture, vertexColor));
            vec3 small = smallBlur3x3(vertexColor);
            vec3 high = base - small;

            float k = clamp(uStrength, 0.0, 1.0);
            vec3 outRGB = low + high;
            outRGB = mix(base, outRGB, k);
            outRGB = mix(base, outRGB, clamp(uEffectOpacity, 0.0, 1.0));
            outRGB = clamp(outRGB, 0.0, 1.0);
            gl_FragColor = vec4(outRGB * src.a, src.a);
        }
    )";

LowFreqBleedFilter::LowFreqBleedFilter(Effect* effect) : effect(effect) {
}

bool LowFreqBleedFilter::initialize(tgfx::Context* context) {
  return LayerFilter::initialize(context);
}

std::string LowFreqBleedFilter::onBuildFragmentShader() {
  return FRAGMENT_SHADER;
}

void LowFreqBleedFilter::onPrepareProgram(tgfx::Context* context, unsigned program) {
  auto gl = tgfx::GLFunctions::Get(context);
  lowTextureHandle = gl->getUniformLocation(program, "lowTexture");
  texelSizeHandle = gl->getUniformLocation(program, "uTexelSize");
  strengthHandle = gl->getUniformLocation(program, "uStrength");
  effectOpacityHandle = gl->getUniformLocation(program, "uEffectOpacity");
}

bool LowFreqBleedFilter::updateLowTexture(tgfx::Context* context, const FilterSource* source) {
  auto* e = reinterpret_cast<const LowFreqBleedEffect*>(effect);
  float radius = e->radius ? e->radius->getValueAt(layerFrame) : 0.0f;
  radius = std::max(0.0f, std::min(400.0f, radius));
  if (radius <= 0.0f) {
    return false;
  }

  if (lowBuffer == nullptr || lowBuffer->width() != source->width || lowBuffer->height() != source->height) {
    lowBuffer = FilterBuffer::Make(context, source->width, source->height);
  }
  if (lowBuffer == nullptr) {
    return false;
  }
  lowBuffer->clearColor();

  tgfx::BackendTexture backendTexture = {source->sampler, source->width, source->height};
  auto image = tgfx::Image::MakeFrom(context, backendTexture);
  if (image == nullptr) {
    return false;
  }

  float sigmaX = radius * filterScale.x * source->scale.x;
  float sigmaY = radius * filterScale.y * source->scale.y;
  sigmaX = std::max(0.0f, std::min(256.0f, sigmaX));
  sigmaY = std::max(0.0f, std::min(256.0f, sigmaY));

  auto blurFilter = tgfx::ImageFilter::Blur(sigmaX, sigmaY, tgfx::TileMode::Clamp);
  tgfx::Point offset = tgfx::Point::Zero();
  auto blurred = image->makeWithFilter(std::move(blurFilter), &offset);
  if (blurred == nullptr) {
    return false;
  }
  blurred = blurred->makeSubset(tgfx::Rect::MakeXYWH(-offset.x, -offset.y,
                                                    static_cast<float>(source->width),
                                                    static_cast<float>(source->height)));
  if (blurred == nullptr) {
    return false;
  }

  tgfx::BackendRenderTarget renderTarget = {lowBuffer->getFramebuffer(), source->width, source->height};
  auto surface = tgfx::Surface::MakeFrom(context, renderTarget, tgfx::ImageOrigin::TopLeft);
  if (surface == nullptr) {
    return false;
  }
  auto canvas = surface->getCanvas();
  canvas->save();
  canvas->drawImage(blurred, 0, 0);
  canvas->restore();
  surface->flush();

  lowTexture = lowBuffer->getTexture();
  return true;
}

void LowFreqBleedFilter::draw(tgfx::Context* context, const FilterSource* source,
                             const FilterTarget* target) {
  sourceWidth = source ? source->width : 1;
  sourceHeight = source ? source->height : 1;
  sourceScale = source ? source->scale : tgfx::Point::Make(1.0f, 1.0f);
  if (!updateLowTexture(context, source)) {
    LayerFilter::draw(context, source, target);
    return;
  }
  LayerFilter::draw(context, source, target);
}

void LowFreqBleedFilter::onUpdateParams(tgfx::Context* context, const tgfx::Rect&, const tgfx::Point&) {
  auto* e = reinterpret_cast<const LowFreqBleedEffect*>(effect);
  float strength = e->strength ? e->strength->getValueAt(layerFrame) : 1.0f;
  strength = std::max(0.0f, std::min(1.0f, strength));
  float opacity = e->effectOpacity ? ToAlpha(e->effectOpacity->getValueAt(layerFrame)) : 1.0f;

  auto gl = tgfx::GLFunctions::Get(context);
  gl->uniform1i(lowTextureHandle, 1);
  gl->uniform2f(texelSizeHandle,
                sourceWidth > 0 ? (sourceScale.x / static_cast<float>(sourceWidth)) : 0.0f,
                sourceHeight > 0 ? (sourceScale.y / static_cast<float>(sourceHeight)) : 0.0f);
  gl->uniform1f(strengthHandle, strength);
  gl->uniform1f(effectOpacityHandle, opacity);
  ActiveGLTexture(context, 1, &lowTexture);
}

}  // namespace pag
