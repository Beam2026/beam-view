# How Beam drives this program

Beam (`Beam`, GPL-3 — github.com/Beam2026/BEAM, normally checked out beside this repo) runs
`beam-view` as a child process. This file is the
contract between them. **It is defined in two places with nothing enforcing agreement** — here, and
`desktop/src-tauri/src/viewer.rs` in the Beam repo. Change one, change the other.

## The shape of a session

Beam does not stream. It builds an encrypted peer-to-peer tunnel between two machines, so each side
talks to `127.0.0.1` and neither engine knows the internet was crossed:

```text
beam-view → 127.0.0.1:ports → client-agent ══P2P══ host-agent → 127.0.0.1:ports → Sunshine
```

Every address this program is given is therefore **always `127.0.0.1`**, on both machines, in every
session — and always **with a port**, `127.0.0.1:48989`.

The port matters and is not decoration. Beam runs its Sunshine on its own base, 48989, rather than
GameStream's 47989, so that a Sunshine the user installed for themselves cannot collide with it.
This program is told that base in the host argument; `addNewHostManually` parses it with `QUrl` and
honours `url.port(DEFAULT_HTTP_PORT)`, and `ComputerSeeker::matchComputer` compares against
`NvAddress::toString()`, which renders `host:port`. No `--port` flag was needed — that was
established by shipping the form with the port already in use and confirming a session behaved
exactly as before.

That has a consequence worth internalising: **`127.0.0.1` is a different physical machine every
time.** Any state cached against that address — pairing certificates most of all — describes
whoever was on the other end of the tunnel last session, not this one. This is not theoretical; it
is the bug that made Moonlight refuse to stream with "Computer Hadi has not been paired", because
the cached record for `127.0.0.1` had been issued by a different Sunshine.

Beam therefore pairs on **every** session and never trusts a cached answer. `pair` must stay cheap,
repeatable, and safe to run against an already-paired host.

## What Beam invokes

```powershell
beam-view.exe pair   127.0.0.1:48989 --pin 1234
beam-view.exe stream 127.0.0.1:48989 "Desktop" --display-mode borderless --resolution 1920x1080 --absolute-mouse --capture-system-keys always --audio-on-host --quit-after
beam-view.exe quit   127.0.0.1:48989
```

**Which of these take a value, and which do not, is not cosmetic.** `--display-mode`,
`--resolution` and `--capture-system-keys` are value or choice options. `--absolute-mouse`,
`--audio-on-host` and `--quit-after` are *toggles*: the parser registers each as a bare `--name`
alongside a `--no-name` and reads them by presence. Beam used to write `--absolute-mouse enable`,
and `enable` was not a value but a stray **positional** -- landing after `stream`, the host and the
app name, where `StreamCommandLineParser` reads indices 0-2 and silently discards the rest. It
worked only because it was thrown away. Corrected 2026-09-19.

`--display-mode borderless` is full-screen-desktop: the stream owns the screen for the session, and
Beam hides its own window rather than hosting the picture inside it. `--resolution` is the
**client's** own screen size, since that is what the picture fills; the host's Sunshine is
configured to resize its capture to match. `--capture-system-keys always` is required, not optional:
without it the Windows key opens the *client's* Start menu and takes the keyboard with it, and
`fullscreen` mode will not do, because it is gated on `SDL_WINDOW_FULLSCREEN`.

**`--audio-on-host` is a server hint, despite the name, and Beam needs it on.** It is read in one
place -- `nvhttp.cpp`, where it sets `localAudioPlayMode=1` on the launch request -- and no code
path alters what this side receives or plays. It does *not* move audio to the host instead of the
client: both get it. What it changes is Sunshine, which on `localAudioPlayMode=0` switches the
host's default playback device to a virtual sink in order to capture, silencing the person sharing
their screen for the whole session. Without it a host on a call cannot hear the conversation about
what is on their own screen.

**`--embed-hwnd` is deliberately not used.** It still works; Beam abandoned it after four separate
bugs traced back to being a `WS_CHILD`. The retrospective in [`patches.md`](patches.md) is required
reading before anyone reaches for it again.

Pairing is automatic and invisible: Beam generates the PIN, sends it to the host over its own
signalling channel, and the host's copy of Beam approves it against Sunshine. Nobody types a PIN.

Ports carried by the tunnel, all derived from Beam's base of 48989 — TCP 48984 (HTTPS/pairing),
48989 (HTTP), 49010 (RTSP); UDP 48998 (video), 48999 (control), 49000 (audio). Sunshine's web UI at
48990 is host-local and is not tunnelled.

Both ends of the tunnel use the same numbers, so the client agent this program talks to listens on
exactly the ports the host's Sunshine advertises in `serverinfo`. That is why the base can move at
all: if only one side moved, this program would follow `HttpsPort` to a port nothing local was
listening on.

## The contract as implemented

All of this exists on the `beam` branch. Full breakdown in [`patches.md`](patches.md).

**`--embed-hwnd <handle>`** (Windows only, decimal `u64`) — *supported, but no longer used by Beam;
see the retrospective in [`patches.md`](patches.md)*. The stream window is created hidden,
restyled to `WS_CHILD`, reparented into the given HWND and sized to fill its client area before it
is ever shown — it never exists as a top-level window. Invalid or zero handles are rejected at parse
time (exit 1).

> **The embedding side must never touch this window.** Not `MoveWindow`, not `SetWindowPos`, not
> `SetWindowLong` — nothing. A cross-process window call is a **synchronous message send**, and the
> cross-process `SetParent` has already joined the two input queues, so both message loops can end
> up waiting on each other forever. That is not theoretical: it is what the first two-machine embed
> test produced, as Beam hanging with a grey stage.
>
> **Beam moves only the host window it owns.** This program watches its parent's client area from
> its own event loop (throttled to one check per 200 ms) and repositions itself with a local
> `SetWindowPos`, pinned at (0,0). Resizing Beam's stage is therefore the entire protocol — the
> child follows on its own, with a lag of up to 200 ms.
>
> **Status: verified end to end, 2026-09-03.** A picture appeared inside Beam's window across two
> machines on separate networks, and the child tracked about fifty sizes through a live window drag.
> The deadlock fix and the geometry pinning both held.

**Two things the embedding side must do, and neither is this program's job.**

**Size the host before starting this program, not when the first frame arrives.** The decoder and
the D3D11 swapchain are built against whatever the embed window measures at start-up, and
`beam: first-frame` is emitted from the *present* path — so a host sized on first-frame was sized
after the stream had already been built for it. Beam parks the host at full size just off its client
area before launching, and the reveal is then a pure move at the same size, which this program can
ignore. A host that is still 1×1 when this program starts will have its stream built for one pixel.

**Raise the host, on every placement.** A native child composites above the WebView2 surface **only
if its z-order says so**. This program deliberately passes `SWP_NOZORDER` on both its embed and its
resize, because it must not fight its parent for stacking — so the embedder owns that entirely. Beam
uses `SetWindowPos` with `HWND_TOP` and `SWP_NOACTIVATE` every time it places the host; once is not
enough, since WebView2's own child window raises itself on focus or repaint.

Getting this wrong is quiet and expensive: the stream decodes, `beam: first-frame` fires, the
geometry logs look perfect, and the user sees the embedder's own page. It cost a week. Whatever the
embedder paints in the stage rectangle should therefore not be black, so that "covered" and "never
arrived" can be told apart by looking.

With the z-order set, the video is above the page, so the embedder can frame the picture but not
overlay it — in-session controls have to live outside the stage rectangle.

**And one thing the embedder must *not* do: fight this program for keyboard focus.** On embedding,
this program calls `SetFocus` on its own window. It has to: mouse messages go to the window under
the cursor, but keyboard messages go to the focused window, and SDL only raises key events for a
window holding input focus. Without it the stream takes the mouse and not one keystroke — and the
on-screen keyboard fails too, since it synthesises into the focused window.

That is deliberately *this* program's job, not the embedder's. A cross-process `SetFocus` would be a
synchronous message send into our thread, and the cross-process `SetParent` has already joined the
two input queues — the pairing that produced the original deadlock. We focus a window we own, which
is safe, and the joined queues are what make it possible at all.

The consequence the embedder should plan for: while a stream is live the keyboard belongs to the
remote machine, so the embedder's own UI cannot have it. An in-app key handler will never fire —
use a system-wide hotkey (`RegisterHotKey`) for anything that must work during a session.
`Ctrl+Alt+Shift+Q` (quit) and `Ctrl+Alt+Shift+M` (toggle mouse mode) keep working throughout,
because they are ours.

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

### What this replaced on the Beam side

All of this has landed; it is kept as the record of what the contract was worth.

- A `SetWinEventHook` + `EnumWindows` sweep that hunted for this program's window and reparented
  it. Beam tried `--embed-hwnd` next and abandoned that too — it now hides its own window and
  lets this one own the screen.
- Guessing the moment to reveal the stream by watching Beam's own tunnel agent for the RTSP
  connection and then waiting 1.5 s. Replaced by `beam: first-frame`.
- An `already paired` check against this program's stderr. `pair` exits 0 in that case.
- The exe name: `moonlight.exe` became `beam-view.exe` across `engines.rs`, `fetch-engines.mjs`
  and its test, and the bundled tree moved to `resources/engines/beam-view/`.

## Rules that bind this side

**Stay a separate process.** This used to be a licence argument. It is not one any more — Beam is
GPL-3 as well, so linking would threaten nothing. What survives is the engineering: separate
processes buy crash isolation, let either engine be updated on its own, and keep this fork
rebaseable against upstream. Beam may revisit that trade deliberately, with a measurement behind
it; the CLI is the default, not a wall.

**Do not share settings with an installed Moonlight.** `app/main.cpp` sets the organisation to
`Beam` and the application to `beam-view`, so settings live under `HKCU\Software\Beam\beam-view`
rather than upstream's `Moonlight Game Streaming Project\Moonlight`. That is not cosmetic: while
the two shared a key, this program accumulated four stale host records all claiming `127.0.0.1`
and pairing broke. Do not move it back.

**Errors belong to Beam.** When something fails, report it on stdout and exit. Do not put a dialog
on screen: the user is looking at Beam, and a message box from a program they have never heard of
is both confusing and the one thing this whole exercise is meant to prevent.

## Testing against Beam

1. Build a full deployable tree — `scripts\build-arch.bat Release x64`.
2. Copy `build\deploy-x64-release\` over Beam's bundled engine at
   `desktop\src-tauri\resources\engines\beam-view\`, or point `engines.rs` at your build.
3. Run Beam on two machines and connect. Loopback on one machine will not work: the client agent
   binds the same ports Sunshine listens on.

`desktop/scripts/fetch-engines.mjs` in the Beam repo already pulls this repo's release rather
than upstream's portable zip, pinned by tag, and verifies the layout it unpacks. It also generates
`THIRD-PARTY-NOTICE.txt`, which describes `beam-view` as a *modified* Moonlight and links to this
repo's source. A new release is a tag and a URL in that file.
