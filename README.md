# 7-Zip Fork: Dark Mode + Windows 11 Right-Click Menu

An unofficial fork of [7-Zip](https://7-zip.org) 26.04 by Igor Pavlov, with two additions:

- **Dark mode.** 7-Zip File Manager and all its windows (Add to Archive, Extract, progress, Options, ...) follow the Windows light/dark setting, with a rounded, animated progress bar. You can also pick it yourself in **Tools → Options → Settings → Theme** (System / Light / Dark).
- **Windows 11 right-click menu.** "7-Zip" appears directly in the new right-click menu, no more clicking **Show more options**.

Everything else is regular 7-Zip.

> This fork is not affiliated with or supported by the 7-Zip project. Please don't report its bugs to the official 7-Zip.

---

## Quick start (for beginners)

You'll build the program from source on your own PC, then register it with Windows. It takes about 15 minutes, most of which is waiting for Visual Studio to install.

**You need:** Windows 10 or 11 (64-bit). The new right-click menu itself only exists on Windows 11; on Windows 10 you still get dark mode.

### Step 1: Install the tools (one time)

Open **PowerShell** (press Start, type `PowerShell`, press Enter) and run these two commands:

```powershell
winget install Git.Git
winget install Microsoft.VisualStudio.2022.Community --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive"
```

- The first installs **Git** (downloads the code).
- The second installs **Visual Studio 2022 Community** (free) with the **Desktop development with C++** workload, which includes the compiler and the Windows SDK. It's a big download.

If you already have Visual Studio 2022, open **Visual Studio Installer**, click **Modify**, and make sure **Desktop development with C++** is ticked.

**Close PowerShell and open a new one** after installing, so it can find Git.

### Step 2: Download the code

```powershell
cd $HOME\Documents
git clone -b dark-mode-win11 https://github.com/jackthirth/7zip.git 7zip-fork
cd 7zip-fork
```

This creates the folder `Documents\7zip-fork`.

### Step 3: Build it

```powershell
.\build.cmd
```

The first build takes a few minutes. It's done when you see **Build finished.** If it stops with an error, see [Troubleshooting](#troubleshooting).

### Step 4: Create a signing certificate (one time, needs Administrator)

Windows only accepts the right-click menu package if it's signed by a certificate your PC trusts. This script creates a personal certificate that is trusted only on your PC.

1. Press Start, type `PowerShell`, right-click **Windows PowerShell**, and choose **Run as administrator**.
2. Run (adjust the path if you cloned somewhere else):

```powershell
powershell -ExecutionPolicy Bypass -File "$HOME\Documents\7zip-fork\package\make-cert.ps1"
```

You should see `Certificate trusted`. You can close the Administrator window now.

### Step 5: Install it

Back in a **normal** (not Administrator) PowerShell window, in the `7zip-fork` folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\package\install.ps1
```

This copies the program to `%LOCALAPPDATA%\Programs\7-Zip Fork` and registers the right-click menu. You should see `Installed to ...`.

### Step 6: Try it

- **Right-click any file or folder** in File Explorer. **7-Zip** is now in the main menu. If it isn't, restart Explorer: open Task Manager (Ctrl+Shift+Esc), find **Windows Explorer**, right-click → **Restart**.
- **Open the File Manager:** press Win+R, paste the line below, press Enter. To keep it handy, open that folder in Explorer, right-click `7zFM.exe` → **Show more options** → **Pin to Start**.

```
%LOCALAPPDATA%\Programs\7-Zip Fork\7zFM.exe
```

- **Dark mode** follows Windows: **Settings → Personalization → Colors → Choose your mode**. Or set it inside 7-Zip: **Tools → Options → Settings → Theme**.

---

## Updating to a newer version of this fork

```powershell
cd $HOME\Documents\7zip-fork
git pull
.\build.cmd
powershell -ExecutionPolicy Bypass -File .\package\install.ps1 -RestartExplorer
```

`-RestartExplorer` briefly restarts Explorer (the taskbar flickers) so the old right-click menu file can be replaced. You don't need to redo Step 4.

## Uninstalling

```powershell
powershell -ExecutionPolicy Bypass -File .\package\uninstall.ps1
```

This removes the right-click menu. Then delete the folder `%LOCALAPPDATA%\Programs\7-Zip Fork`. To also remove the certificate, run this in an **Administrator** PowerShell:

```powershell
Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\TrustedPeople | Where-Object Subject -eq 'CN=7-Zip Fork Dev' | Remove-Item
```

## Using it alongside the official 7-Zip

That's fine. If the official 7-Zip is installed, its own entry stays under **Show more options**, and the fork's entry appears in the main menu. File associations (double-clicking `.7z`/`.zip` files) aren't changed by this fork; you can set them in the fork's **Tools → Options → System** if you want.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `git` or `winget` is "not recognized" | Close PowerShell and open a new one after installing. `winget` comes with the **App Installer** app from the Microsoft Store. |
| `Visual Studio C++ tools not found` | Open **Visual Studio Installer** → **Modify** → tick **Desktop development with C++** → **Modify**. |
| `running scripts is disabled on this system` | Run scripts the way shown above: `powershell -ExecutionPolicy Bypass -File ...`. |
| `Signing certificate ... not found: run make-cert.ps1 first` | Do Step 4. |
| Install fails with `0x800B0109` or a trust/certificate error | Step 4 was run without Administrator. Run it again **as administrator**. |
| `Could not overwrite 7-zip.dll (in use?)` | Close all 7-Zip windows and run install again with `-RestartExplorer`. |
| The menu entry doesn't appear | Restart Windows Explorer (see Step 6). The new menu only exists on Windows 11. |

---

## For developers

- `build.cmd` builds x64 with MSVC and nmake. Outputs go to each project's `x64\` folder: `Bundles\Fm` (7zFM.exe), `UI\GUI` (7zG.exe), `UI\Explorer` (7-zip.dll), `Bundles\Format7zF` (7z.dll), `UI\Console` (7z.exe). The code is compiled with `-Wall -WX`, so warnings are errors.
- Dark mode: `CPP/Windows/DarkMode.cpp`, hooked into all dialogs through `CPP/Windows/Control/Dialog.cpp`.
- Windows 11 menu: 7-Zip's `IExplorerCommand` handler (`CPP/7zip/UI/Explorer/ContextMenu.cpp`) registered through a signed sparse MSIX package in `package/`.
- To pull in a new official 7-Zip release: `git remote add upstream https://github.com/ip7z/7zip.git`, then `git fetch upstream` and merge the new release.

## License

7-Zip is Copyright (C) 1999-2026 Igor Pavlov, licensed under the GNU LGPL (with the unRAR restriction and some BSD 3-clause code). See `DOC/License.txt` and `DOC/readme.txt`. The changes in this fork are under the same license. The unRAR code may not be used to develop a RAR (WinRAR) compatible archiver.
