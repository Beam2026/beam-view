# The patch set

Six changes, deliberately separate. Rebasing onto upstream is the permanent cost of this fork and
it scales with how tangled the patches are — so one concern per commit, and resist the urge to
"tidy while I'm in here". Unrelated cleanup makes future rebases hurt for no benefit.

The base is a pinned upstream commit on the `beam` branch. Status (August 2026):

| Patch | Status |
| --- | --- |
| P1 — Identity | **Implemented** — one commit on `beam` |
| P2 — `--embed-hwnd` | **Implemented, and no longer used by Beam** (2026-09-17). Four follow-up fixes were not enough; embedding was abandoned for a plain full-screen window. Kept because it works and is someone else's useful option — but read the retrospective below before reaching for it |
| P3 — No UI of its own | **Implemented** — status helper in `app/beamstatus.{h,cpp}`, headless runners in `app/cli/headless.{h,cpp}` |
| P4 — Headless `pair`/`quit` | **Implemented** — including idempotent re-pair |
| P5 — Baked-in defaults | Not implemented, deliberately (lowest value; the CLI is manageable) |
| P6 — Pairing key fix | **Implemented** (2026-09-28) — one line in `app/backend/nvpairingmanager.cpp`; a bug fix upstream shares, not a feature |

---

## Where Beam's code is, and what a sync will cost

Two kinds of change, with very different maintenance costs. **Only the second kind conflicts.**

**Files Beam added** — upstream has never heard of them, so they never conflict:

```text
app/beamstatus.{h,cpp}     the `beam:` status lines
app/cli/headless.{h,cpp}   the headless stream/pair/quit runners
docs/, CLAUDE.md           this documentation
```

**Files Beam modified** — every one of these is a conflict waiting for the next rebase, and the
line count is a fair proxy for how much it will hurt:

| File | Lines | What, and how exposed it is |
| --- | --- | --- |
| `app/streaming/session.cpp` | **172** | Embed window, geometry, focus. **The one to worry about** |
| `app/main.cpp` | 39 | Settings identity, routing to the headless runners |
| `app/cli/commandlineparser.cpp` | 23 | `--embed-hwnd` parsing |
| `app/cli/pair.cpp` | 22 | Idempotent re-pair |
| `app/backend/nvpairingmanager.cpp` | 4 | P6: pairing key taken as bytes, not a C string |
| `app/app.pro` | 19 | Target name, new sources |
| `app/streaming/session.h` | 12 | Embed members |
| `pacer/pacer.cpp` | **5** | One call to `BeamStatus::firstFrame()` |
| `cli/{listapps,quitstream,startstream}.cpp` | 2 each | Message wording |
| `app/qml.qrc`, `Moonlight.exe.manifest` | 3, 1 | Removed QML, description |

Every hook inside an upstream file is tagged, so the whole set is one command away:

```bash
grep -rn "// BEAM:" app/          # our hooks
git diff <base>..HEAD --stat      # authoritative, always current
```

**The rule that keeps this cheap: a hook should be one line calling into our own file, not logic
inlined into theirs.** `pacer.cpp` is the model — five lines, one call, and it will survive almost
any upstream change. `session.cpp` is the counter-example at 172 lines, and it is the file that will
fight every rebase. Much of it now serves `--embed-hwnd`, which Beam no longer uses; moving the rest
behind a helper in our own files is the single highest-value thing anyone could do for future sync
cost.

**Organising by directory does not help.** Moving the added files into `app/beam/` would change
nothing about conflicts — those files already never conflict — while adding path churn to `app.pro`,
an upstream file, and separating `cli/headless.cpp` from the `cli/` runners it belongs with. The
diff is the map, not the directory tree.

### Syncing with upstream

There is **no `upstream` remote configured**; add it before the first sync:

```bash
git remote add upstream https://github.com/moonlight-stream/moonlight-qt.git
git fetch upstream
git rebase --onto <new-tag> <current-base>     # base: 7cf8b46c, 2026-08-06
```

Patches are separate commits (P1..P4) on purpose, so a rebase replays them one at a time and a
conflict names which patch it belongs to.

**Check the deps pin first, before debugging anything else.** `setup-deps.ps1` is pinned to `v12`;
`v11` shipped an FFmpeg whose 8-bit D3D11VA decoding was broken — audio played, no picture ever
appeared, and it looked like a renderer bug. If decoding breaks after a sync, compare that tag
against upstream's before looking at any of our code.

---

## P1 — Identity

Make it Beam's program rather than a renamed Moonlight.

- `app/app.pro` — `TARGET`, `QMAKE_TARGET_COMPANY`, `QMAKE_TARGET_DESCRIPTION`,
  `QMAKE_TARGET_COPYRIGHT`, `QMAKE_TARGET_PRODUCT`, and `RC_ICONS = beam.ico` for the executable.
  The description is what Task Manager lists the process as, so it is just **"Beam stream"**; the
  "modified Moonlight" notice moved to the copyright field (2026-10-01).
- `app/main.cpp` — `setOrganizationName`, `setOrganizationDomain`, `setApplicationName`, and
  `setWindowIcon`.
- `app/streaming/session.cpp` — the window title, now just `Beam` (2026-10-06; it was
  `<computer> - Beam`, and the computer was the *host's* PC name).

**There are two icons, and only one of them is the exe’s.** `RC_ICONS` is what Explorer shows.
The *window* icon is set separately by `setWindowIcon`, and it is what Task Manager shows beneath a
process and what Alt+Tab shows. That one was still `res/moonlight.svg` until 2026-09-21, so anyone
who left the stream to reach their own desktop was shown the name this whole file exists to keep
them from learning. It is now `res/beam.png`.

And that was still not the stream window, which is the one a user actually reaches. That window
belongs to **SDL**, not Qt, and `session.cpp` renders an icon from `:/res/moonlight.svg` and applies it
with `SDL_SetWindowIcon` a few lines after creating it -- overriding the application icon entirely.
Setting it in `main.cpp` looked like the fix and changed nothing visible. Both are Beam’s now, and
`res/moonlight.svg` is no longer compiled into the binary at all — nothing read it once the window
stopped. The file stays on disk, because `app.pro` still installs it on Linux and
`scripts/generate-ico.sh` still rasterises it for upstream’s WiX bundle; neither is a path Beam
ships, and leaving both working is one less thing for a rebase to fight over.

**Neither Beam icon is generated here.** Both are byte-identical copies from the Beam repo, which
owns the artwork: `app/beam.ico` from `desktop/src-tauri/icons/icon.ico`, `app/res/beam.png` from
`desktop/src-tauri/icons/128x128.png`. Refresh them by copying — last done 2026-10-06, for the
redrawn prism (a straight beam, the spectrum starting inside it). Do **not** rasterise `beam.ico` out
of `res/beam.png` — that PNG is 128×128 and the committed `.ico` carries a 256×256 entry, so
“regenerating” it would quietly downgrade the icon Windows shows at the largest size.
`generate-ico.sh` says all of this in its header now; it used to say nothing and produce
`moonlight.ico`, which nothing in Beam reads.

**The `main.cpp` names are the important part, and not for branding.** They decide where `QSettings`
stores everything: today `HKCU\Software\Moonlight Game Streaming Project\Moonlight`, shared with any
Moonlight the user has installed. Beam accumulated four stale host records there — all claiming
`127.0.0.1`, each with a certificate from a different Sunshine — and pairing failed with "Computer
… has not been paired". Owning a private settings location fixes that class of bug at the root.

This also fixes the one tell that external window-hiding could never cover: Task Manager showing
`Moonlight.exe`. Renaming the *build target* is not a modification of the program's behaviour, so
it carries no extra obligation beyond the modification notice already required.

---

## P2 — `--embed-hwnd <handle>`

Create the stream window as a `WS_CHILD` of a caller-supplied HWND, at creation.

- `app/cli/commandlineparser.cpp` — add the option to `StreamCommandLineParser`.
- `app/streaming/session.cpp` — apply it at `SDL_CreateWindow`, via `SDL_CreateWindowFrom` or by
  setting the parent before the window is shown.

**This was believed to be the patch that justified the fork. It was not, and that is the single most
expensive mistake in this project's history.** It replaced a WinEvent hook and an 8 ms polling sweep
with one command-line argument, which was a real simplification of *that* code — but it bought a
harder problem than the one it solved. See the retrospective at the end of this section.

Worth knowing: a child window composites above the WebView2 surface Beam's UI is drawn on **only
once something raises it** — see the sections below, which is where that assumption cost a week.
Once raised, Beam can frame the picture but not overlay it.

### What the first two-machine test changed

The original patch assumed the embedding side would resize the child. It cannot. Two follow-up
commits fixed what that produced, and they are the reason this patch is three commits rather than
one:

- **Never let the parent move this window.** A cross-process window call is a synchronous message
  send, and the cross-process `SetParent` joins both input queues — so the two message loops can
  wait on each other forever. Observed as Beam hanging with a grey stage. The window now tracks its
  parent's client area from the SDL event loop, throttled to one check per 200 ms.
- **Reposition with raw `SetWindowPos`, pinned at (0,0).** `SDL_SetWindowSize` repositions using
  SDL's cached *screen* coordinates, which are wrong for a `WS_CHILD` whose position is
  parent-relative — the stream could land far inside its parent, rendering where nobody could see
  it. Every geometry correction is now logged, so an embedding failure is visible in the session log.

**Both fixes held, verified 2026-09-03** across two machines on separate networks: the picture
appeared inside Beam's window, and the child tracked about fifty sizes through a live window drag
without hanging.

**What the same session found was ours to *not* do.** The stage stayed black for a week afterwards,
and the cause was on the embedding side: nothing raised the host window's z-order, so Beam's own
page sat over a stream that was decoding perfectly. This program passes `SWP_NOZORDER` on purpose —
it must not fight its parent for stacking — which makes the raise entirely the embedder's job. That
is now stated as an obligation in [`beam-integration.md`](beam-integration.md); before, both
documents implied a native child is simply always on top, and it is not.

Worth remembering when this patch is next touched: every symptom pointed at this code — black
picture, embedded window, geometry logs — and none of the fault was here.

### Keyboard focus, 2026-09-14

The next session found the stream took the mouse and not one keystroke. Reparenting alone does not
bring focus, and SDL raises key events only for the window holding it — so `handleKeyEvent` was
never called. The embedder was not claiming focus either (it shows its host with `SW_SHOWNA` and
places it with `SWP_NOACTIVATE`, deliberately), so focus sat on the embedder's own UI.

`SetFocus(streamHwnd)` after `SDL_ShowWindow`, and it belongs here rather than in the embedder for
the same reason as everything else on this patch: a cross-process `SetFocus` is a synchronous
message send into our thread across already-joined input queues. We focus a window we own.

Note the shape this shares with the black screen: the mouse worked, the geometry was right, the logs
were clean, and the missing piece was a window-manager state nobody had set. **Reparenting a window
gives you none of size, z-order or focus — all three have to be asked for.** That is now the whole
of this patch's hard-won knowledge.

---

## P3 — No UI of its own

Remove this program's user interface. Not restyle it — remove it.

- `app/gui/CliPair.qml`, `CliStartStreamSegue.qml`, `CliQuitStreamSegue.qml` — the
  "Establishing connection to PC…" overlays.
- `app/cli/startstream.cpp`, `quitstream.cpp`, `listapps.cpp` — the "has not been paired" and
  similar messages.

In their place, emit stable line-oriented status on stdout:

```text
beam: connecting
beam: first-frame
beam: error <code> <text>
beam: ended <reason>
```

As implemented: error codes are 1 launch failed, 2 connection stage failed, 3 session
error/termination, 4 pairing failed, 5 quit failed; `<reason>` is `clean` or `error`; every line is
flushed immediately (stdout is a fully buffered pipe under Beam). `beam: first-frame` is emitted
from `Pacer::renderFrame()`, the one funnel every rendered frame passes through. When stderr is a
pipe, logging below Error level is suppressed so an undrained pipe buffer cannot fill up and block
this process — Beam should still drain both pipes once its reader lands.

`beam: first-frame` matters most. Beam currently infers the moment to reveal the window by watching
its own tunnel agent for the RTSP connection on port 48010 and then waiting 1.5 s — a guess biased
long, because revealing early leaks this program's connection screen. An explicit signal removes the
guess and the delay.

Error text goes to Beam, which renders it in its own words. Never a dialog: the user is looking at
Beam and has never heard of this program.

---

### The window cannot be hidden until the first frame, 2026-09-21

Tried and reverted the same evening, and worth recording so nobody tries it twice.

The stream window is created several hundred milliseconds before the first frame arrives — the
renderer has to exist before anything can be decoded into it — and for that gap it is an empty
black full-screen window on top of the embedder. Measured on a real session: the renderer was
created at 5.992 s and `beam: first-frame` came at 6.251 s.

The obvious fix is to create it with `SDL_WINDOW_HIDDEN` and show it on the first rendered frame.
It does not work: **nothing renders into a window that was never shown**, so the first frame never
arrives, the window is never shown, and the session waits on itself forever. Beam sat on "Waiting
for their screen…" with a live `beam-view.exe` and no window at all.

Two smaller traps on the way there, in case a later attempt gets further:

- The creation flag alone is undone by `SDL_SetWindowFullscreen`, which puts the window on screen
  on Windows.
- The embedded path *does* create it hidden and show it later, which is what made this look
  safe. It gets away with it because it shows the window during setup, long before any frame.

**Superseded by P7 (2026-10-05)**, which closes the gap here after all — with a DWM cloak rather
than a hidden window. The reasoning below is what was true before it.

So the gap belongs to whoever is *behind* this window, not to this program: the embedder should
stay in front until it sees `beam: first-frame`, which it already receives.

---

## P4 — Headless `pair` and `quit`

Both currently open a window and show the connection overlay — before the session and after it. On
a normal session the user sees this program's UI twice even when the stream itself is perfectly
hidden. They should do their work and exit silently.

Largely falls out of P3, but verify each independently: they are separate entry points
(`app/cli/pair.cpp`, `quitstream.cpp`) and each has its own QML segue.

---

## P5 — Beam's defaults

Bake in what Beam always passes — borderless, absolute mouse, quit-after, codec, frame pacing — so
the invocation stays short and behaviour cannot drift with a stray config file.

Lowest value of the five. Do it last, or skip it if the CLI stays manageable.

---

## P6 — The pairing key stopped at a zero byte, 2026-09-28

**Symptom.** About one Beam session in sixteen stuck on pairing. The guest's beam-view reported
`Incorrect PIN`, although both machines' logs showed the same PIN, and Sunshine's debug log showed
the host doing everything right: the PIN arrived after the request, and Sunshine answered
`getservercert`, `clientchallenge` and `serverchallengeresp` in order. The guest then sent
`/unpair` 32 ms later and never sent `clientpairingsecret`.

**Cause.** `NvPairingManager::pair` built the AES key as

```cpp
QByteArray aesKey = QCryptographicHash::hash(saltedPin, hashAlgo).constData();
```

`.constData()` hands back a `const char *`, and constructing a `QByteArray` from that reads it as a
C string: it stops at the first zero byte. Whenever `hash(salt + PIN)` has a `0x00` in its first 16
bytes, the key comes out short. AES then reads past it into whatever follows, so the key is wrong,
and the challenge response cannot match Sunshine's, which copies all 16 bytes
(`crypto::gen_aes_key` in Sunshine's `crypto.cpp`). The failure is decided by the random salt, so it
is intermittent: 1 − (255/256)¹⁶ ≈ 6.1% of attempts.

**Proof.** The failed attempt's salt (`134e89f6…`) with PIN 7367 hashes to `4010d32dce476100…`, a
zero at byte 7. The retry's salt hashes with no zero in the first 16 bytes.

**Fix.** Keep the hash as the `QByteArray` it already is, then truncate. One line, tagged
`// BEAM:`.

**Why nobody saw it upstream.** Upstream has the same line (on `master` at the pinned base).
Moonlight pairs once per device and a failed pair asks for a retry, which succeeds with a fresh
salt ~94% of the time. Beam pairs every session, which turned a rare nuisance into a regular one.
Worth reporting to moonlight-stream/moonlight-qt; if upstream fixes it, this patch disappears on the
next rebase.

---

## P7 — The stream window stays off screen until it has a picture, 2026-10-05

`app/streaming/session.cpp` (`cloakUntilFirstFrame`, `revealWindow`, `raiseWindow`), a hook in
`pacer.cpp`, `SDL_CODE_BEAM_REVEAL_WINDOW` / `SDL_CODE_BEAM_RAISE_WINDOW` in `video/decoder.h`, and
a small first-frame state in `beamstatus.cpp`.

The empty black window of the gap above is never on screen. On Windows, outside `--embed-hwnd`:

1. **Cloaked.** Created hidden, cloaked with `DwmSetWindowAttribute(DWMWA_CLOAK)`, then shown. A
   cloaked window is shown as far as SDL and the renderer are concerned, so frames render; DWM keeps
   it off the screen. (This is not the `SDL_WINDOW_HIDDEN` attempt above: that window was never
   *shown*, and nothing renders into one.)
2. **Revealed out of sight.** On the first rendered frame the pacer posts a reveal; the session moves
   the window to the bottom of the z-order and uncloaks it, so DWM composes it behind every other
   window.
3. **Raised with a picture.** After 150 ms and at least two more frames the pacer posts a raise (a
   1 s timer does too, in case frames stop); the session brings the window to the front, and only
   then is `beam: first-frame` sent.

**Why three steps, measured on 2026-10-05.** The first version uncloaked on the first frame and
printed `first-frame` from the render thread *before* the uncloak had happened, so Beam dropped its
loading screen onto nothing: a black blink. Fixing that order still blinked — 20 ms between uncloak
and `first-frame` — because a newly uncloaked window presents black until DWM has composed real
frames into it. Before the cloak the window had ~260 ms behind Beam's cover and never blinked;
composing at the bottom of the z-order gives it that time where nobody can see it, and also covers
a user who switched to another app while the stream connected.

`beam: first-frame` therefore means **the picture is on screen**, not merely rendered. Without a
cloak (it failed, or not Windows) the first rendered frame prints it, as before. If SDL recreates
the window (only the SDL renderer path can), the new one is not cloaked and the old gap returns for
that path alone.

---

## P8 — No Discord integration, 2026-10-05

`app/app.pro` no longer adds `discord-rpc` on Windows, so `HAVE_DISCORD` is undefined and
`RichPresenceManager` compiles to nothing; `scripts/build-arch.bat` drops `discord-rpc.dll`, which
the copy-every-prebuilt-DLL step would otherwise still ship.

Upstream reports every stream to Discord under **Moonlight's own Discord application**, so a Beam
guest with Discord open appeared to their friends as *playing Moonlight, "Streaming Desktop"* —
seen in a Beam session log as `Discord integration ready for user: …`. A privacy leak, and
Moonlight's name on screen. Defaulting the `richpresence` setting to false would not have been
enough: the registry already holds `true` on any machine that has run beam-view.

## P9 — Say the stream is ending the moment the user ends it, 2026-10-05

`app/streaming/session.cpp` (the `SDL_QUIT` case) and `BeamStatus::ending()` in `beamstatus.cpp`.

On the quit combo — anything that raises `SDL_QUIT` — this program prints `beam: ending` and hides
its window *before* the polite shutdown that follows. That shutdown took 3.4 s on 2026-10-05: 2.2 s
waiting for the host to acknowledge the control stream's disconnect, 0.8 s on the `--quit-after`
request, and the rest exiting — all of it with a frozen stream on screen and Beam waiting for this
process to exit before it could come back. Beam now ends the session on `ending` and kills this
process, as its own End button always did; the host closes its own side locally either way. Measured
after: 86 ms from the quit combo to Beam's session end.

## P10 — Every pairing request names its Beam session, 2026-10-06

`NvPairingManager::pairArguments()` in `app/backend/nvpairingmanager.cpp`, set from `pair
--beam-session <id>` in `app/main.cpp`.

Moonlight identifies itself to the host with the same `uniqueid` (`0123456789ABCDEF`) and
`devicename=roth` on every request, so nothing in a pairing request says which session it is for.
Sunshine's `POST /api/pin` could therefore only hand a PIN to whichever request happened to be
waiting -- and a request left waiting by a cancelled session took the next session's PIN. Beam
worked around that from outside for weeks (see Beam's `CLAUDE.md`).

With `--beam-session`, all five pairing requests carry `beamid=<id>`. The host's Sunshine is
beam-share, Beam's fork, which matches the request to the PIN its host approved for that session
(`POST /api/beam/pairing`) -- even when the host approved it first, so the request is answered on
arrival instead of parked. Without the flag nothing changes, and an upstream Sunshine ignores the
extra argument.

## P11 — Every fallback is said, 2026-10-06

`BeamStatus::warning` in `app/beamstatus.cpp`, called from `Session::emitLaunchWarning` and after the
stream's decoder is chosen in `app/streaming/session.cpp`.

Moonlight decides a fallback in a dozen places -- a codec the host cannot encode, surround the audio
device cannot play, HDR or 4:4:4 the GPU cannot decode -- and queues a warning for a dialog. Headless
there is no dialog, so each fallback happened with no word to anyone: an HEVC request streamed as
H.264 on 2026-10-05 and nothing said so. Every launch warning is now also a `beam: warning <text>`
line, whatever the user's warnings preference, and Beam shows and logs it.

One fallback Moonlight never warns about is added: Auto settling on software decoding, which is what
a GPU that cannot decode the codec gets. On 2026-10-06 an Intel Arc decoding HEVC in software could
not keep up at 80 Mbps, and only the decoder's own log said why.

## P12 — A host trusted by its certificate, without pairing, 2026-10-06

`identity` in `app/main.cpp` and `app/cli/commandlineparser.cpp`; `--server-cert` there and in
`ComputerManager::setPinnedServerCert` (`app/backend/computermanager.cpp`), read by its constructor,
`saveHosts`, `PendingAddTask` and `ComputerSeeker` (`app/backend/computerseeker.cpp`).

Pairing exists to swap two certificates under a PIN. Beam already has a channel both sides trust --
its own signalling, between two signed-in users -- so it swaps them there instead (Beam's backlog
C5, beam-share's S6). `beam-view identity` prints this install's client certificate as
`beam: identity <base64 of PEM>`; the host's Sunshine trusts it for the session. `stream
--server-cert <base64 of PEM>` pins the host's certificate for this process, in place of the one
pairing would have saved, so the stream needs no `pair` at all. Only public certificates cross:
each side still proves on every TLS connection that it holds its private key.

Two things a pinned stream needs that a paired one got for free:

- **Saved hosts are neither loaded nor saved.** Through the tunnel every host is `127.0.0.1`, so
  saved records are earlier sessions' hosts at the same address. Each one polled it with its own
  stale certificate -- "Found unexpected PC" over and over -- and, on this PC with eight of them,
  the second stream to the same host crashed inside Windows' `ncrypt.dll` in two runs of five. With
  none loaded, six streams in a row ran clean.
- **The seeker waits for the app list.** A host added this way is paired the moment it is added,
  before its poller has run; finding it stops polling, so without waiting there would never be an
  app list, and the launch failed as "Failed to find application Desktop".

Checked live against beam-share with S6: streams to a first frame with no `pair`; pinning the wrong
certificate fails as not paired; after the session is cancelled it is refused.

## P13 — The gamepad subsystem starts once, 2026-10-06

`SdlInputHandler::findUnmappedGamepads` in `app/streaming/input/gamepad.cpp`, called from
`Session::start` (`app/streaming/session.cpp`).

`validateLaunch` checked for unmapped gamepads by starting SDL's gamepad subsystem, loading the
mapping database and shutting it all down again -- and the input handler then started it again for
the stream, a moment later and before the launch request. About 0.2 s of every launch on the
laptop measured on 2026-10-06. The check now runs on the input handler's own subsystem, right after
it starts. The warning is the same and still reaches Beam as a `beam: warning`.

## P14 — reverted, 2026-10-07

It made the stream's first frames wait for the first decoder rather than be dropped, and skipped
that decoder's keyframe request, on the strength of one session where the opening keyframe arrived
before the decoder existed. Six sessions with it said otherwise: the decoder was ready before the
first packet anyway, first packet to picture stayed at 0.35 s, and in one session the opening
keyframe did not arrive whole -- with no request to recover, the picture waited 1.1 s for the next.
The request is what recovers that, so it stays. The number P14 is not reused.

---

## When to stop

If this list grows well past five patches, or a rebase starts taking real work rather than an
afternoon, that is the signal to reconsider. The alternative is owning the pipeline outright —
Desktop Duplication for capture, NVENC/AMF/QSV to encode, Media Foundation or D3D11VA to decode,
rendering into Beam's own swapchain. Complete control, no fork to rebase, and Beam already owns the
hard part: the transport. (This used to read "no GPL anywhere" as though that were the prize. Beam
is GPL-3 itself, so it is not one — the prize is the maintenance, which is the argument that still
holds.)

That is months of work and years behind Sunshine and Moonlight on tuning — adaptive bitrate, FEC,
jitter buffering, HDR. It is the right destination and the wrong starting point. This fork is how
you get most of the benefit now; revisit when the maintenance cost says otherwise.

---

## Retrospective on P2: why Beam stopped embedding, 2026-09-17

Beam no longer passes `--embed-hwnd`. The stream is a plain borderless full-screen window
(`--display-mode borderless`) and the embedder hides its own window for the session instead.

The patch itself works. What it could not do is stop being a `WS_CHILD`, and four separate bugs came
from that, each found by a two-machine test, each fixed, and each followed by another:

1. **Black screen for a week.** A native child is above the WebView only while something keeps
   raising its z-order. Nothing did. Every symptom pointed at the video pipeline; none of the fault
   was there.
2. **Deadlock.** The cross-process `SetParent` attaches both threads' input queues, so a synchronous
   window call from the embedder froze both message loops.
3. **No keyboard.** A `WS_CHILD` does not take focus from a click the way a top-level window does.
   `SetFocus` on creation fixed the start of a session; the first time focus moved away — the local
   Start menu was enough — it was gone for good, including the combo that ends the session.
4. **No cursor, and a letterboxed picture.** The window can never be SDL-fullscreen, so the stream
   was fitted into whatever the parent measured, with the bars as dead zones.

**The lesson is about the shape of the evidence, not the Win32 details.** Four bugs in a row, each
individually plausible, each with a local fix that worked — and the common cause was the
architecture, not any of them. A run of unrelated-looking bugs in one area is itself the signal.
Every remote-desktop client that does this well uses a separate top-level window; the fork spent
weeks discovering why.

Keep the patch. It is correct, it costs nothing while unused, and embedding is genuinely the right
answer for a caller that needs the stream inside its own UI. Just do not reach for it to avoid a
second window without pricing in that list.
