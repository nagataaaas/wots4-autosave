# Autosave

[日本語](README.md)

> [!NOTE]
> This Mod is for the [Steam version of Way of the Samurai 4](https://store.steampowered.com/app/312780/) only.

Automatically saves the state you would return to by choosing Retry after death. Autosaves go to their own save data, separate from the five manual slots, so they never overwrite your manual saves.

## Features

- **Autosave the retry state**: whenever the game updates its retry checkpoint, the Mod saves the data that retrying at that moment would restore. The game updates the checkpoint when, for example, you start a new game, move between areas normally or use a save menu.
  - Progress and inventory come from the retry checkpoint. Data that Retry keeps, such as revenge data and encounter history, is saved as it is at the moment of saving. Saving never rewinds the game you are playing.
  - Loading the autosave returns you to the retry checkpoint, not to the latest state after a move or to where you stood when it was saved.
- **An autosave row on the Load screen**: the title screen's Load list gets a sixth row, so you can continue from the autosave. Like the manual saves (`S4-DATA_00` and so on), the row shows its file name, `S4-AUTOSAVE`. When the autosave is the newest save data, that row is selected from the start. Until there is an autosave, the row reads "No Autosave".
- **Options page**: the options get an "Autosave" page where you can switch autosaving on or off and set a minimum interval between autosaves.

To fit six rows on screen, the rows of the Load list are spaced a little closer together. The Save and Delete lists keep their usual five rows.

## Installation

> [!WARNING]
> Install [wots4-mods (ModLoader)](https://github.com/nagataaaas/wots4-mods) first.

1. Exit the game.
2. Download `wots4-mod-autosave-<version>.zip` from [Releases](../../releases) and extract it.
3. Copy the extracted `mods` folder into the Way of the Samurai 4 installation folder (where `WayOfTheSamurai4.exe` is). If a `mods` folder already exists, merge them.
4. Make sure `{Way of the Samurai 4 installation folder}/mods/autosave/mod.dll` exists, then start the game.

When updating, do not overwrite `mods/autosave/config.ini`, so that your settings are kept (replace only `mod.dll` and the other files).

## Uninstallation

1. Exit the game.
2. Delete the `{Way of the Samurai 4 installation folder}/mods/autosave/` folder. Your settings are deleted with it.

The autosave data (`S4-AUTOSAVE` in the save folder) stays. Without the Mod it does not appear on the Load screen, so delete it if you no longer need it. The save folder is usually `Documents\My Games\Ghostlight Ltd\Way of the Samurai 4\`.

## Settings

Change them on the "Autosave" page of the game's options. They are saved to `{Way of the Samurai 4 installation folder}/mods/autosave/config.ini` when you close the options, and take effect immediately. You may also edit `config.ini` directly while the game is not running.

| Option | `config.ini` key | Default | Description |
| --- | --- | --- | --- |
| Autosave | `[autosave]` `enabled` | On (`1`) | Off (`0`) stops autosaving. The autosave row on the Load screen stays, so you can still load the last autosave. |
| Minimum Autosave Interval | `[autosave]` `minimum_interval` | None (`0`) | Retry checkpoint updates within this many minutes of the last autosave are not saved. One of `0`, `1`, `3`, `5` or `10`. |
