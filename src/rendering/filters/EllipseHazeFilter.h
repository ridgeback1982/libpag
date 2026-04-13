#pragma once

#include "LayerFilter.h"

namespace pag {
class EllipseHazeFilter : public LayerFilter {
 public:
  explicit EllipseHazeFilter(Effect* effect);
  ~EllipseHazeFilter() override = default;

  bool initialize(tgfx::Context* context) override;

  void draw(tgfx::Context* context, const FilterSource* source,
            const FilterTarget* target) override;

 private:
  Effect* effect = nullptr;
};
}  // namespace pag
