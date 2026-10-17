// Tests that need neither the game nor its executable: settings, the autosave schedule,
// the background writer and the auto-save row of the Load list. Files are made in a
// fixture folder next to the test; the installation and the real saves are not touched.
#include "autosave_schedule.h"
#include "retry_save.h"
#include "save_file_writer.h"
#include "save_slot_record.h"
#include "settings.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
void check(const char* label, bool success) {
    ++checks;
    if (!success) ++failures;
    std::printf("%s: %s\n", success ? "PASS" : "FAIL", label);
}
std::string lastLog;
void recordLog(const char* message) {
    lastLog = message;
    std::printf("  log: %s\n", message);
}
std::filesystem::path fixture;
std::string directoryText() { return fixture.string() + "\\"; }

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
std::vector<uint8_t> saveImage(uint8_t difficulty, const char* description) {
    std::vector<uint8_t> bytes(autosave::saveFileSize, 0);
    bytes[0] = difficulty;
    constexpr size_t descriptionOffset = 0xdc;
    memcpy(bytes.data() + descriptionOffset, description, strlen(description) + 1);
    return bytes;
}
// Sets the last write time to now + offsetSeconds, so tests do not depend on file system timing.
void setWriteTime(const std::filesystem::path& path, int offsetSeconds) {
    const HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER time{{now.dwLowDateTime, now.dwHighDateTime}};
    constexpr long long ticksPerSecond = 10'000'000;
    time.QuadPart += static_cast<long long>(offsetSeconds) * ticksPerSecond;
    const FILETIME written{time.LowPart, time.HighPart};
    SetFileTime(file, nullptr, nullptr, &written);
    CloseHandle(file);
}

void testSettings(const char* bundledConfig) {
    autosave::Settings settings;
    settings.initialize((fixture.wstring() + L"\\missing\\").c_str(), recordLog);
    check("Without config.ini autosave is on with no minimum interval",
        settings.enabled() && settings.minimumIntervalMilliseconds() == 0);

    const std::filesystem::path bundled(bundledConfig);
    settings.initialize((bundled.parent_path().wstring() + L"\\").c_str(), recordLog);
    check("The bundled config.ini matches the defaults", settings.enabled() && settings.minimumIntervalMilliseconds() == 0);

    const auto config = fixture / "config.ini";
    WritePrivateProfileStringW(L"autosave", L"enabled", L"0", config.c_str());
    WritePrivateProfileStringW(L"autosave", L"minimum_interval", L"5", config.c_str());
    settings.initialize((fixture.wstring() + L"\\").c_str(), recordLog);
    check("config.ini values are read", !settings.enabled() && settings.minimumIntervalMilliseconds() == 5 * 60 * 1000);

    lastLog.clear();
    WritePrivateProfileStringW(L"autosave", L"minimum_interval", L"7", config.c_str());
    settings.initialize((fixture.wstring() + L"\\").c_str(), recordLog);
    check("An unknown interval falls back to the default with a warning",
        settings.minimumIntervalMilliseconds() == 0 && lastLog.find("minimum_interval=7") != std::string::npos);

    check("Out-of-range option values are refused", !settings.change(autosave::minimumIntervalOption, 5) &&
        !settings.change(autosave::optionCount, 0));
    check("Option changes apply", settings.change(autosave::enabledOption, 1) &&
        settings.change(autosave::minimumIntervalOption, 4) && settings.minimumIntervalMilliseconds() == 10 * 60 * 1000);
    settings.save();
    autosave::Settings reloaded;
    reloaded.initialize((fixture.wstring() + L"\\").c_str(), recordLog);
    check("Saved options are read back", reloaded.enabled() && reloaded.value(autosave::minimumIntervalOption) == 4);

    const auto* items = autosave::Settings::definitions();
    bool labelsFit = true;
    for (uint32_t item = 0; item < autosave::optionCount; ++item)
        labelsFit = labelsFit && wcslen(items[item].label.japanese) <= wots4::maxOptionTextChars &&
            wcslen(items[item].label.english) <= wots4::maxOptionTextChars && items[item].choiceCount <= wots4::maxOptionChoices;
    check("Option labels and choices are within the ModLoader limits", labelsFit);
    std::filesystem::remove(config);
}

void testSchedule() {
    autosave::AutosaveSchedule schedule;
    check("Nothing is due at start", !schedule.due());
    check("The first retry checkpoint requests an autosave",
        schedule.backupUpdated(1000, 60'000) && schedule.due());
    schedule.saved(2000);
    check("Saving clears it", !schedule.due());
    check("A checkpoint within the minimum interval is skipped",
        !schedule.backupUpdated(30'000, 60'000) && !schedule.due());
    check("The exact interval boundary is eligible", schedule.backupUpdated(62'000, 60'000) && schedule.due());
    schedule.returnedToTitle();
    check("Returning to the title drops the owed autosave", !schedule.due());
    check("No interval permits another checkpoint immediately", schedule.backupUpdated(62'001, 0));
    check("A newer checkpoint can replace one waiting for the writer", schedule.backupUpdated(62'002, 0) && schedule.due());
    schedule.cancel();
    check("Disabling autosave cancels a pending checkpoint", !schedule.due());
}

void testRetrySave() {
    std::vector<uint8_t> live(0x49b3c), backup(autosave::saveFileSize);
    for (size_t index = 0; index < live.size(); ++index) live[index] = static_cast<uint8_t>(index * 17 + 91);
    for (size_t index = 0; index < backup.size(); ++index) backup[index] = static_cast<uint8_t>(index * 31 + 7);
    // The format word is written by the native backup serializer.
    const uint32_t format = 4;
    memcpy(backup.data()+0x48b34, &format, sizeof(format));
    backup[0] = 4; // A serializer-hook difficulty value must survive the composition.
    const auto liveBefore = live, backupBefore = backup;
    std::vector<uint8_t> guarded(autosave::saveFileSize+2, 0xa5);
    autosave::composeRetrySave(guarded.data()+1, backup.data(), live.data());
    const uint8_t* result = guarded.data()+1;

    // Independently follow restore -> put back retained fields -> serialize.
    auto restored = live;
    memcpy(restored.data()+0x18, backup.data(), 0x48b78);
    memcpy(restored.data()+0x49b1c, backup.data()+0x48b8e, 32);
    for (size_t index = 0; index < 2; ++index) restored[0x41008+index] = live[0x41008+index];
    for (size_t index = 0; index < 0x1104; ++index) restored[0x4222c+index] = live[0x4222c+index];
    for (size_t index = 0; index < 0xc80; ++index) restored[0x43468+index] = live[0x43468+index];
    for (size_t index = 0; index < 8; ++index) restored[0x440e8+index] = live[0x440e8+index];
    std::vector<uint8_t> expected(autosave::saveFileSize, 0);
    memcpy(expected.data(), restored.data()+0x18, 0x48b78);
    memcpy(expected.data()+0x48b78, restored.data(), 22);
    memcpy(expected.data()+0x48b8e, restored.data()+0x49b1c, 32);
    check("The complete file matches restore, retain and serialize", !memcmp(result, expected.data(), expected.size()));
    check("Current game state stays untouched", live == liveBefore);
    check("The game's retry backup stays untouched", backup == backupBefore);
    check("The output stays within its destination", guarded.front()==0xa5 && guarded.back()==0xa5);
    check("Custom serialized difficulty is retained", result[0]==4);
    check("The ordinary payload and location come from the checkpoint",
        !memcmp(result+0x30, backup.data()+0x30, 4) && !memcmp(result+0x10, backup.data()+0x10, 4));
    check("Current settings survive retry", !memcmp(result+0x48b78, live.data(), 22));
    check("Revenge state survives retry", !memcmp(result+0x42214, live.data()+0x4222c, 0x1104));
    check("Encounter history and counts survive retry", !memcmp(result+0x43450, live.data()+0x43468, 0xc80+8));
    check("The retained bytes survive retry", !memcmp(result+0x40ff0, live.data()+0x41008, 2));
    check("The checkpoint trailer is retained", !memcmp(result+0x48b8e, backup.data()+0x48b8e, 32));
    check("Unwritten file padding is initialized", result[0x48bae]==0 && result[0x48baf]==0);
}

void testAtomicWrite() {
    const auto path = fixture / autosave::autoSaveFileName;
    const std::string pathText = path.string();
    const auto first = saveImage(1, "first");
    check("A new file is written", autosave::writeFileAtomically(pathText.c_str(), first.data(), autosave::saveFileSize) == 0 &&
        readBytes(path) == first);
    const auto second = saveImage(2, "second");
    check("An existing file is replaced whole", autosave::writeFileAtomically(pathText.c_str(), second.data(), autosave::saveFileSize) == 0 &&
        readBytes(path) == second);
    check("No temporary file remains", !std::filesystem::exists(pathText + ".tmp"));
    const std::string missingFolder = (fixture / "no_such_folder" / autosave::autoSaveFileName).string();
    check("A missing folder reports a Windows error",
        autosave::writeFileAtomically(missingFolder.c_str(), second.data(), autosave::saveFileSize) == ERROR_PATH_NOT_FOUND);
    std::filesystem::remove(path);
}

void testBackgroundWriter() {
    static autosave::SaveFileWriter writer;  // Its thread is never joined, like in the game
    check("The writer thread starts", writer.start());
    check("The writer is idle before any request", writer.idle());
    const auto image = saveImage(3, "background");
    memcpy(writer.buffer(), image.data(), image.size());
    const auto path = fixture / autosave::autoSaveFileName;
    check("The writer accepts a request", writer.write(path.string().c_str()));
    autosave::SaveFileWriter::Result result{};
    bool reported = false;
    for (int attempt = 0; attempt < 500 && !reported; ++attempt) {
        reported = writer.takeResult(result);
        if (!reported) Sleep(10);
    }
    check("The finished write is reported once with its path", reported && result.error == 0 &&
        !strcmp(result.path, path.string().c_str()) && !writer.takeResult(result));
    check("The written file holds the buffer", readBytes(path) == image);
    check("The writer is idle after reporting", writer.idle());
    std::filesystem::remove(path);
}

void testSaveSlotRecord() {
    const std::string directory = directoryText();
    const autosave::SlotLabels labels{L"< オートセーブがありません >", L"< 破損したファイル >"};
    const auto autoSavePath = fixture / autosave::autoSaveFileName;
    autosave::SaveSlotRecord record;
    memset(&record, 0xcc, sizeof(record));
    char expected[64];
    WideCharToMultiByte(CP_ACP, 0, labels.missing, -1, expected, sizeof(expected), nullptr, nullptr);
    check("A missing auto-save is not usable",
        !autosave::fillSaveSlotRecord(record, directory.c_str(), autosave::autoSaveFileName, labels));
    check("It shows its own text without details",
        !strcmp(record.label, expected) && !record.hasDetails && !record.wrongSize && !record.difficulty);

    writeBytes(autoSavePath, {1, 2, 3});
    WideCharToMultiByte(CP_ACP, 0, labels.wrongSize, -1, expected, sizeof(expected), nullptr, nullptr);
    check("A file of the wrong size is not usable",
        !autosave::fillSaveSlotRecord(record, directory.c_str(), autosave::autoSaveFileName, labels));
    check("It shows the broken data text, dimmed", !strcmp(record.label, expected) && record.wrongSize == 1 && !record.hasDetails);

    writeBytes(autoSavePath, saveImage(4, "Day 3 / Amihama"));
    check("A valid auto-save is usable",
        autosave::fillSaveSlotRecord(record, directory.c_str(), autosave::autoSaveFileName, labels));
    SYSTEMTIME today;
    GetLocalTime(&today);
    check("It shows its file name S4-AUTOSAVE with details, like the manual slots",
        !strcmp(record.label, "S4-AUTOSAVE") && record.hasDetails == 1 && !record.wrongSize);
    check("Its difficulty and description come from the file", record.difficulty == 4 &&
        !strcmp(record.description, "Day 3 / Amihama"));
    check("Its date is the local time of the last write", record.savedAt.wYear == today.wYear &&
        record.savedAt.wMonth == today.wMonth && record.savedAt.wDay == today.wDay);
}

void testNewestSave() {
    const std::string directory = directoryText();
    const auto autoSavePath = fixture / autosave::autoSaveFileName;
    std::filesystem::remove(autoSavePath);
    writeBytes(fixture / "S4-DATA_01", saveImage(1, "manual"));
    setWriteTime(fixture / "S4-DATA_01", -60);
    check("Without an auto-save the cursor stays on the manual saves", !autosave::autoSaveIsNewest(directory.c_str()));
    writeBytes(autoSavePath, saveImage(1, "auto"));
    setWriteTime(autoSavePath, 0);
    check("An auto-save written after every manual save is the newest", autosave::autoSaveIsNewest(directory.c_str()));
    setWriteTime(fixture / "S4-DATA_01", 60);
    check("A manual save written later wins", !autosave::autoSaveIsNewest(directory.c_str()));
    std::filesystem::remove(autoSavePath);
    std::filesystem::remove(fixture / "S4-DATA_01");
}
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    fixture = std::filesystem::absolute(argv[0]).parent_path() / "autosave_offline_fixture";
    std::filesystem::remove_all(fixture);
    std::filesystem::create_directories(fixture);
    testSettings(argv[1]);
    testSchedule();
    testRetrySave();
    testAtomicWrite();
    testBackgroundWriter();
    testSaveSlotRecord();
    testNewestSave();
    std::filesystem::remove_all(fixture);
    std::printf("RESULT: %u checks passed, %u failed\n", checks - failures, failures);
    std::puts("Only files in a fixture folder next to the test were written; no game was started.");
    std::puts("No writes to the installation, saves or game process.");
    return failures ? 1 : 0;
}
