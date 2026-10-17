#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace autosave {
// Shown as the row's label like the manual saves' file names, so loading needs no other name.
// Other Mods may read the file named by a row's label (better_save_name shows the part after "S4-").
constexpr const char* autoSaveFileName = "S4-AUTOSAVE";
constexpr uint32_t manualSaveSlotCount = 5;
constexpr uint32_t autoSaveRow = manualSaveSlotCount;  // The row after the five manual slots
constexpr uint32_t rowCountWithAutoSave = manualSaveSlotCount + 1;

// One row of the save list, as 0x53f380 fills it (0x94 bytes each).
#pragma pack(push, 1)
struct SaveSlotRecord {
    // File name of an existing save, which is both shown and used to load (0x53eac8),
    // or a text in the game's ANSI code page.
    char label[0x40];
    uint8_t wrongSize;  // Drawn dimmed (0x53fd25)
    uint8_t hasDetails;  // Shows the date, description and difficulty below
    SYSTEMTIME savedAt;  // Local time of the last write
    char description[0x40];  // Copied from the file at descriptionOffset
    uint8_t difficulty;  // Copied from the file's first byte
    uint8_t padding;
};
#pragma pack(pop)
static_assert(sizeof(SaveSlotRecord) == 0x94);
static_assert(offsetof(SaveSlotRecord, wrongSize) == 0x40 && offsetof(SaveSlotRecord, savedAt) == 0x42 &&
    offsetof(SaveSlotRecord, description) == 0x52 && offsetof(SaveSlotRecord, difficulty) == 0x92);

// Texts shown in a row without a usable file; a usable file shows its file name.
struct SlotLabels {
    const wchar_t* missing;
    const wchar_t* wrongSize;
};

// Fills record the way 0x53f380 fills a manual slot, for directory + fileName.
// Returns true if the file is a usable save.
bool fillSaveSlotRecord(SaveSlotRecord& record, const char* directory, const char* fileName, const SlotLabels& labels);

// True if directory + autoSaveFileName exists and was written after every manual save,
// which is the row 0x53f380 would put the cursor on had it known the file.
bool autoSaveIsNewest(const char* directory);
}
