#pragma once

#include "LayerFilter.h"

namespace pag {
class WhiteNoiseFilter : public LayerFilter {
 public:
  explicit WhiteNoiseFilter(Effect* effect);
  ~WhiteNoiseFilter() override = default;

 protected:
  std::string onBuildFragmentShader() override;

  void onPrepareProgram(tgfx::Context* context, unsigned program) override;

  void onUpdateParams(tgfx::Context* context, const tgfx::Rect& contentBounds,
                      const tgfx::Point& filterScale) override;

 private:
  Effect* effect = nullptr;
  int intensityHandle = -1;
  int effectOpacityHandle = -1;
  int timeHandle = -1;
};
}  // namespace pag

