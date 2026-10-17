#include "game_hooks.h"
#include "autosave_schedule.h"
#include "game_code.h"
#include "game_save.h"
#include "save_file_writer.h"
#include "save_slot_record.h"
#include <MinHook.h>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace autosave {
namespace {
using namespace game;

Settings* settings = nullptr;
LogFunction log = nullptr;
AutosaveSchedule schedule;
SaveFileWriter writer;
alignas(16) uint8_t pendingRetrySave[saveFileSize] = {};
bool reportedFirstSave = false;
DWORD lastReportedError = 0;

void logMessage(const char* message) { if (log) log(message); }

// ---- Autosave when the retry checkpoint changes --------------------------------------

void reportFinishedWrite() {
    SaveFileWriter::Result result;
    if (!writer.takeResult(result)) return;
    char message[MAX_PATH + 160];
    if (!result.error) {
        lastReportedError = 0;
        if (reportedFirstSave) return;
        reportedFirstSave = true;
        sprintf_s(message, "[INFO] Autosave wrote %s; later autosaves are logged only when they fail.", result.path);
        logMessage(message);
        return;
    }
    // A full disk or a locked file repeats at later checkpoints; report each new cause once.
    if (result.error == lastReportedError) return;
    lastReportedError = result.error;
    sprintf_s(message, "[ERROR] Autosave could not write %s (Windows error %lu); the previous auto-save file is kept "
        "and the next retry checkpoint tries again.", result.path, result.error);
    logMessage(message);
}

void startAutosave() {
    char directory[MAX_PATH], path[MAX_PATH];
    if (!copySaveDirectory(directory, sizeof(directory)) || sprintf_s(path, "%s%s", directory, autoSaveFileName) < 0) {
        logMessage("[ERROR] Autosave skipped: the game's save folder path is missing or too long.");
        schedule.cancel();
        return;
    }
    memcpy(writer.buffer(), pendingRetrySave, saveFileSize);
    if (!writer.write(path)) {
        logMessage("[ERROR] Autosave skipped: the background writer did not accept the save.");
        schedule.cancel();
        return;
    }
    schedule.saved(GetTickCount64());
}

void pumpAutosave() {
    reportFinishedWrite();
    if (!schedule.due()) return;
    if (!settings->enabled()) {
        schedule.cancel();
        return;
    }
    if (!writer.idle()) return;
    startAutosave();
}

using PrepareRetryBackup = void(__cdecl*)();
PrepareRetryBackup originalPrepareRetryBackup = nullptr;

void __cdecl prepareRetryBackupHook() {
    originalPrepareRetryBackup();
    if (!settings->enabled()) {
        schedule.cancel();
        return;
    }
    if (!schedule.backupUpdated(GetTickCount64(), settings->minimumIntervalMilliseconds())) return;
    // Capture on this game-thread edge. A busy writer must never cause later live
    // changes to leak into the checkpoint while its disk write is deferred.
    if (!captureRetrySave(pendingRetrySave)) {
        schedule.cancel();
        return;
    }
    pumpAutosave();
}

using PartUpdate = int(__thiscall*)(void* part);
PartUpdate originalActionUpdate = nullptr;
PartUpdate originalTitleUpdate = nullptr;

// __fastcall receives a thiscall's ecx; edx is unused.
int __fastcall actionUpdateHook(void* part, void*) {
    const int nextPart = originalActionUpdate(part);
    // The pending image is already immutable; no game-state synchronization occurs here.
    if (nextPart == stayInPart) pumpAutosave();
    return nextPart;
}

int __fastcall titleUpdateHook(void* part, void*) {
    schedule.returnedToTitle();
    return originalTitleUpdate(part);
}

// ---- The auto-save row of the Load list -------------------------------------------------

void* originalRefreshSaveList = nullptr;
void* originalRegisterSaveListRegions = nullptr;

uint32_t& listField(uintptr_t list, uint32_t offset) { return *reinterpret_cast<uint32_t*>(list + offset); }

// The game's labels for a slot without a usable file, read like 0x53f56c (index 0 if out of range).
const wchar_t* uiText(uint32_t number) {
    const uintptr_t texts = read<uintptr_t>(textManager);
    if (!texts) return nullptr;
    const auto* table = *reinterpret_cast<const uint32_t* const*>(texts + uiTextTableOffset);
    if (!table) return nullptr;
    const auto* strings = *reinterpret_cast<const wchar_t* const* const*>(table + 1);
    if (!strings) return nullptr;
    return strings[*table > number ? number : 0];
}

bool hasNonAscii(const wchar_t* text) {
    for (; text && *text; ++text)
        if (*text > 0x7f) return true;
    return false;
}

// The game's own empty slot text ("< Empty Slot >" or its Japanese) tells the language.
// The missing label is enclosed in < > like that text: loading opens the row's label as a file
// (0x53e7e0, OPEN_ALWAYS), and those characters make Windows refuse the name instead of
// creating an empty file.
SlotLabels autoSaveLabels() {
    if (hasNonAscii(uiText(noDataText))) return {L"< オートセーブがありません >", uiText(brokenDataText)};
    return {L"< No Autosave >", uiText(brokenDataText)};
}

bool writeMemory(uintptr_t address, const void* bytes, uint32_t length);

// The draw (0x53f9ef, 0x53fa11) and mouse region (0x53ff9b, 0x53ffec) code reads its row position
// from immediates, so the layout follows the list's mode by rewriting them when the list opens.
bool compactLayoutInUse = false;
void useRowLayout(bool compact) {
    if (compact == compactLayoutInUse) return;
    const int32_t top = compact ? compactRowTop : nativeRowTop;
    const int8_t step = static_cast<int8_t>(compact ? compactRowStep : nativeRowStep);
    constexpr uint32_t topOperand = 3, stepOperand = 3;  // After the opcode and ModRM/displacement bytes
    writeMemory(relocated(saveListDrawTop) + topOperand, &top, sizeof(top));
    writeMemory(relocated(saveListDrawStep) + stepOperand, &step, sizeof(step));
    writeMemory(relocated(saveListRegionTop) + topOperand, &top, sizeof(top));
    writeMemory(relocated(saveListRegionStep) + stepOperand, &step, sizeof(step));
    compactLayoutInUse = compact;
}

void __cdecl afterSaveListRefreshed(uintptr_t list, uint32_t mode) {
    if (mode != loadMode) {
        // Saving and deleting name the file from the row number, so they keep the five slots.
        listField(list, listRowCountOffset) = manualSaveSlotCount;
        useRowLayout(false);
        return;
    }
    char directory[MAX_PATH];
    auto* rows = reinterpret_cast<SaveSlotRecord*>(listField(list, listRowsOffset));
    if (!rows || !copySaveDirectory(directory, sizeof(directory))) return;
    // Loading opens the save folder + the row's label (0x53eac8), which is the file name for a usable file.
    fillSaveSlotRecord(rows[autoSaveRow], directory, autoSaveFileName, autoSaveLabels());
    listField(list, listRowCountOffset) = rowCountWithAutoSave;
    useRowLayout(true);
    if (autoSaveIsNewest(directory)) listField(list, listCursorOffset) = autoSaveRow;
}

__declspec(naked) void refreshSaveListHook() {
    __asm {
        push dword ptr [esp+4]  // Mode, for the original
        call dword ptr [originalRefreshSaveList]  // ebx = list; the original keeps ebx and pops its argument
        push dword ptr [esp+4]
        push ebx
        call afterSaveListRefreshed
        add esp, 8
        ret 4
    }
}

// The native regions write their slots to a five-entry table at +0x9bc followed by other
// fields, so the auto-save row moves the cursor itself (what 0x53f785 does for the others).
uint8_t __cdecl autoSaveRowMouseEvent(uint32_t, uint32_t event, uintptr_t list) {
    constexpr uint32_t pointerEntered = 0;
    if (event == pointerEntered && listField(list, listRowCountOffset) == rowCountWithAutoSave)
        listField(list, listCursorOffset) = autoSaveRow;
    return 1;
}

void __cdecl afterSaveListRegionsRegistered(uintptr_t list, uint32_t groupBefore) {
    const uint32_t group = listField(list, listRegionGroupOffset);
    if (groupBefore || !group || listField(list, listRowCountOffset) != rowCountWithAutoSave) return;
    const uintptr_t regions = read<uintptr_t>(mouseRegions);
    if (!regions) return;
    const uintptr_t allocate = relocated(allocateMouseRegion);
    const uintptr_t registerRegion = relocated(registerMouseRegion);
    int32_t slot;
    __asm {
        mov edx, regions
        call allocate
        mov slot, eax
    }
    if (slot < 0) return;
    // Locals, because inline assembly reads a named constant as a memory operand.
    const int32_t left = rowLeft, top = compactRowTop + static_cast<int32_t>(autoSaveRow) * compactRowStep;
    const int32_t width = rowWidth, height = rowHeight;
    const void* callback = autoSaveRowMouseEvent;
    __asm {
        push list
        push callback
        push height
        push width
        push top
        push left
        push group
        push regions
        mov ecx, slot
        call registerRegion
    }
    ++*reinterpret_cast<uint32_t*>(regions + mouseRegionCountOffset);
}

__declspec(naked) void registerSaveListRegionsHook() {
    __asm {
        push dword ptr [ebx + 0x9b4]  // listRegionGroupOffset before the original registers
        call dword ptr [originalRegisterSaveListRegions]  // ebx = list
        push eax
        push dword ptr [esp+4]
        push ebx
        call afterSaveListRegionsRegistered
        add esp, 8
        pop eax
        add esp, 4
        ret
    }
}

// ---- Verification -----------------------------------------------------------------------

struct CodeBytes {
    uintptr_t address;
    const uint8_t* bytes;
    uint32_t length;
};
const uint8_t refreshPrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0x80, 0x02, 0x00, 0x00};
const uint8_t regionsPrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c};
const uint8_t retryBackupPrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};
const uint8_t rowsAllocationBytes[] = {0x68, 0xe4, 0x02, 0x00, 0x00};
const uint8_t drawTopBytes[] = {0xc7, 0x45, 0xf8, 0x7c, 0x00, 0x00, 0x00};
const uint8_t drawStepBytes[] = {0x83, 0x45, 0xf8, 0x6a};
const uint8_t regionTopBytes[] = {0xc7, 0x45, 0xfc, 0x7c, 0x00, 0x00, 0x00};
const uint8_t regionStepBytes[] = {0x83, 0x45, 0xfc, 0x6a};
// Not changed, but the Mod's sixth row assumes them.
const uint8_t listRowCountBytes[] = {0xc7, 0x46, 0x08, 0x05, 0x00, 0x00, 0x00};  // 0x53ec0d: five rows
const uint8_t regionCountBytes[] = {0xc7, 0x45, 0xf4, 0x05, 0x00, 0x00, 0x00};  // 0x53ffa5: five regions
const uint8_t regionHeightBytes[] = {0x6a, 0x5e};  // 0x53ffcd
const CodeBytes expectedCode[] = {
    {prepareRetryBackup, retryBackupPrologue, sizeof(retryBackupPrologue)},
    {refreshSaveList, refreshPrologue, sizeof(refreshPrologue)},
    {registerSaveListRegions, regionsPrologue, sizeof(regionsPrologue)},
    {saveListRowsAllocation, rowsAllocationBytes, sizeof(rowsAllocationBytes)},
    {saveListDrawTop, drawTopBytes, sizeof(drawTopBytes)},
    {saveListDrawStep, drawStepBytes, sizeof(drawStepBytes)},
    {saveListRegionTop, regionTopBytes, sizeof(regionTopBytes)},
    {saveListRegionStep, regionStepBytes, sizeof(regionStepBytes)},
    {0x53ec0d, listRowCountBytes, sizeof(listRowCountBytes)},
    {0x53ffa5, regionCountBytes, sizeof(regionCountBytes)},
    {0x53ffcd, regionHeightBytes, sizeof(regionHeightBytes)},
};
// The game's own calls identify the functions the Mod calls, wherever other Mods hook them.
struct CallSite {
    uintptr_t call;
    uintptr_t target;
};
const CallSite expectedCalls[] = {
    {0x53e3f2, syncPlayer}, {0x53e3f7, syncItems}, {0x53e3fc, syncWeapons}, {0x53e40b, syncSkillTables},
    {0x53e41a, syncStyles}, {0x53e426, writeSaveBuffer}, {0x53e6cd, findSaveDirectory}, {0x4dc4c9, findCharacter},
    {0x53ffb2, allocateMouseRegion}, {0x53ffde, registerMouseRegion},
};
struct VirtualSlot {
    uintptr_t slot;
    uintptr_t nativeFunction;
    void* hook;
    void** original;
};
const VirtualSlot virtualSlots[] = {
    {actionUpdateSlot, actionUpdate, reinterpret_cast<void*>(actionUpdateHook), reinterpret_cast<void**>(&originalActionUpdate)},
    {titleUpdateSlot, titleUpdate, reinterpret_cast<void*>(titleUpdateHook), reinterpret_cast<void**>(&originalTitleUpdate)},
};

bool sameBytes(uintptr_t address, const uint8_t* bytes, uint32_t length) {
    return !memcmp(reinterpret_cast<const void*>(relocated(address)), bytes, length);
}

bool callsTarget(const CallSite& site) {
    constexpr uint8_t callOpcode = 0xe8;
    constexpr uint32_t callLength = 5;
    const auto* instruction = reinterpret_cast<const uint8_t*>(relocated(site.call));
    const auto displacement = *reinterpret_cast<const int32_t*>(instruction + 1);
    return instruction[0] == callOpcode && site.call + callLength + displacement == site.target;
}

bool supportedImage() {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || pe->FileHeader.TimeDateStamp != supportedTimestamp)
        return false;
    const DWORD imageSize = pe->OptionalHeader.SizeOfImage;
    if (imageSize != supportedImageSize && imageSize != steamImageSize) return false;
    for (const auto& code : expectedCode)
        if (!sameBytes(code.address, code.bytes, code.length)) return false;
    for (const auto& site : expectedCalls)
        if (!callsTarget(site)) return false;
    for (const auto& slot : virtualSlots)
        if (read<uintptr_t>(slot.slot) != relocated(slot.nativeFunction)) return false;
    return true;
}

// ---- Installation -----------------------------------------------------------------------

struct Claim {
    uintptr_t address;
    uint32_t length;
};
// MinHook overwrites whole instructions: 6 bytes at both list functions.
constexpr uint32_t refreshHookLength = 6;
constexpr uint32_t regionsHookLength = 6;
constexpr uint32_t retryBackupHookLength = 6;
const Claim autosaveClaims[] = {{actionUpdateSlot, 4}, {titleUpdateSlot, 4}, {prepareRetryBackup, retryBackupHookLength}};
const Claim saveListClaims[] = {
    {refreshSaveList, refreshHookLength}, {registerSaveListRegions, regionsHookLength},
    {saveListRowsAllocation, sizeof(rowsAllocationBytes)}, {saveListDrawTop, sizeof(drawTopBytes)},
    {saveListDrawStep, sizeof(drawStepBytes)}, {saveListRegionTop, sizeof(regionTopBytes)},
    {saveListRegionStep, sizeof(regionStepBytes)},
};

// An immediate operand rewritten in place; value bytes start at offset.
struct Immediate {
    uintptr_t instruction;
    uint32_t offset;
    uint32_t size;
    uint32_t value;
};
// The row layout immediates are rewritten per opening instead (useRowLayout).
const Immediate saveListImmediates[] = {
    {saveListRowsAllocation, 1, 4, sizeof(SaveSlotRecord) * rowCountWithAutoSave},
};

bool writeMemory(uintptr_t address, const void* bytes, uint32_t length) {
    auto* target = reinterpret_cast<void*>(address);
    DWORD previous;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &previous)) return false;
    memcpy(target, bytes, length);
    DWORD unused;
    VirtualProtect(target, length, previous, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, length);
    return true;
}

bool claimAll(const wots4::ModContext& context, const Claim* claims, size_t count) {
    for (size_t index = 0; index < count; ++index)
        if (!context.host->claim(context.module, relocated(claims[index].address), claims[index].length)) return false;
    return true;
}

struct CodeHook {
    uintptr_t target;
    void* hook;
    void** original;
};
const CodeHook saveListHooks[] = {
    {refreshSaveList, reinterpret_cast<void*>(refreshSaveListHook), &originalRefreshSaveList},
    {registerSaveListRegions, reinterpret_cast<void*>(registerSaveListRegionsHook), &originalRegisterSaveListRegions},
};

void unhookSaveList() {
    for (const auto& hook : saveListHooks) MH_RemoveHook(reinterpret_cast<void*>(relocated(hook.target)));
}

bool hookSaveList() {
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    for (const auto& hook : saveListHooks) {
        auto* target = reinterpret_cast<void*>(relocated(hook.target));
        if (MH_CreateHook(target, hook.hook, hook.original) == MH_OK && MH_EnableHook(target) == MH_OK) continue;
        unhookSaveList();
        return false;
    }
    return true;
}

size_t patchImmediates() {
    size_t patched = 0;
    for (const auto& immediate : saveListImmediates) {
        if (!writeMemory(relocated(immediate.instruction) + immediate.offset, &immediate.value, immediate.size)) break;
        ++patched;
    }
    return patched;
}

void restoreImmediates(size_t count) {
    for (size_t index = 0; index < count; ++index) {
        const auto& immediate = saveListImmediates[index];
        for (const auto& code : expectedCode) {
            if (code.address != immediate.instruction) continue;
            writeMemory(relocated(immediate.instruction) + immediate.offset, code.bytes + immediate.offset, immediate.size);
        }
    }
}

size_t replaceVirtualSlots() {
    size_t replaced = 0;
    for (const auto& slot : virtualSlots) {
        *slot.original = reinterpret_cast<void*>(relocated(slot.nativeFunction));
        if (!writeMemory(relocated(slot.slot), &slot.hook, sizeof(slot.hook))) break;
        ++replaced;
    }
    return replaced;
}

void restoreVirtualSlots(size_t count) {
    for (size_t index = 0; index < count; ++index) {
        const uintptr_t native = relocated(virtualSlots[index].nativeFunction);
        writeMemory(relocated(virtualSlots[index].slot), &native, sizeof(native));
    }
}

// The rows are allocated when the list is made (0x53ec91), which normally happens after the
// Mods load. A list made before that has room for five rows only.
bool saveListAlreadyMade() {
    const uintptr_t manager = read<uintptr_t>(saveDataManager);
    return manager && *reinterpret_cast<const uintptr_t*>(manager + saveDataManagerListOffset);
}

bool installSaveList(const wots4::ModContext& context) {
    if (saveListAlreadyMade()) {
        logMessage("[WARN] The save list existed before Autosave loaded, with room for five rows only; "
            "autosaves are still written, but the Load list will not show S4-AUTOSAVE.");
        return true;
    }
    if (!claimAll(context, saveListClaims, std::size(saveListClaims))) {
        logMessage("[ERROR] Could not reserve the save list code: a range is owned by another Mod; initialization stopped.");
        return false;
    }
    if (!hookSaveList()) {
        logMessage("[ERROR] Could not hook the save list: MinHook failed; initialization stopped.");
        return false;
    }
    const size_t patched = patchImmediates();
    if (patched == std::size(saveListImmediates)) return true;
    restoreImmediates(patched);
    unhookSaveList();
    logMessage("[ERROR] Could not change the save list layout: memory protection failed; initialization stopped.");
    return false;
}

bool installAutosave(const wots4::ModContext& context) {
    if (!claimAll(context, autosaveClaims, std::size(autosaveClaims))) {
        logMessage("[ERROR] Could not reserve the update functions: a range is owned by another Mod; initialization stopped.");
        return false;
    }
    if (!writer.start()) {
        logMessage("[ERROR] Could not start the background save writer thread; initialization stopped.");
        return false;
    }
    const size_t replaced = replaceVirtualSlots();
    if (replaced == std::size(virtualSlots)) {
        const MH_STATUS initialized = MH_Initialize();
        auto* target = reinterpret_cast<void*>(relocated(prepareRetryBackup));
        if ((initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED) &&
            MH_CreateHook(target, reinterpret_cast<void*>(prepareRetryBackupHook),
                reinterpret_cast<void**>(&originalPrepareRetryBackup)) == MH_OK &&
            MH_EnableHook(target) == MH_OK) return true;
        MH_RemoveHook(target);
        restoreVirtualSlots(replaced);
        logMessage("[ERROR] Could not hook the retry checkpoint: MinHook failed; initialization stopped.");
        return false;
    }
    restoreVirtualSlots(replaced);
    logMessage("[ERROR] Could not redirect the update functions: memory protection failed; initialization stopped.");
    return false;
}
}

bool installGameHooks(const wots4::ModContext& context, Settings& modSettings, LogFunction logger, bool& loadRowAdded) {
    base = reinterpret_cast<uintptr_t>(context.game);
    settings = &modSettings;
    log = logger;
    loadRowAdded = false;
    if (!supportedImage()) {
        logMessage("[ERROR] Could not initialize Autosave: game version or code does not match; initialization stopped.");
        return false;
    }
    const bool saveListWasMade = saveListAlreadyMade();
    if (!installSaveList(context)) {
        context.host->releaseClaims(context.module);
        return false;
    }
    if (installAutosave(context)) {
        loadRowAdded = !saveListWasMade;
        return true;
    }
    if (!saveListWasMade) {
        restoreImmediates(std::size(saveListImmediates));
        unhookSaveList();
    }
    context.host->releaseClaims(context.module);
    return false;
}
}
