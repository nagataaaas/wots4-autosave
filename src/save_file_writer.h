#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace autosave {
// Size of a save file; the game refuses to load any other size (0x53e874).
constexpr uint32_t saveFileSize = 0x48bb0;

// Replaces path with data through path + ".tmp", so a crash or power loss while
// writing leaves the previous file intact. Returns the Windows error, or 0.
DWORD writeFileAtomically(const char* path, const void* data, uint32_t size);

// Writes one save at a time on a background thread, keeping disk I/O off the game
// thread. The game thread fills buffer() while idle(), then calls write().
class SaveFileWriter {
public:
    struct Result {
        DWORD error;
        char path[MAX_PATH];
    };
    bool start();
    // Also false while a finished write is unreported, so takeResult never mixes two writes.
    bool idle() const { return !busy.load(std::memory_order_acquire) && !finished.load(std::memory_order_acquire); }
    uint8_t* buffer() { return data; }
    // path is copied. Only call while idle().
    bool write(const char* path);
    // True once per finished write; result is valid until the next write.
    bool takeResult(Result& result);
private:
    static DWORD WINAPI run(void* context);
    void writePending();
    alignas(16) uint8_t data[saveFileSize] = {};
    char targetPath[MAX_PATH] = {};
    DWORD lastError = 0;
    HANDLE requested = nullptr;
    std::atomic<bool> busy{false};
    std::atomic<bool> finished{false};
};
}
