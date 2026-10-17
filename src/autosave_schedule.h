#pragma once
#include <cstdint>

namespace autosave {
// Backup updates, rather than frame samples of area/script state, request saves.
class AutosaveSchedule {
public:
    bool backupUpdated(uint64_t nowMilliseconds, uint64_t minimumIntervalMilliseconds) {
        if (hasSaved && nowMilliseconds - lastSaveMilliseconds < minimumIntervalMilliseconds) return false;
        pending = true;
        return true;
    }
    void returnedToTitle() { pending = false; }
    void cancel() { pending = false; }
    bool due() const { return pending; }
    void saved(uint64_t nowMilliseconds) {
        pending = false;
        hasSaved = true;
        lastSaveMilliseconds = nowMilliseconds;
    }
private:
    bool pending = false;
    bool hasSaved = false;
    uint64_t lastSaveMilliseconds = 0;
};
}
