#pragma once

#include "LayerFilter.h"

namespace pag {
class WhiteVeilFilter : public LayerFilter {
 public:
  explicit WhiteVeilFilter(Effect* effect);
  ~WhiteVeilFilter() override = default;

 protected:
  std::string onBuildFragmentShader() override;

  void onPrepareProgram(tgfx::Context* context, unsigned program) override;

  void onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                      const tgfx::Point& filterScale) override;

 private:
  Effect* effect = nullptr;
  int strengthHandle = -1;
  int effectOpacityHandle = -1;
  int uniformHandle = -1;
  int timeHandle = -1;
};
}  // namespace pag
