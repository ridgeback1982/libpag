#include "EllipseHazeFilter.h"

#include "base/utils/TGFXCast.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/gpu/Surface.h"

namespace pag {

EllipseHazeFilter::EllipseHazeFilter(Effect* effect) : effect(effect) {
}

bool EllipseHazeFilter::initialize(tgfx::Context*) {
  return true;
}

void EllipseHazeFilter::draw(tgfx::Context* context, const FilterSource* source,
                             const FilterTarget* target) {
  if (context == nullptr || source == nullptr || target == nullptr) {
    LOGE("EllipseHazeFilter::draw() can not draw filter");
    return;
  }
  auto* haze = reinterpret_cast<const EllipseHazeEffect*>(effect);
  float blurRadius = haze->blurRadius ? haze->blurRadius->getValueAt(layerFrame) : 0.0f;
  float bloom = haze->bloom ? haze->bloom->getValueAt(layerFrame) : 0.0f;
  float whiten = haze->whiten ? haze->whiten->getValueAt(layerFrame) : 0.0f;
  float opacity = haze->effectOpacity ? ToAlpha(haze->effectOpacity->getValueAt(layerFrame)) : 1.0f;

  blurRadius = std::max(0.0f, std::min(64.0f, blurRadius));
  bloom = std::max(0.0f, std::min(1.0f, bloom));
  whiten = std::max(0.0f, std::min(1.0f, whiten));
  opacity = std::max(0.0f, std::min(1.0f, opacity));

  float sigmaX = blurRadius * filterScale.x * source->scale.x;
  float sigmaY = blurRadius * filterScale.y * source->scale.y;
  sigmaX = std::max(0.0f, std::min(64.0f, sigmaX));
  sigmaY = std::max(0.0f, std::min(64.0f, sigmaY));

  tgfx::BackendRenderTarget renderTarget = {target->frameBuffer, target->width, target->height};
  auto targetSurface = tgfx::Surface::MakeFrom(context, renderTarget, tgfx::ImageOrigin::TopLeft);
  auto canvas = targetSurface->getCanvas();

  tgfx::BackendTexture backendTexture = {source->sampler, source->width, source->height};
  auto image = tgfx::Image::MakeFrom(context, backendTexture);
  if (image == nullptr) {
    LOGE("EllipseHazeFilter::draw() failed to create an Image from the backend texture!");
    return;
  }

  canvas->save();
  canvas->setMatrix(ToMatrix(target));
  canvas->drawImage(image, 0, 0);

  if (opacity > 0.0f && (sigmaX > 0.0f || sigmaY > 0.0f) && (bloom > 0.0f || whiten > 0.0f)) {
    auto blurFilter = tgfx::ImageFilter::Blur(sigmaX, sigmaY, tgfx::TileMode::Clamp);
    tgfx::Point offset = tgfx::Point::Zero();
    auto blurred = image->makeWithFilter(std::move(blurFilter), &offset);
    if (blurred != nullptr) {
      blurred = blurred->makeSubset(tgfx::Rect::MakeXYWH(-offset.x, -offset.y,
                                                        static_cast<float>(source->width),
                                                        static_cast<float>(source->height)));

      if (bloom > 0.0f) {
        tgfx::Paint p;
        p.setAlpha(opacity * (0.30f + 0.70f * bloom));
        p.setBlendMode(tgfx::BlendMode::Screen);
        canvas->drawImage(blurred, 0, 0, &p);
      }
      if (whiten > 0.0f) {
        tgfx::Paint veil;
        auto white = tgfx::Color::White();
        white.alpha = opacity * whiten * 0.60f;
        veil.setColor(white);
        veil.setBlendMode(tgfx::BlendMode::Screen);
        canvas->drawRect(tgfx::Rect::MakeWH(static_cast<float>(source->width),
                                            static_cast<float>(source->height)),
                         veil);
      }
    }
  }
  canvas->restore();
  targetSurface->flush();
}

}  // namespace pag
