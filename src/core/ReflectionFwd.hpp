#pragma once

// Light forward declarations for reflection, so behaviour/node *headers* can
// declare `static void describe(ne::reflect::TypeBuilder<T>&)` without pulling in
// the full reflection machinery (and nlohmann/json). The .cpp that defines
// describe() includes "core/Reflection.hpp".

namespace ne::reflect {
template <typename T>
class TypeBuilder;
}
