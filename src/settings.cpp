#include "settings.h"
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace autosave {
namespace {
const wots4::LocalizedText toggleChoices[] = {{L"有効", L"On"}, {L"無効", L"Off"}};
const wots4::LocalizedText intervalChoices[] = {
    {L"なし", L"None"}, {L"1分", L"1 min"}, {L"3分", L"3 min"}, {L"5分", L"5 min"}, {L"10分", L"10 min"}};
const wots4::OptionItem items[] = {
    {wots4::optionToggle, {L"オートセーブ", L"Autosave"}, toggleChoices, 2, 1},
    {wots4::optionChoice, {L"オートセーブの最短間隔", L"Minimum Autosave Interval"}, intervalChoices, 5, 0},
};
// config.ini spells each choice by name; the array index is the option value.
// enabled keeps 0/1 like every Mod's on/off key (a toggle's value 1 is "on").
const wchar_t* const enabledNames[] = {L"0", L"1"};
const wchar_t* const intervalNames[] = {L"0", L"1", L"3", L"5", L"10"};
constexpr uint64_t intervalMinutes[] = {0, 1, 3, 5, 10};
constexpr uint64_t millisecondsPerMinute = 60 * 1000;
struct ConfigKey { const wchar_t* section; const wchar_t* key; const wchar_t* const* names; };
const ConfigKey configKeys[] = {
    {L"autosave", L"enabled", enabledNames},
    {L"autosave", L"minimum_interval", intervalNames},
};
static_assert(std::size(items) == optionCount && std::size(configKeys) == optionCount);
static_assert(std::size(intervalChoices) == std::size(intervalNames) && std::size(intervalNames) == std::size(intervalMinutes));
// Longer than any choice name, so a truncated read is never mistaken for a valid one.
constexpr DWORD valueBufferLength = 16;

bool findChoice(const ConfigKey& config, uint32_t choiceCount, const wchar_t* text, uint32_t& value) {
    for (uint32_t choice = 0; choice < choiceCount; ++choice) {
        if (_wcsicmp(text, config.names[choice])) continue;
        value = choice;
        return true;
    }
    return false;
}
std::wstring acceptedNames(const ConfigKey& config, uint32_t choiceCount) {
    std::wstring names;
    for (uint32_t choice = 0; choice < choiceCount; ++choice) {
        if (choice) names += L", ";
        names += config.names[choice];
    }
    return names;
}
}

const wots4::OptionItem* Settings::definitions() { return items; }

void Settings::initialize(const wchar_t* directory, LogFunction logger) {
    path = std::wstring(directory) + L"config.ini";
    log = logger;
    dirty = false;
    for (uint32_t item = 0; item < optionCount; ++item) {
        const ConfigKey& config = configKeys[item];
        const uint32_t choiceCount = items[item].choiceCount;
        values[item] = items[item].defaultValue;
        wchar_t text[valueBufferLength];
        const DWORD length = GetPrivateProfileStringW(config.section, config.key, L"", text, valueBufferLength, path.c_str());
        if (!length) continue;
        const bool truncated = length >= valueBufferLength - 1;
        if (!truncated && findChoice(config, choiceCount, text, values[item])) continue;
        if (!log) continue;
        char message[256];
        sprintf_s(message, "[WARN] config.ini [%ls] %ls=%ls%ls is not one of %ls; using the default %ls.",
            config.section, config.key, text, truncated ? L"..." : L"", acceptedNames(config, choiceCount).c_str(),
            config.names[items[item].defaultValue]);
        log(message);
    }
}

uint32_t Settings::value(uint32_t item) const { return item < optionCount ? values[item] : 0; }

uint64_t Settings::minimumIntervalMilliseconds() const {
    return intervalMinutes[values[minimumIntervalOption]] * millisecondsPerMinute;
}

bool Settings::change(uint32_t item, uint32_t number) {
    if (item >= optionCount || number >= items[item].choiceCount) return false;
    if (values[item] == number) return true;
    values[item] = number;
    dirty = true;
    return true;
}

void WINAPI Settings::readPage(void* context, uint32_t* output, uint32_t count) {
    if (!output || count != optionCount) return;
    const auto& settings = *static_cast<const Settings*>(context);
    for (uint32_t item = 0; item < optionCount; ++item) output[item] = settings.values[item];
}

void WINAPI Settings::changePage(void* context, uint32_t item, uint32_t number) {
    static_cast<Settings*>(context)->change(item, number);
}

bool Settings::registerPage(const wots4::ModContext& context) {
    const wots4::LocalizedText title{L"オートセーブ", L"Autosave"};
    const wots4::OptionsPageCallbacks callbacks{this, readPage, changePage};
    return context.host->addOptionsPage(context.module, &title, items, optionCount, &callbacks) != FALSE;
}

void Settings::save() {
    if (!dirty) return;
    for (uint32_t item = 0; item < optionCount; ++item) {
        const ConfigKey& config = configKeys[item];
        // Updates the key in place, keeping the comments of the bundled config.ini.
        if (WritePrivateProfileStringW(config.section, config.key, config.names[values[item]], path.c_str())) continue;
        if (log) {
            char message[256];
            sprintf_s(message, "[ERROR] Could not save config.ini [%ls] %ls (Windows error %lu); "
                "settings remain active and will be retried on the next options save.",
                config.section, config.key, GetLastError());
            log(message);
        }
        return;
    }
    // A cache-flush call returns zero even on success; only the writes above
    // determine whether the settings need to be retried.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    dirty = false;
}
}
