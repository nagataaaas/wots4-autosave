#include "retry_save.h"
#include "save_file_writer.h"
#include <cstring>

namespace autosave {
namespace {
constexpr uint32_t parameterPayloadOffset = 0x18;
constexpr uint32_t saveHeaderOffset = 0x48b78;
constexpr uint32_t saveHeaderSize = 22;
constexpr uint32_t writtenSaveSize = 0x48bae;
struct PreservedRange { uint32_t parameterOffset, size; };
// The retry branch retains these current fields around its backup restore.
constexpr PreservedRange preservedRanges[] = {
    {0x41008, 2},      // Two bytes retained by the game's retry path.
    {0x4222c, 0x1104}, // Revenge state and character.
    {0x43468, 0xc80},  // Forty encounter history entries.
    {0x440e8, 8},     // Encounter count and the adjacent retained counter.
};
static_assert(saveHeaderOffset + saveHeaderSize <= writtenSaveSize && writtenSaveSize <= saveFileSize);
}

void composeRetrySave(uint8_t* destination, const uint8_t* retryBackup, const uint8_t* gameParameters) {
    // The backup already includes serializer hooks (for example custom difficulty).
    // Reusing it avoids restoring or synchronizing the live game to build a save.
    memcpy(destination, retryBackup, writtenSaveSize);
    for (const auto& range : preservedRanges)
        memcpy(destination + range.parameterOffset - parameterPayloadOffset,
            gameParameters + range.parameterOffset, range.size);
    // Retry restores with header=false: current settings survive the rewind.
    memcpy(destination + saveHeaderOffset, gameParameters, saveHeaderSize);
    memset(destination + writtenSaveSize, 0, saveFileSize - writtenSaveSize);
}
}
