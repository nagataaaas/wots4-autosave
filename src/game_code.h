#pragma once
#include <windows.h>
#include <cstdint>

// Addresses of the supported Steam executable at its preferred base 0x400000.
namespace autosave::game {
constexpr uintptr_t preferredImageBase = 0x400000;
constexpr DWORD supportedTimestamp = 0x683d42eb;
constexpr DWORD supportedImageSize = 0x65b000;
// The Steam executable keeps its DRM wrapper section, so the running image is larger than the decrypted copy.
constexpr DWORD steamImageSize = 0x689000;

// Globals (pointers to the game's managers).
constexpr uintptr_t gameState = 0x9e967c;  // Everything a save holds (cpGParamMng)
constexpr uintptr_t retryBackupBuffer = 0x9ecfa8;  // Serialized retry checkpoint.
constexpr uintptr_t retryBackupAvailable = 0x9ecf43;  // Read by the retry option's availability check.
constexpr uintptr_t prepareRetryBackup = 0x53e3e0;  // No inputs; replaces the retry checkpoint.
constexpr uintptr_t saveDataManager = 0x9e96d4;  // SaveDataWin32Manager, owner of the save list
constexpr uintptr_t mouseRegions = 0x9ecf30;
constexpr uintptr_t textManager = 0x9e9680;

constexpr uint32_t uiTextTableOffset = 0x1c;  // GameTextUI: count, then the string array
constexpr uint32_t noDataText = 0x336;  // GameTextUI shown for an empty slot (0x53f56c)
constexpr uint32_t brokenDataText = 0x337;  // GameTextUI shown for a file of the wrong size (0x53f499)

constexpr uint32_t mouseRegionCountOffset = 0x448;  // Raised after each registration (0x53ffe3)
constexpr uint32_t saveDataManagerListOffset = 0x8;

// Native backup preparation calls, verified before installing its hook.
constexpr uintptr_t syncPlayer = 0x4dc4b0;  // eax = game state, push 0; player values into the state
constexpr uintptr_t syncItems = 0x4fb3d0;
constexpr uintptr_t syncWeapons = 0x697dd0;
constexpr uintptr_t syncSkillTables = 0x59cc70;  // ecx = skill table manager
constexpr uintptr_t syncStyles = 0x592f70;  // push style manager
constexpr uintptr_t writeSaveBuffer = 0x4d9ed0;  // ebx = buffer, push game state
// Game functions called by the Mod.
constexpr uintptr_t findSaveDirectory = 0x4bddc0;  // Returns the folder of the saves, ending with a separator
constexpr uintptr_t findCharacter = 0x465590;  // ecx = character manager, esi = character id
constexpr uintptr_t allocateMouseRegion = 0x4f4370;  // edx = mouse regions; returns a free slot or -1
constexpr uintptr_t registerMouseRegion = 0x4f43d0;  // ecx = slot, stdcall (regions, group, x, y, w, h, callback, user)

// Hooked code. The save list functions take the list in ebx.
constexpr uintptr_t refreshSaveList = 0x53f380;  // push mode; reads the five files into the rows
constexpr uintptr_t registerSaveListRegions = 0x53ff60;  // Registers one mouse region per row once per opening
// Virtual function slots replaced by the Mod.
constexpr uintptr_t actionUpdateSlot = 0x98799c;  // GamePartAction's update (0x4df7f0)
constexpr uintptr_t titleUpdateSlot = 0x987bc0;  // GamePartTitle's update (0x4eccb0)
constexpr uintptr_t actionUpdate = 0x4df7f0;
constexpr uintptr_t titleUpdate = 0x4eccb0;
constexpr int stayInPart = 7;  // A part update returns this unless the game switches parts

// Immediates rewritten by the Mod.
constexpr uintptr_t saveListRowsAllocation = 0x53ec91;  // push 0x2e4 (five rows of 0x94 bytes)
constexpr uintptr_t saveListDrawTop = 0x53f9ef;  // mov dword [ebp-8], 0x7c
constexpr uintptr_t saveListDrawStep = 0x53fa11;  // add dword [ebp-8], 0x6a
constexpr uintptr_t saveListRegionTop = 0x53ff9b;  // mov dword [ebp-4], 0x7c
constexpr uintptr_t saveListRegionStep = 0x53ffec;  // add dword [ebp-4], 0x6a

// Save list (0x9d4 bytes, made by 0x53ebe0).
constexpr uint32_t listCursorOffset = 0x4;
constexpr uint32_t listRowCountOffset = 0x8;
constexpr uint32_t listRowsOffset = 0xc;
constexpr uint32_t listRegionGroupOffset = 0x9b4;
constexpr uint32_t loadMode = 2;  // Opened from the title's Load (0x53e780); loads the row's file name
// Row geometry of the list, in the game's 2D coordinates (0x53ffd5).
constexpr int32_t rowLeft = 0x1ea;
constexpr int32_t rowWidth = 0x2f8;
constexpr int32_t rowHeight = 0x5e;
constexpr int32_t nativeRowTop = 0x7c;
constexpr int32_t nativeRowStep = 0x6a;
// The Load list's six rows: start higher and close the gaps so the last row ends at 692,
// within the 720-high 2D screen. The Save and Delete lists keep the native five-row layout.
constexpr int32_t compactRowTop = 0x6c;
constexpr int32_t compactRowStep = 0x62;
static_assert(compactRowTop + 5 * compactRowStep + rowHeight <= 720 && compactRowStep > rowHeight);

inline uintptr_t base;
inline uintptr_t relocated(uintptr_t address) { return base + address - preferredImageBase; }
template <typename T> T read(uintptr_t address) { return *reinterpret_cast<const T*>(relocated(address)); }
}
