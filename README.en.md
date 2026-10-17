# Autosave

[日本語](README.md) | [Changelog](CHANGELOG.en.md)

> [!NOTE]
> This Mod is for the [Steam version of Way of the Samurai 4](https://store.steampowered.com/app/312780/4/?l=english) only.

Adds an autosave and lets you continue from it on the Load screen. Autosaves go to their own save data, separate from the five manual slots, so they never overwrite your manual saves.

## Features

- **Autosave the retry state**: whenever the game updates its retry checkpoint, the Mod saves the data that retrying at that moment would restore. The game updates the checkpoint when, for example, you start a new game, move between areas normally or use a save menu.
  - Progress and inventory come from the retry checkpoint. Data that Retry keeps, such as revenge data and encounter history, is saved as it is at the moment of saving. Saving never rewinds the game you are playing.
  - Loading the autosave returns you to the retry checkpoint, not to the latest state after a move or to where you stood when it was saved.
- **An autosave row on the Load screen**: the title screen's Load list gets a sixth row, so you can continue from the autosave. Like the manual saves (`S4-DATA_00` and so on), the row shows its file name, `S4-AUTOSAVE`. When the autosave is the newest save data, that row is selected from the start. Until there is an autosave, the row reads "No Autosave".
- **Options page**: the options get an "Autosave" page where you can switch autosaving on or off and set a minimum interval between autosaves.

To fit six rows on screen, the rows of the Load list are spaced a little closer together. The Save and Delete lists keep their usual five rows.

## Installation

### Automatic installation (recommended)

1. Close the game.
2. Download and run [WayOfTheSamurai4ModInstaller.exe](https://github.com/nagataaaas/wots4-mods/releases/latest/download/WayOfTheSamurai4ModInstaller.exe).
   - If "Windows protected your PC" appears, click "More info" → "Run anyway".
   - When asked whether to allow it to make changes as administrator, click "Yes" (it writes to the game folder).
3. Follow the screen and choose `autosave` in the list of Mods. Move with Up and Down, choose with Space and confirm with Enter.

The ModLoader is installed along with it. The same steps update the Mod, and your settings (`config.ini`) are kept.

<details>
<summary>Manual installation</summary>

> [!WARNING]
> Install [wots4-mods (ModLoader)](https://github.com/nagataaaas/wots4-mods) first.

1. Close the game.
2. Download `wots4-mod-autosave-<version>.zip` from [Releases](../../releases) and extract it.
3. Copy the extracted `mods` folder into the game's installation folder (the folder containing `WayOfTheSamurai4.exe`). If a `mods` folder already exists, merge the contents into it.
4. Confirm that `{game installation folder}/mods/autosave/mod.dll` exists, then start the game.

When updating, do not overwrite `mods/autosave/config.ini`, so that your settings are kept (replace only `mod.dll` and the other files).

</details>

## Uninstallation

1. Close the game.
2. Delete the `{game installation folder}/mods/autosave/` folder. The settings in it (`config.ini`) are deleted too.

The autosave data (`S4-AUTOSAVE` in the save folder) stays. Without the Mod it does not appear on the Load screen, so delete it if you no longer need it. The save folder is usually `Documents\My Games\Ghostlight Ltd\Way of the Samurai 4\`.

## Settings

Change them on the "Autosave" page (オートセーブ in the Japanese game), which is added to the options on the title screen and to 各種設定 in the pause menu. They are saved to `{game installation folder}/mods/autosave/config.ini` when you close the options, and take effect immediately. You may also edit `config.ini` directly while the game is not running.

| Option | Section | Key | Default | Description |
| --- | --- | --- | --- | --- |
| Autosave | `autosave` | `enabled` | `1` (On) | `0` (Off) stops autosaving. The autosave row on the Load screen stays, so you can still load the last autosave. |
| Minimum Autosave Interval | `autosave` | `minimum_interval` | `0` (None) | Retry checkpoint updates within this many minutes of the last autosave are not saved. One of `0`, `1`, `3`, `5` or `10`. |
