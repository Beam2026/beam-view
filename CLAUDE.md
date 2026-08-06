# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

> **Asked to build, run or change this?** Read [`docs/building.md`](docs/building.md) first — the
> build has two traps on a normal dev machine and neither produces an obvious error.

## What this is

`beam-view` is Beam's fork of [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) —
the client that decodes a Sunshine stream and renders it. Beam runs it as a **child process** and
shows its window *inside* Beam's own window, so the user never sees a second application.

Beam itself lives in a separate, private repo at `C:\Projects\BeamApp\BEAM`. It talks to this
program only over a command line. **That process boundary is load-bearing** — see Licence below.

This is a **modified** version of Moonlight. It is not Moonlight, must not claim to be, and its
upstream is tracked at the `upstream` remote.

## Licence — read before restructuring anything

This repo is **GPL-3.0**, inherited from upstream, and that is not negotiable. Two obligations
follow, and one boundary must never be crossed:

- **Say that it is modified.** GPL-3 §5(a) requires prominent notices that the work was changed and
  when. Keep them in source headers, the README, and release notes.
- **Publish this source.** Anyone receiving a Beam build that contains this program is entitled to
  it. That is why this repo is public and Beam's is not.
- **Never link this into Beam.** Beam invokes it as a separate process. Turn any of it into a
  library Beam links against and Beam becomes a combined work — GPL-3 then covers Beam's own
  source, which is the exact outcome the whole architecture exists to prevent. A CLI is a boundary;
  a DLL is not.

No on-screen attribution is required: §5(d) only obliges keeping legal notices in an interactive UI
*if the original displayed them*, and Moonlight shows no copyright splash. Rebranding the UI
entirely is compliant. Renaming is also trademark-safe — it moves away from upstream's marks.

Do **not** delete `LICENSE` or the copyright headers.

## Branch and patch discipline

Work happens on **`beam`**, branched from a pinned upstream commit. `master` tracks upstream and
should not be committed to.

Upstream's last release was **v6.1.0 in September 2024**, but `master` is still committed to daily —
roughly two years of unreleased work. The base is therefore a pinned `master` commit
(`7cf8b46c`), not a tag: it is what upstream's CI actually keeps green, and there is no newer tag to
prefer.

**Keep patches small, separate, and single-purpose.** Rebasing onto upstream is the permanent cost
of this fork, and it scales with how tangled the changes are. One concern per commit. If the patch
set grows much past the five in [`docs/patches.md`](docs/patches.md), that is the signal that
owning the whole pipeline — capture, encode, decode, render, no GPL at all — has become cheaper
than maintaining a fork.

## Build

Full detail, including the two traps, is in [`docs/building.md`](docs/building.md). Briefly:

```powershell
# once
git submodule update --init --recursive
powershell .\setup-deps.ps1              # prebuilt FFmpeg/SDL2/OpenSSL, ~v11 release

# every build - needs Qt 6.11.1 msvc2022_64, MSVC, and 7-Zip on PATH
scripts\build-arch.bat Release x64
```

Output lands in `build\deploy-x64-release\` — that directory *is* the portable app.

**`scripts\build-arch.bat` wipes its output directories on every run**, so there is no incremental
build through it. Iterating on code is much faster through `jom` in `build\build-x64-release\`
directly; only use the full script when a complete deployable tree is needed.

## Code map

Where the planned changes land. Line numbers drift — search for the symbol.

| Concern | Location |
| --- | --- |
| Settings path / app identity | `app/main.cpp` — `setOrganizationName` / `setApplicationName` |
| Executable name, version strings | `app/app.pro` — `TARGET`, `QMAKE_TARGET_*` |
| Stream window creation | `app/streaming/session.cpp` — `SDL_CreateWindow` |
| Stream window title | `app/streaming/session.cpp` — `" - Moonlight"` |
| CLI options | `app/cli/commandlineparser.cpp` — `StreamCommandLineParser::parse` |
| `pair` / `quit` / `stream` entry points | `app/cli/pair.cpp`, `quitstream.cpp`, `startstream.cpp` |
| "Establishing connection to PC…" overlays | `app/gui/CliPair.qml`, `CliStartStreamSegue.qml`, `CliQuitStreamSegue.qml` |
| "has not been paired" message | `app/cli/startstream.cpp`, `quitstream.cpp`, `listapps.cpp` |

**`app/main.cpp`'s organisation and application names are not cosmetic.** They decide the settings
path, currently `HKCU\Software\Moonlight Game Streaming Project\Moonlight` — *shared with any
Moonlight the user has installed*. That shared state is what left four stale host records all
claiming `127.0.0.1` and broke Beam's pairing. Changing these isolates the fork and fixes that
class of bug at the root.

## What Beam expects

The CLI contract Beam depends on is in [`docs/beam-integration.md`](docs/beam-integration.md).
Changing any of it means changing `desktop/src-tauri/src/engines.rs` in the Beam repo at the same
time — nothing enforces that the two agree.

Note that `pair` and `quit` are **not** headless today: each opens a window and shows
"Establishing connection to PC…". Making them silent is a required change, not a nicety — those
windows appear either side of every session.
