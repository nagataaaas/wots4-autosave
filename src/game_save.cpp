#include "game_save.h"
#include "game_code.h"
#include "retry_save.h"
#include <cstring>

namespace autosave::game {
bool captureRetrySave(uint8_t* buffer) {
    if (!buffer || !read<uint8_t>(retryBackupAvailable)) return false;
    const auto* state = read<const uint8_t*>(gameState);
    const auto* backup = read<const uint8_t*>(retryBackupBuffer);
    if (!state || !backup) return false;
    composeRetrySave(buffer, backup, state);
    return true;
}

bool copySaveDirectory(char* directory, size_t capacity) {
    const uintptr_t function = relocated(findSaveDirectory);
    const char* folder;
    __asm {
        call function
        mov folder, eax
    }
    return folder && strcpy_s(directory, capacity, folder) == 0;
}
}
