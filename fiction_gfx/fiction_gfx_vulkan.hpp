// Fiction Graphics — Vulkan Backend (Desktop)
//
// Thin wrapper that includes the existing Vulkan implementation headers.
// fiction_text.hpp already includes fiction_engine.hpp, so including just
// fiction_text.hpp brings in both namespaces:
//   - fiction_engine (from fiction_engine.hpp)
//   - fiction         (from fiction_text.hpp)

#pragma once

// The existing headers live in vulkan/ relative to the project root.
// The project root is already on the include path (-I.), so we can
// include them directly.
#include "vulkan/fiction_text.hpp"
