#include "game_hooks.h"
#include <cstddef>
#include <cstdio>

namespace {
const wots4::HostApi* host = nullptr;
HMODULE module = nullptr;
autosave::Settings settings;
void log(const char* message) { if (host && host->log) host->log(module, message); }
void WINAPI saveSettings(void*) { settings.save(); }
bool supported(const wots4::ModContext* context) {
    if (!context || context->size < sizeof(*context) || context->version != wots4::modApiVersion ||
        !context->game || !context->directory || !context->host || !context->module) return false;
    const auto& api = *context->host;
    constexpr size_t requiredSize = offsetof(wots4::HostApi, onOptionsSaved) + sizeof(api.onOptionsSaved);
    return api.size >= requiredSize && api.version == wots4::modApiVersion && api.claim && api.releaseClaims &&
        api.addOptionsPage && api.onOptionsSaved;
}
}

extern "C" BOOL WINAPI ModInitialize(const wots4::ModContext* context) {
    if (!supported(context)) return FALSE;
    host = context->host;
    module = context->module;
    try {
        settings.initialize(context->directory, log);
        if (!settings.registerPage(*context) || !host->onOptionsSaved(module, saveSettings, nullptr)) {
            log("[ERROR] The ModLoader did not accept the Autosave options page; Autosave was refused.");
            return FALSE;
        }
    } catch (...) {
        log("[ERROR] Autosave initialization failed while reading its settings; Autosave was refused.");
        return FALSE;
    }
    bool loadRowAdded = false;
    if (!autosave::installGameHooks(*context, settings, log, loadRowAdded)) return FALSE;
    char message[256];
    sprintf_s(message, "[INFO] Autosave initialized: saving retry checkpoints to S4-AUTOSAVE is %s (options page); %s.",
        settings.enabled() ? "on" : "off",
        loadRowAdded ? "the title's Load list shows S4-AUTOSAVE as a sixth row" : "the Load list keeps five rows");
    log(message);
    return TRUE;
}
