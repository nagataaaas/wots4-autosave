#pragma once
#include "settings.h"

namespace autosave {
// Installs everything the Mod changes in the game, or nothing:
// - retry checkpoint capture and deferred writes (backup preparation and part updates)
// - the sixth row of the title's Load list for the auto-save file
// Returns false after rolling back and releasing every claim. loadRowAdded tells whether the
// Load list got its sixth row (not when the list was made before the Mod loaded).
bool installGameHooks(const wots4::ModContext& context, Settings& settings, LogFunction log, bool& loadRowAdded);
}
