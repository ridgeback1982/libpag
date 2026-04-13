#include "FrostedGlassFilter.h"

#include "base/utils/TGFXCast.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/gpu/Surface.h"

namespace pag {

FrostedGlassFilter::FrostedGlassFilter(Effect* effect) : effect(effect) {
}

bool FrostedGlassFilter::initialize(tgfx::Context*) {
  return true;
}

void FrostedGlassFilter::draw(tgfx::Context* context, const FilterSource* source,
                             const FilterTarget* target) {
  if (source == nullptr || target == nullptr) {
    LOGE("FrostedGlassFilter::draw() can not draw filter");
    return;
  }
  auto* frosted = reinterpret_cast<const FrostedGlassEffect*>(effect);
  float blurRadius = frosted->blurRadius ? frosted->blurRadius->getValueAt(layerFrame) : 0.0f;
  float translucency =
      frosted->translucency ? frosted->translucency->getValueAt(layerFrame) : 0.0f;
  float colorBleed = frosted->colorBleed ? frosted->colorBleed->getValueAt(layerFrame) : 0.0f;
  float opacity =
      frosted->effectOpacity ? ToAlpha(frosted->effectOpacity->getValueAt(layerFrame)) : 1.0f;

  translucency = std::max(0.0f, std::min(1.0f, translucency));
  colorBleed = std::max(0.0f, std::min(2.0f, colorBleed));
  opacity = std::max(0.0f, std::min(1.0f, opacity));

  float sigmaX = blurRadius * filterScale.x * source->scale.x;
  float sigmaY = blurRadius * filterScale.y * source->scale.y;
  float bleedFactor = 1.0f + 1.6f * colorBleed + 3.0f * colorBleed * colorBleed;
  sigmaX = std::max(0.0f, std::min(64.0f, sigmaX * bleedFactor));
  sigmaY = std::max(0.0f, std::min(64.0f, sigmaY * bleedFactor));

  float mixK = opacity * (1.0f - translucency);
  mixK = std::max(0.0f, std::min(1.0f, mixK));

  tgfx::BackendRenderTarget renderTarget = {target->frameBuffer, target->width, target->height};
  auto targetSurface = tgfx::Surface::MakeFrom(context, renderTarget, tgfx::ImageOrigin::TopLeft);
  auto targetCanvas = targetSurface->getCanvas();

  tgfx::BackendTexture backendTexture = {source->sampler, source->width, source->height};
  auto image = tgfx::Image::MakeFrom(context, backendTexture);
  if (image == nullptr) {
    LOGE("FrostedGlassFilter::draw() failed to create an Image from the backend texture!");
    return;
  }

  targetCanvas->save();
  targetCanvas->setMatrix(ToMatrix(target));
  targetCanvas->drawImage(image, 0, 0);
  if (mixK > 0.0f && (sigmaX > 0.0f || sigmaY > 0.0f)) {
    auto blurFilter = tgfx::ImageFilter::Blur(sigmaX, sigmaY, tgfx::TileMode::Clamp);
    tgfx::Point offset = tgfx::Point::Zero();
    auto blurred = image->makeWithFilter(std::move(blurFilter), &offset);
    if (blurred != nullptr) {
      blurred = blurred->makeSubset(tgfx::Rect::MakeXYWH(-offset.x, -offset.y,
                                                        static_cast<float>(source->width),
                                                        static_cast<float>(source->height)));
      tgfx::Paint paint;
      paint.setAlpha(mixK);
      targetCanvas->drawImage(std::move(blurred), 0, 0, &paint);
    }
  }
  targetCanvas->restore();
  targetSurface->flush();
}

}  // namespace pag
