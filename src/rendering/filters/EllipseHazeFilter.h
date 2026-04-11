#pragma once

#include "LayerFilter.h"

namespace pag {
class EllipseHazeFilter : public LayerFilter {
 public:
  explicit EllipseHazeFilter(Effect* effect);
  ~EllipseHazeFilter() override = default;

 protected:
  std::string onBuildFragmentShader() override;

  void onPrepareProgram(tgfx::Context* context, unsigned program) override;

  void onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                      const tgfx::Point& filterScale) override;

 private:
  Effect* effect = nullptr;
  int centerHandle = -1;
  int radiusHandle = -1;
  int blurHandle = -1;
  int featherHandle = -1;
  int texelSizeHandle = -1;
  int effectOpacityHandle = -1;
};
}  // namespace pag

