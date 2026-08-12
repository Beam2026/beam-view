# Building beam-view on Windows

Upstream's own instructions are in `README.md` and are broadly right. This file covers what they
leave out — including two traps that produce misleading failures on a normal developer machine.

Verified on Windows 11 with Visual Studio 2022 **Build Tools** 17.14 and Qt 6.11.1.

## What you need

| | Why |
| --- | --- |
| **Qt 6.11.1**, arch `msvc2022_64` | The version upstream CI builds against |
| **MSVC** — VS 2022 or Build Tools | `README.md` says VS 2026; CI actually uses the `windows-2025` runner with MSVC 2022, and Build Tools is enough |
| **7-Zip** on `PATH` | Not optional — see trap 2 |
| **Python** | Only to run `aqtinstall`, if installing Qt that way |

Everything else — FFmpeg, SDL2, OpenSSL, dav1d, libplacebo, Opus — arrives prebuilt via
`setup-deps.ps1`. There is no vcpkg or manual dependency build.

## Installing Qt

The Qt online installer needs an account and is interactive. `aqtinstall` is what CI uses:

```powershell
pip install "git+https://github.com/miurahr/aqtinstall.git@073e34d7c2ab4ae6961ed7cca690b3abd5ba5a7e"
python -m aqt install-qt windows desktop 6.11.1 win64_msvc2022_64 --outputdir C:\Qt
```

**Install that exact aqtinstall commit**, the one upstream CI pins. Released aqtinstall 3.3.0 builds
the wrong repository URL for Qt 6.11 and fails with:

```text
WARNING : Failed to download checksum for the file
'online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111/Updates.xml'
ERROR   : Failed to locate XML data for Qt version '6.11.1'
```

Note the doubled `qt6_6111/qt6_6111` — that is the bug, not a broken mirror, and retrying against
`--base https://download.qt.io/` does not help. Roughly 2.1 GB installed.

## First-time setup

```powershell
git submodule update --init --recursive
powershell .\setup-deps.ps1
```

`setup-deps.ps1` downloads prebuilt native libraries into `libs\windows\`. It is pinned to a tag
(currently `v12`) inside the script, so it is reproducible.

The pinned tag matters beyond reproducibility: `v11` shipped an FFmpeg whose 8-bit full-range
D3D11VA decoding is broken, and upstream's default color range is now full — so every 8-bit
hardware decode failed with `AVHWFramesContext: Unsupported pixel format: (null)` in an endless
IDR-request loop. Audio played, no picture ever appeared. `v12` carries the fixed FFmpeg
(upstream commit `2e13ed99`). If decode loops like that ever reappear after a rebase, check the
deps tag against upstream's before debugging anything else.

## Building

```powershell
$env:PATH = "C:\Qt\6.11.1\msvc2022_64\bin;C:\Program Files\7-Zip;$env:PATH"
scripts\build-arch.bat Release x64
```

The script finds MSVC itself and runs `vcvarsall.bat`, so a "Developer Command Prompt" is not
needed — *provided* trap 1 does not apply to you.

Output:

| Directory | Contents |
| --- | --- |
| `build\deploy-x64-release\` | **The portable app.** Exe, Qt runtime, native DLLs, QML. |
| `build\build-x64-release\` | Object files and the linked exe before deployment |
| `build\symbols-x64-release\` | PDBs and their zip |
| `build\installer-x64-release\` | Installer output, if built |

## Trap 1 — "Cannot run compiler 'cl'"

```text
Configuring the project
Project ERROR: Cannot run compiler 'cl'. Output:
Maybe you forgot to setup the environment?
```

`scripts\build-arch.bat` locates Visual Studio with:

```bat
%VSWHERE% -latest -property installationPath
```

`vswhere` **excludes Build Tools from its default product filter**. Without a full Visual Studio
install that command prints nothing, the `for /f` loop body never executes, `vcvarsall.bat` is never
called, and qmake then fails on a missing compiler. The script does not check, so the error arrives
several steps later than the cause.

Upstream would need `-products *`. Until that is patched, supply the environment yourself:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvarsall.bat`" x64 && scripts\build-arch.bat Release x64"
```

The same `vswhere` call also sets `VC_REDIST_DLL_PATH` for the CRT DLLs. Because it is only assigned
*inside* the loop that never runs, a value already in the environment survives — so pre-set it:

```powershell
$env:VC_REDIST_DLL_PATH = "$vs\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"
```

## Trap 2 — an empty `deploy` folder after a successful compile

```text
'7z' is not recognized as an internal or external command
Build failed
```

7-Zip looks optional — `README.md` lists it under "only if building installers". It is not. The
script zips debug symbols **before** it copies any DLLs or runs `windeployqt`, and aborts there. The
result is a compile and link that both succeeded, a linked exe sitting in
`build\build-x64-release\app\release\`, and a completely empty `build\deploy-x64-release\`.

```powershell
winget install 7zip.7zip
$env:PATH = "C:\Program Files\7-Zip;$env:PATH"
```

There is no resume: `scripts\build-arch.bat` deletes all four output directories on entry, so
hitting this costs a full rebuild.

## Iterating

Because the script wipes and rebuilds every time, do not use it for day-to-day work. Configure once,
then drive `jom` directly:

```powershell
cd build\build-x64-release
..\..\scripts\jom.exe release
```

Run the full script when a complete deployable tree is needed — before testing against Beam, or
before cutting a release.
