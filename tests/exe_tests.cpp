// Initializes mod.dll against a mapped copy of the decrypted executable and checks what it
// reserves and rewrites. The retry hook is also exercised with local native stubs;
// no game startup, manager logic or game save directory is used.
#include "mapped_exe.h"
#include <algorithm>
#include <filesystem>

namespace {
using namespace mapped_exe;
unsigned claimCalls = 0, conflictAt = 0, pages = 0, saveCallbacks = 0;
wots4::OptionsPageCallbacks options{};
BOOL WINAPI claim(HMODULE owner, uintptr_t address, uint32_t length) {
    ++claimCalls;
    if (conflictAt && claimCalls == conflictAt) return FALSE;
    return hostClaim(owner, address, length);
}
BOOL WINAPI addPage(HMODULE, const wots4::LocalizedText* title, const wots4::OptionItem*, uint32_t count,
    const wots4::OptionsPageCallbacks* callbacks) {
    options = *callbacks;
    ++pages;
    check("The options page has both titles and two items",
        !wcscmp(title->japanese, L"オートセーブ") && !wcscmp(title->english, L"Autosave") && count == 2);
    return TRUE;
}
BOOL WINAPI onFrame(HMODULE, wots4::GameEventCallback, void*) { return TRUE; }
BOOL WINAPI onSave(HMODULE, wots4::GameEventCallback, void*) { ++saveCallbacks; return TRUE; }

unsigned readU32(unsigned preferred) { return *reinterpret_cast<unsigned*>(va(preferred)); }
unsigned char readU8(unsigned preferred) { return *reinterpret_cast<unsigned char*>(va(preferred)); }
bool insideModule(HMODULE module, unsigned address) {
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) && info.AllocationBase == module;
}

// Native values of everything the Mod may change.
struct NativeState {
    unsigned rowsAllocation, drawTop, regionTop, mapLoadSlot, actionSlot, titleSlot;
    unsigned char drawStep, regionStep, refreshFirst, regionsFirst, openFileFirst, backupFirst;
};
NativeState captureState() {
    return {readU32(0x53ec92), readU32(0x53f9f2), readU32(0x53ff9e), readU32(0x98c548), readU32(0x98799c), readU32(0x987bc0),
        readU8(0x53fa14), readU8(0x53ffef), readU8(0x53f380), readU8(0x53ff60), readU8(0x53e7e0), readU8(0x53e3e0)};
}
// Field by field: the struct has padding that memcmp would compare too.
bool sameState(const NativeState& a, const NativeState& b) {
    return a.rowsAllocation == b.rowsAllocation && a.drawTop == b.drawTop && a.regionTop == b.regionTop &&
        a.mapLoadSlot == b.mapLoadSlot && a.actionSlot == b.actionSlot && a.titleSlot == b.titleSlot &&
        a.drawStep == b.drawStep && a.regionStep == b.regionStep && a.refreshFirst == b.refreshFirst &&
        a.regionsFirst == b.regionsFirst && a.openFileFirst == b.openFileFirst && a.backupFirst == b.backupFirst;
}

unsigned backupCalls = 0;
void testRetryHook(const std::filesystem::path& fixture) {
    std::vector<uint8_t> live(0x49b3c, 0x51), backup(0x48bb0, 0x27);
    const uint32_t format = 4;
    memcpy(backup.data()+0x48b34, &format, sizeof(format));
    *reinterpret_cast<void**>(va(0x9e967c)) = live.data();
    *reinterpret_cast<void**>(va(0x9ecfa8)) = backup.data();
    const auto originalLive = live, originalBackup = backup;
    const std::string folder = fixture.string()+"\\";
    // The trampoline runs the native six-byte prologue, then this local stub.
    uint8_t backupStub[] = {0xff,0x05,0,0,0,0, 0xc6,0x05,0,0,0,0,1, 0x8b,0xe5,0x5d,0xc3};
    *reinterpret_cast<uintptr_t*>(backupStub+2) = reinterpret_cast<uintptr_t>(&backupCalls);
    *reinterpret_cast<uintptr_t*>(backupStub+8) = va(0x9ecf43);
    memcpy(reinterpret_cast<void*>(va(0x53e3e6)), backupStub, sizeof(backupStub));
    uint8_t directoryStub[] = {0xb8,0,0,0,0,0xc3};
    *reinterpret_cast<uintptr_t*>(directoryStub+1) = reinterpret_cast<uintptr_t>(folder.c_str());
    memcpy(reinterpret_cast<void*>(va(0x4bddc0)), directoryStub, sizeof(directoryStub));
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(imageBase), 0x65b000);
    const auto prepare = reinterpret_cast<void(__cdecl*)()>(va(0x53e3e0));
    const auto target = fixture/"S4-AUTOSAVE";
    options.changed(options.context,0,0);
    prepare();
    check("Disabling Autosave still calls the original backup preparation", backupCalls==1);
    check("Disabling Autosave does not write a file", !std::filesystem::exists(target));
    options.changed(options.context,0,1);
    *reinterpret_cast<uint8_t*>(va(0x53e3e6)+12) = 0;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(va(0x53e3e6)), sizeof(backupStub));
    prepare();
    check("An unavailable retry checkpoint does not write a file", backupCalls==2 && !std::filesystem::exists(target));
    *reinterpret_cast<uint8_t*>(va(0x53e3e6)+12) = 1;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(va(0x53e3e6)), sizeof(backupStub));
    *reinterpret_cast<void**>(va(0x9ecfa8)) = nullptr;
    prepare();
    check("A missing retry buffer does not write a file", backupCalls==3 && !std::filesystem::exists(target));
    *reinterpret_cast<void**>(va(0x9ecfa8)) = backup.data();
    prepare();
    check("The enabled hook calls the original once", backupCalls==4);
    check("Capture does not change live parameters or the retry buffer", live==originalLive && backup==originalBackup);
    // Mutating the game after capture must not change the background writer's image.
    std::fill(live.begin(),live.end(),0xee);
    std::fill(backup.begin(),backup.end(),0xdd);
    std::vector<uint8_t> actual;
    for (unsigned attempt=0; attempt<500; ++attempt) {
        if (std::filesystem::exists(target)) {
            std::ifstream file(target,std::ios::binary);
            actual.assign(std::istreambuf_iterator<char>(file),{});
            if (actual.size()==0x48bb0) break;
        }
        Sleep(10);
    }
    check("The real Mod hook writes a full file in the fixture", actual.size()==0x48bb0);
    if (actual.size()==0x48bb0) {
        check("The captured ordinary payload comes from the backup", actual[0x20]==originalBackup[0x20]);
        check("The captured settings come from current parameters", !memcmp(actual.data()+0x48b78, originalLive.data(),22));
        check("The captured retained fields survive later live mutations",
            !memcmp(actual.data()+0x42214, originalLive.data()+0x4222c,0x1104) &&
            !memcmp(actual.data()+0x43450, originalLive.data()+0x43468,0xc88));
        check("The hook never rewrites the game's retry flag", readU8(0x9ecf43)==1);
    }
    check("Atomic writing leaves no temporary file", !std::filesystem::exists(fixture/"S4-AUTOSAVE.tmp"));
    *reinterpret_cast<void**>(va(0x9e967c)) = nullptr;
    *reinterpret_cast<void**>(va(0x9ecfa8)) = nullptr;
}
}

int main(int argc, char** argv) {
    if (argc < 3 || !mapImage(argv[1])) return 2;
    const std::string scenario = argc > 3 ? argv[3] : "valid";
    // Claim order: the seven save list ranges, then the two update slots and the retry checkpoint.
    conflictAt = scenario == "conflict" ? 5 : scenario == "slot_conflict" ? 8 : scenario == "checkpoint_conflict" ? 10 : 0;
    if (scenario == "unsupported") *reinterpret_cast<unsigned char*>(va(0x53ffcd + 1)) = 0x60;
    unsigned char saveManager[0x10]{}, saveList[0x10]{};
    if (scenario == "list_made") {
        *reinterpret_cast<void**>(saveManager + 8) = saveList;
        *reinterpret_cast<void**>(va(0x9e96d4)) = saveManager;
    }
    const NativeState native = captureState();

    const auto fixture = std::filesystem::absolute(argv[0]).parent_path() / ("autosave_exe_" + scenario);
    std::filesystem::create_directories(fixture);
    const auto directory = fixture.wstring() + L"\\";
    const auto module = LoadLibraryA(argv[2]);
    const auto initialize = module ? reinterpret_cast<wots4::Initialize>(GetProcAddress(module, "Wots4ModInitialize")) : nullptr;
    check("The x86 DLL exports Wots4ModInitialize", initialize != nullptr);
    if (!initialize) return 3;
    wots4::HostApi host{sizeof(host), wots4::modApiVersion, hostLog, claim, hostRelease, addPage, onFrame, onSave};
    wots4::ModContext context{sizeof(context), wots4::modApiVersion, nullptr, module,
        reinterpret_cast<HMODULE>(imageBase), directory.c_str(), &host};
    check("A null context is refused", initialize(nullptr) == FALSE);
    auto otherVersion = context;
    otherVersion.version = 99;
    check("An unknown API version is refused", initialize(&otherVersion) == FALSE);

    const BOOL accepted = initialize(&context);
    if (scenario == "valid" || scenario == "list_made") {
        check("Initialization succeeds", accepted == TRUE);
        check("The options page and the options-saved callback are registered", pages == 1 && saveCallbacks == 1);
        check("Both part update slots are reserved", claimed(0x98799c, 4) && claimed(0x987bc0, 4));
        check("The update slots point into mod.dll", insideModule(module, readU32(0x98799c)) &&
            insideModule(module, readU32(0x987bc0)));
        check("LoadWaitMap's slot is left alone", readU32(0x98c548) == native.mapLoadSlot);
        check("Retry checkpoint preparation is reserved and hooked", claimed(0x53e3e0, 6) && readU8(0x53e3e0) == 0xe9);
    }
    if (scenario == "valid") {
        check("The save list code is reserved", claimed(0x53f380, 6) && claimed(0x53ff60, 6) && claimed(0x53ec91, 5) &&
            claimed(0x53f9ef, 7) && claimed(0x53fa11, 4) && claimed(0x53ff9b, 7) && claimed(0x53ffec, 4));
        check("The list allocates six rows of 0x94 bytes", readU32(0x53ec92) == 6 * 0x94);
        check("The row layout stays native until a list opens", readU32(0x53f9f2) == 0x7c && readU8(0x53fa14) == 0x6a &&
            readU32(0x53ff9e) == 0x7c && readU8(0x53ffef) == 0x6a);
        check("The two list functions are hooked", readU8(0x53f380) == 0xe9 && readU8(0x53ff60) == 0xe9);
        check("Loading opens the row's file name without a hook", readU8(0x53e7e0) == native.openFileFirst &&
            std::none_of(claims.begin(), claims.end(), [](const Claim& c) { return c.address == va(0x53e7e0); }));
        check("Untouched list code keeps five native rows", readU32(0x53ec10) == 5 && readU32(0x53ffa8) == 5);
    } else if (scenario == "list_made") {
        check("A list made before the Mod keeps its code and layout", claims.size() == 3 &&
            readU32(0x53ec92) == native.rowsAllocation && readU32(0x53f9f2) == native.drawTop &&
            readU8(0x53f380) == native.refreshFirst && readU8(0x53ff60) == native.regionsFirst && readU8(0x53e7e0) == native.openFileFirst);
        check("The Mod logs that the Load list will not show the auto-save", loggedText("will not show S4-AUTOSAVE") &&
            loggedText("the Load list keeps five rows"));
    } else {
        check("Initialization is refused", accepted == FALSE);
        check("No reservation is left", claims.empty());
        check("Every byte and slot is restored", sameState(captureState(), native));
        if (scenario == "unsupported") check("Nothing is claimed for an unknown executable", claimCalls == 0);
        else check("The partial installation released its claims", releases >= 1);
    }
    if (scenario == "valid") testRetryHook(fixture);
    printf("RESULT: %u checks passed, %u failed\n", tests - failures, failures);
    puts("Only a mapped executable copy and fixture stubs were used; no game was started.");
    puts("No writes to the installation, saves or game process.");
    std::filesystem::remove_all(fixture);
    return failures ? 1 : 0;
}
