// Executes only the copied restore/serialize routines. Manager side effects and
// the CRT memcpy import are replaced by local stubs; no game startup or I/O runs.
#include "mapped_exe.h"
#include "retry_save.h"
#include "save_file_writer.h"

namespace {
using namespace mapped_exe;

void* __cdecl copyBytes(void* destination, const void* source, size_t size) {
    return memcpy(destination, source, size);
}

void replaceWithJump(unsigned address, const void* target) {
    auto* instruction = reinterpret_cast<unsigned char*>(va(address));
    instruction[0] = 0xe9;
    *reinterpret_cast<uint32_t*>(instruction+1) = reinterpret_cast<uintptr_t>(target)-va(address)-5;
}

void nativeSerialize(uint8_t* destination, const uint8_t* parameters) {
    const uintptr_t function = va(0x4d9ed0);
    __asm {
        push parameters
        mov ebx, destination
        call function
    }
}

uint32_t randomState = 0x29d0c745;
uint8_t randomByte() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<uint8_t>(randomState);
}
}

int main(int argc, char** argv) {
    using namespace mapped_exe;
    if (argc != 2 || !mapImage(argv[1])) return 2;
    // The reset helper only writes transient fields outside the saved payload.
    *reinterpret_cast<uint8_t*>(va(0x4d8220)) = 0xc3;
    const uint8_t returnOneStackWord[] = {0xc2, 4, 0};
    memcpy(reinterpret_cast<void*>(va(0x406b10)), returnOneStackWord, sizeof(returnOneStackWord));
    for (unsigned global : {0x9ebb1cU, 0x9e96c8U, 0x9e964cU, 0x9e95ccU})
        *reinterpret_cast<uintptr_t*>(va(global)) = 0;
    replaceWithJump(0x8d4b00, reinterpret_cast<const void*>(copyBytes));
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(imageBase), 0x65b000);
    using Restore = void(__stdcall*)(uint8_t*, const uint8_t*, uint32_t);
    const auto restore = reinterpret_cast<Restore>(va(0x4d9f50));

    for (unsigned pattern = 0; pattern < 32; ++pattern) {
        std::vector<uint8_t> live(0x49b3c), backup(autosave::saveFileSize);
        for (auto& byte : live) byte = randomByte();
        for (auto& byte : backup) byte = randomByte();
        const uint32_t format = 4;
        memcpy(backup.data()+0x48b34, &format, sizeof(format));
        const auto liveBefore = live, backupBefore = backup;
        auto retried = live;
        restore(retried.data(), backup.data(), 0);
        // Native retry caller's exact retained ranges, independent of the Mod's table.
        retried[0x41008] = live[0x41008];
        retried[0x41009] = live[0x41009];
        memcpy(retried.data()+0x4222c, live.data()+0x4222c, 0x1104);
        memcpy(retried.data()+0x43468, live.data()+0x43468, 0xc80);
        memcpy(retried.data()+0x440e8, live.data()+0x440e8, 4);
        memcpy(retried.data()+0x440ec, live.data()+0x440ec, 4);
        std::vector<uint8_t> expected(autosave::saveFileSize, 0), actual(autosave::saveFileSize, 0xcc);
        nativeSerialize(expected.data(), retried.data());
        autosave::composeRetrySave(actual.data(), backup.data(), live.data());
        check("All file bytes match copied native retry restoration and serialization", actual == expected);
        check("Source state and retry buffer remain unchanged", live == liveBefore && backup == backupBefore);
    }
    printf("RESULT: %u checks passed, %u failed\n", tests-failures, failures);
    puts("Copied restore/serialize ran with transient/manager/CRT stubs; whole retry gameplay was not executed.");
    puts("No writes to the installation, saves or game process.");
    return failures ? 1 : 0;
}
