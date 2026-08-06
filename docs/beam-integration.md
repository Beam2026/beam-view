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

## What Beam invokes today

```powershell
beam-view.exe pair  127.0.0.1 --pin 1234
beam-view.exe stream 127.0.0.1 "Desktop" --display-mode borderless --absolute-mouse enable --quit-after enable
beam-view.exe quit  127.0.0.1
```

Pairing is automatic and invisible: Beam generates the PIN, sends it to the host over its own
signalling channel, and the host's copy of Beam approves it against Sunshine. Nobody types a PIN.

Ports carried by the tunnel — TCP 47984 (HTTPS/pairing), 47989 (HTTP), 48010 (RTSP); UDP 47998
(video), 47999 (control), 48000 (audio).

## What Beam needs that does not exist yet

These are the reasons the fork exists. Full breakdown in [`patches.md`](patches.md).

**A window Beam can host.** `--embed-hwnd <handle>` should make the stream window a `WS_CHILD` of
the given HWND at creation. Beam currently achieves this from outside with a `SetWinEventHook`, an
8 ms `EnumWindows` sweep and an in-place reparent — every part of which is a workaround for not
being able to ask.

**Silence until there is a picture.** Beam shows its own loading screen while this program starts,
so nothing of this program's UI should ever reach the screen: no title bar, no taskbar entry, no
"Establishing connection to PC…" overlay, no dialogs. `pair` and `quit` included — both open a
window today, one before the session and one after, which is precisely what users notice.

**A status stream on stdout.** Beam needs to know when to reveal the window, and currently guesses:
it watches its own tunnel agent for `local connection on 48010` (the RTSP handshake) and then waits
1.5 s. Machine-readable output replaces the guess:

```text
beam: connecting
beam: first-frame            <- reveal now; the picture is real
beam: error <code> <text>    <- Beam renders this in its own words
beam: ended <reason>
```

Keep it line-oriented, prefixed, and stable. Beam parses it.

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
