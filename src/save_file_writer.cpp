#include "save_file_writer.h"
#include <cstdio>
#include <cstring>

namespace autosave {
DWORD writeFileAtomically(const char* path, const void* data, uint32_t size) {
    char temporaryPath[MAX_PATH];
    if (sprintf_s(temporaryPath, "%s.tmp", path) < 0) return ERROR_FILENAME_EXCED_RANGE;
    const HANDLE file = CreateFileA(temporaryPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD written = 0;
    const bool complete = WriteFile(file, data, size, &written, nullptr) && written == size && FlushFileBuffers(file);
    const DWORD writeError = complete ? 0 : (GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
    CloseHandle(file);
    if (!complete) {
        DeleteFileA(temporaryPath);
        return writeError;
    }
    if (MoveFileExA(temporaryPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
    const DWORD moveError = GetLastError();
    DeleteFileA(temporaryPath);
    return moveError;
}

bool SaveFileWriter::start() {
    if (requested) return true;
    requested = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!requested) return false;
    // Never joined: the process exit ends the thread, and the rename keeps the last
    // complete file even if that happens mid-write.
    const HANDLE thread = CreateThread(nullptr, 0, run, this, 0, nullptr);
    if (!thread) {
        CloseHandle(requested);
        requested = nullptr;
        return false;
    }
    CloseHandle(thread);
    return true;
}

bool SaveFileWriter::write(const char* path) {
    if (!requested || !idle() || strcpy_s(targetPath, path)) return false;
    busy.store(true, std::memory_order_release);
    if (SetEvent(requested)) return true;
    busy.store(false, std::memory_order_release);
    return false;
}

bool SaveFileWriter::takeResult(Result& result) {
    if (!finished.exchange(false, std::memory_order_acquire)) return false;
    result.error = lastError;
    strcpy_s(result.path, targetPath);
    return true;
}

DWORD WINAPI SaveFileWriter::run(void* context) {
    auto& writer = *static_cast<SaveFileWriter*>(context);
    for (;;) {
        if (WaitForSingleObject(writer.requested, INFINITE) != WAIT_OBJECT_0) return 1;
        writer.writePending();
    }
}

void SaveFileWriter::writePending() {
    lastError = writeFileAtomically(targetPath, data, saveFileSize);
    finished.store(true, std::memory_order_release);
    busy.store(false, std::memory_order_release);
}
}
