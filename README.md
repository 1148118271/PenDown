# PenDown · Lightweight Windows Notes

English | [简体中文](README.zh-CN.md)

PenDown is a portable Windows desktop note-taking app. Each note body is stored as a plain UTF-8 `.txt` file, while colors, fonts, and other formatting are kept in a matching `.style.json` file. It needs no database and no Markdown syntax.

## Download and run

Download `PenDown-*-win64.zip` from [Releases](https://github.com/1148118271/PenDown/releases), extract it, and double-click `PenDown.exe`.

- Requires Windows 10 / 11 x64
- No installation needed; on first launch, PenDown creates a `data` folder next to the executable
- Runtime libraries are included in the zip
- The executable is not code-signed, so Windows SmartScreen may show “Windows protected your PC”. Choose “More info” → “Run anyway”
- Prefer a user-writable folder; `Program Files` may not be writable

## Quick start

The main window is a narrow, tall layout with a toolbar at the top and a multi-level group tree below.

- Right-click a group: create a subgroup or note, rename, move, or delete it
- Right-click empty space: create a group or note at the root
- Notes can live at the root or inside any nested group
- `Del` deletes, `F2` renames a group / opens a note, `F5` refreshes
- `Ctrl+N` creates a note, `Ctrl+Shift+N` creates a group
- The gear icon opens Settings

Deleted notes and groups are moved to the Windows Recycle Bin. Deleting a group also moves all its subgroups and notes to the Recycle Bin.

## Editing notes

Each note opens in its own window that can be moved independently of the main window.

- The body is displayed as the final formatted text; no Markdown syntax is required
- The first line of the body becomes the note title and filename; saving after changing it renames the file
- `Ctrl+S` saves; closing a note with unsaved changes asks for confirmation
- Line numbers are shown on the left
- Closing the main window only hides it to the system tray; open note windows stay open

Common shortcuts:

| Shortcut | Action |
| --- | --- |
| `Ctrl+S` | Save |
| `Ctrl+B` / `Ctrl+I` / `Ctrl+U` | Bold / italic / underline |
| `Ctrl+0` | Reset the current line to body style |
| `Ctrl+F` / `Ctrl+H` | Find / replace |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo text |
| `Ctrl+A` | Select all |

## Colors, styles, and fonts

Formatting controls are in the note title bar:

- **Color palette**: set text color or background color, with common colors and custom RGB
- **Style library**: bold, italic, underline, strikethrough, font size +/−, hyperlink, and font selection
- **Clear formatting**: restore the default font, size, colors, background, and link
- **Format painter**: copy the format at the caret, then select the target text and release the mouse to apply it

How to use them:

- Select text first, then click a style to apply it to the selection
- With no selection, colors or fonts you set apply to the next text you type
- The color palette and style library are floating windows; they can be open together and dragged outside the editor
- To add a hyperlink, select the text and enter a URL; hold `Ctrl` and click the link to open it in your browser
- `Ctrl+Z` / `Ctrl+Y` only undo/redo text. Style and link changes are not part of the undo stack

This is a plain-text + character-style editor. It does not support embedded images, tables, or Markdown syntax highlighting.

## UI language

Choose the language at the top of Settings:

- **System default**: Chinese on Chinese systems, English otherwise
- **Chinese**
- **English**

Restart PenDown after changing it. The language affects UI text only; note filenames and default titles on disk stay the same.

## Data and backups

The default data folder is `data` next to the executable:

```text
PenDown/
├─ PenDown.exe
└─ data/
   ├─ Shopping list.txt
   ├─ Shopping list.txt.style.json
   └─ Work/
      ├─ Weekly meeting.txt
      └─ Project A/
         └─ API notes.txt
```

- Each folder is a group; each `.txt` file is a note
- Colors, fonts, links, and other formatting are stored in the matching `.txt.style.json`
- A note that has been saved autosaves a few seconds after you stop typing; the next time you open it, PenDown asks whether to restore the autosaved version
- Settings → “Data storage” changes the data folder. You can optionally copy existing notes; the old folder is kept
- The startup option `--data <folder>` temporarily uses another data folder
- When upgrading, replace only the executable, DLLs, `icons/`, and `lang/`; do not overwrite or delete `data/`
- Back up the whole `data` folder before upgrading

## Git sync (optional)

Git is not required for normal use. To sync notes between devices:

1. Install [Git for Windows](https://git-scm.com/download/win)
2. Open Settings → “Git sync”
3. Enter the repository URL and branch, plus an optional username and password/token
4. Close all note windows
5. Click “Push” or “Pull”

Sync rules:

- The whole `data` folder is used as one Git repository
- Push: commit local changes, then force-push; the remote is replaced with the local state
- Pull: back up local notes to `%LOCALAPPDATA%\PenDown\backups`, then force-overwrite the local folder with the remote state
- No conflict merging is performed
- The access token is encrypted with Windows DPAPI and stored on this computer

## Tray and global shortcuts

Closing the main window does not exit PenDown; it hides the window to the system tray. Use “Close” in the tray menu to exit.

Default global shortcuts:

| Shortcut | Action |
| --- | --- |
| `Ctrl+Alt+H` | Show the main window |
| `Ctrl+Alt+N` | Create a note |

Change them in Settings → “Keyboard shortcuts”. The new-note shortcut only opens the editor and does not bring the main window back from the tray.

## FAQ

### Can I recover deleted notes?

Yes. Notes and groups are moved to the Windows Recycle Bin. Open the Recycle Bin to restore them. If the data folder is on a network drive or another location without a Recycle Bin, deletion fails with an error; use File Explorer instead.

### Where are my old `.md` / `.wxr` notes?

The old files remain in the data folder and are not deleted, but they are not listed as notes. Convert their content to a new `.txt` note manually.

### Can I run multiple PenDown instances?

No. Launching PenDown again activates the existing instance instead of starting a second process.

### Git sync says git.exe was not found

Install Git for Windows, or set the actual `git.exe` path in Settings → “Git sync” → “Git executable”.

### Will updating the app overwrite my notes?

No. Replacing the program files does not touch your notes, but do not overwrite or delete the `data` folder.

### Why is there no installer?

PenDown is portable: extract it and run. If `Program Files` causes write-permission issues, put it in a user folder or use `--data` to point to a separate data folder.

## Third-party components and license

PenDown uses wxWidgets, Scintilla / wxStyledTextCtrl, and Lucide icons. PenDown itself is released under the MIT License; third-party components follow their own licenses. See `LICENSE` in the repository and the upstream project pages for details.
