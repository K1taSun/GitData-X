#pragma once

#include <string_view>

namespace gitdatax {

// string_view: zwracamy stały napis bez kopiowania i bez alokacji pamięci.
std::string_view version();

}  // namespace gitdatax

