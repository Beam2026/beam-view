# How Beam drives this program

Beam (`C:\Projects\BeamApp\BEAM`, private) runs `beam-view` as a child process. This file is the
contract between them. **It is defined in two places with nothing enforcing agreement** — here, and
`desktop/src-tauri/src/engines.rs` in the Beam repo. Change one, change the other.

## The shape of a session

Beam does not stream. It builds an encrypted peer-to-peer tunnel between two machines, so each side
talks to `127.0.0.1` and neither engine knows the internet was crossed:

```text
beam-view → 127.0.0.1:ports → client-agent ══P2P══ host-agent → 127.0.0.1:ports → Sunshine
```

Every address this program is given is therefore **always `127.0.0.1`**, on both machines, in every
session.

That has a consequence worth internalising: **`127.0.0.1` is a different physical machine every
time.** Any state cached against that address — pairing certificates most of all — describes
whoever was on the other end of the tunnel last session, not this one. This is not theoretical; it
is the bug that made Moonlight refuse to stream with "Computer Hadi has not been paired", because
the cached record for `127.0.0.1` had been issued by a different Sunshine.

Beam therefore pairs on **every** session and never trusts a cached answer. `pair` must stay cheap,
repeatable, and safe to run against an already-paired host.

## What Beam invokes

```powershell
beam-view.exe pair   127.0.0.1 --pin 1234
beam-view.exe stream 127.0.0.1 "Desktop" --display-mode borderless --absolute-mouse enable --quit-after enable --embed-hwnd 1182734
beam-view.exe quit   127.0.0.1
```

Pairing is automatic and invisible: Beam generates the PIN, sends it to the host over its own
signalling channel, and the host's copy of Beam approves it against Sunshine. Nobody types a PIN.

Ports carried by the tunnel — TCP 47984 (HTTPS/pairing), 47989 (HTTP), 48010 (RTSP); UDP 47998
(video), 47999 (control), 48000 (audio).

## The contract as implemented

All of this exists on the `beam` branch. Full breakdown in [`patches.md`](patches.md).

**`--embed-hwnd <handle>`** (Windows only, decimal `u64`). The stream window is created hidden,
restyled to `WS_CHILD`, reparented into the given HWND and sized to fill its client area before it
is ever shown — it never exists as a top-level window. From then on the parent owns position and
size; Beam resizes the child with `MoveWindow` and SDL adapts. Recommended pattern: Beam creates a
dedicated native host window inside its stage, passes that handle, and shows/positions the host
when `beam: first-frame` arrives. Note a native child composites *above* the WebView2 surface, so
Beam can frame the picture but not overlay it. Invalid or zero handles are rejected at parse time
(exit 1).

**No UI of its own.** `stream`, `pair` and `quit` create no Qt window, overlay, or dialog. `pair`
and `quit` do their work silently and exit 0, or report an error and exit 1. `pair` is idempotent:
it pairs fresh every time, even when a cached record claims the host is already paired — so the
"already paired" stderr special-case in `engines.rs` becomes dead code (pairing an already-trusted
client succeeds and exits 0).

**Status lines on stdout.** Line-oriented, prefixed, flushed per line:

```text
beam: connecting             <- emitted when the stream command starts work
beam: first-frame            <- first video frame actually rendered; reveal now
beam: error <code> <text>    <- Beam renders this in its own words
beam: ended <reason>         <- reason is "clean" or "error"; process exits after
```

`<code>` is stable: 1 launch failed (host/app not found, validation), 2 connection stage failed,
3 session error or abnormal termination, 4 pairing failed, 5 quit failed. `<text>` is
human-readable and free to change. Exit code is 0 for a clean session, 1 otherwise.

**Logging.** All logs go to stderr, never stdout. When stderr is a pipe, only Error-level and above
is emitted so an undrained pipe cannot fill and block this process — but Beam should still drain
both pipes with reader threads (the `agent.rs` pattern) when it starts parsing status lines.

### What this replaces on the Beam side

- The `SetWinEventHook` + `EnumWindows` sweep + reparent (`embed.rs`) — pass `--embed-hwnd` instead.
- The `local connection on 48010` + 1.5 s reveal guess — reveal on `beam: first-frame`.
- The `already paired` stderr check in `engines.rs` — `pair` now exits 0 in that case.
- The expected exe name is now `beam-view.exe`: update `engines.rs` (`bundled()` call, the two
  system-path fallbacks become meaningless), `fetch-engines.mjs` (`expect`), and the
  `finds_both_engines_in_the_fetched_tree` test together.

## Rules that bind this side

**Stay a separate process.** Beam invokes this over a command line and that is the entire basis on
which Beam's own source stays proprietary. See the Licence section of `CLAUDE.md`.

**Do not share settings with an installed Moonlight.** `app/main.cpp` currently uses upstream's
organisation and application names, so the settings live in
`HKCU\Software\Moonlight Game Streaming Project\Moonlight` — the same key any Moonlight the user
installed writes to. Beam accumulated four stale host records there, all claiming `127.0.0.1`, and
pairing broke. The fork must own its own settings location.

**Errors belong to Beam.** When something fails, report it on stdout and exit. Do not put a dialog
on screen: the user is looking at Beam, and a message box from a program they have never heard of
is both confusing and the one thing this whole exercise is meant to prevent.

## Testing against Beam

1. Build a full deployable tree — `scripts\build-arch.bat Release x64`.
2. Copy `build\deploy-x64-release\` over Beam's bundled engine at
   `desktop\src-tauri\resources\engines\moonlight\`, or point `engines.rs` at your build.
3. Run Beam on two machines and connect. Loopback on one machine will not work: the client agent
   binds the same ports Sunshine listens on.

Once this repo publishes releases, `desktop/scripts/fetch-engines.mjs` in the Beam repo switches
from upstream's portable zip to ours — it already pins versions and verifies layout, so that is
close to a URL change. It also generates `THIRD-PARTY-NOTICE.txt`, which must then describe
`beam-view` as a *modified* Moonlight and link to this repo's source.
