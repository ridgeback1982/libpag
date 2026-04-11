#pragma once

#include "LayerFilter.h"

namespace pag {
class FrostedGlassFilter : public LayerFilter {
 public:
  explicit FrostedGlassFilter(Effect* effect);
  ~FrostedGlassFilter() override = default;

  bool initialize(tgfx::Context* context) override;

  void draw(tgfx::Context* context, const FilterSource* source,
            const FilterTarget* target) override;

 private:
  Effect* effect = nullptr;
};
}  // namespace pag
