#pragma once
#include <cstdint>

namespace autosave {
// Inputs must be separate from destination and remain untouched.
void composeRetrySave(uint8_t* destination, const uint8_t* retryBackup, const uint8_t* gameParameters);
}
