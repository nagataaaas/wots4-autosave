#pragma once
#include <wots4/mod_api.h>
#include <string>

namespace autosave {
constexpr uint32_t optionCount = 2;
enum OptionIndex : uint32_t { enabledOption, minimumIntervalOption };
using LogFunction = void (*)(const char*);

class Settings {
public:
    void initialize(const wchar_t* directory, LogFunction logger);
    bool registerPage(const wots4::ModContext& context);
    void save();
    bool change(uint32_t item, uint32_t value);
    uint32_t value(uint32_t item) const;
    bool enabled() const { return values[enabledOption] != 0; }
    uint64_t minimumIntervalMilliseconds() const;
    static const wots4::OptionItem* definitions();
private:
    static void WINAPI readPage(void*, uint32_t*, uint32_t);
    static void WINAPI changePage(void*, uint32_t, uint32_t);
    uint32_t values[optionCount] = {1, 0};
    bool dirty = false;
    std::wstring path;
    LogFunction log = nullptr;
};
}
