#include "save_slot_record.h"
#include "save_file_writer.h"
#include <cstdio>
#include <cstring>

namespace autosave {
namespace {
constexpr DWORD descriptionOffset = 0xdc;  // Read by 0x53f8f0
constexpr const char* manualSaveNameFormat = "%sS4-DATA_%02u";  // Directory, slot (0x98a704 with 0x98a70c)

// Labels are converted like the game does (setlocale "" + wcstombs), i.e. to the ANSI code page.
void setLabel(SaveSlotRecord& record, const wchar_t* text) {
    record.label[0] = '\0';
    if (!text) return;
    const int length = WideCharToMultiByte(CP_ACP, 0, text, -1, record.label, sizeof(record.label), nullptr, nullptr);
    if (!length) record.label[sizeof(record.label) - 1] = '\0';
}

bool readAt(HANDLE file, DWORD offset, void* destination, DWORD size) {
    DWORD read = 0;
    return SetFilePointer(file, offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(file, destination, size, &read, nullptr) && read == size;
}

// Same fields and order as 0x53f8f0.
void readDetails(SaveSlotRecord& record, HANDLE file) {
    FILETIME written, local;
    if (!GetFileTime(file, nullptr, nullptr, &written)) return;
    FileTimeToLocalFileTime(&written, &local);
    FileTimeToSystemTime(&local, &record.savedAt);
    if (!readAt(file, 0, &record.difficulty, sizeof(record.difficulty))) return;
    readAt(file, descriptionOffset, record.description, sizeof(record.description));
}

bool lastWriteTime(const char* path, ULARGE_INTEGER& time) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attributes)) return false;
    time.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    time.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    return true;
}
}

bool fillSaveSlotRecord(SaveSlotRecord& record, const char* directory, const char* fileName, const SlotLabels& labels) {
    memset(&record, 0, sizeof(record));
    char path[MAX_PATH];
    if (sprintf_s(path, "%s%s", directory, fileName) < 0) {
        setLabel(record, labels.missing);
        return false;
    }
    const HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        setLabel(record, labels.missing);
        return false;
    }
    const bool usable = GetFileSize(file, nullptr) == saveFileSize;
    if (usable) {
        strcpy_s(record.label, fileName);
        record.hasDetails = 1;
        readDetails(record, file);
    } else {
        // The game also flags these rows as having details but reads none; leave them empty.
        setLabel(record, labels.wrongSize);
        record.wrongSize = 1;
    }
    CloseHandle(file);
    return usable;
}

bool autoSaveIsNewest(const char* directory) {
    char path[MAX_PATH];
    ULARGE_INTEGER autoSaveTime;
    if (sprintf_s(path, "%s%s", directory, autoSaveFileName) < 0 || !lastWriteTime(path, autoSaveTime)) return false;
    for (uint32_t slot = 0; slot < manualSaveSlotCount; ++slot) {
        ULARGE_INTEGER manualSaveTime;
        if (sprintf_s(path, manualSaveNameFormat, directory, slot) < 0) return false;
        if (lastWriteTime(path, manualSaveTime) && manualSaveTime.QuadPart >= autoSaveTime.QuadPart) return false;
    }
    return true;
}
}
