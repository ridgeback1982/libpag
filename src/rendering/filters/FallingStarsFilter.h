#pragma once

#include "LayerFilter.h"

namespace pag {
class FallingStarsFilter : public LayerFilter {
 public:
  explicit FallingStarsFilter(Effect* effect);
  ~FallingStarsFilter() override = default;

 protected:
  std::string onBuildFragmentShader() override;

  void onPrepareProgram(tgfx::Context* context, unsigned program) override;

  void onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                      const tgfx::Point& filterScale) override;

 private:
  Effect* effect = nullptr;
  int starCountHandle = -1;
  int starSizeHandle = -1;
  int fallSpeedHandle = -1;
  int twinkleSpeedHandle = -1;
  int aspectHandle = -1;
  int timeHandle = -1;
  int effectOpacityHandle = -1;
};
}  // namespace pag
