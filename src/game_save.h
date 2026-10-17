#pragma once
#include <cstddef>
#include <cstdint>

namespace autosave::game {
// Game thread only. Copies a valid retry result without changing any game state.
bool captureRetrySave(uint8_t* buffer);
bool copySaveDirectory(char* directory, size_t capacity);
}
