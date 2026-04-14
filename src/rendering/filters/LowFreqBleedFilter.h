#pragma once

#include "LayerFilter.h"
#include "rendering/filters/utils/FilterBuffer.h"

namespace pag {
class LowFreqBleedFilter : public LayerFilter {
 public:
  explicit LowFreqBleedFilter(Effect* effect);
  ~LowFreqBleedFilter() override = default;

  bool initialize(tgfx::Context* context) override;

  void draw(tgfx::Context* context, const FilterSource* source,
            const FilterTarget* target) override;

 protected:
  std::string onBuildFragmentShader() override;

  void onPrepareProgram(tgfx::Context* context, unsigned program) override;

  void onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                      const tgfx::Point& filterScale) override;

 private:
  Effect* effect = nullptr;

  std::shared_ptr<FilterBuffer> lowBuffer = nullptr;
  tgfx::GLTextureInfo lowTexture = {};
  int sourceWidth = 1;
  int sourceHeight = 1;
  tgfx::Point sourceScale = tgfx::Point::Make(1.0f, 1.0f);

  int lowTextureHandle = -1;
  int texelSizeHandle = -1;
  int strengthHandle = -1;
  int effectOpacityHandle = -1;

  bool updateLowTexture(tgfx::Context* context, const FilterSource* source);
};
}  // namespace pag
