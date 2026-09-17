# The patch set

Five changes, deliberately separate. Rebasing onto upstream is the permanent cost of this fork and
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

---

## P1 — Identity

Make it Beam's program rather than a renamed Moonlight.

- `app/app.pro` — `TARGET`, `QMAKE_TARGET_COMPANY`, `QMAKE_TARGET_DESCRIPTION`,
  `QMAKE_TARGET_PRODUCT`; icon resources.
- `app/main.cpp` — `setOrganizationName`, `setOrganizationDomain`, `setApplicationName`.
- `app/streaming/session.cpp` — the window title, currently `<computer> - Moonlight`.

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

## When to stop

If this list grows well past five patches, or a rebase starts taking real work rather than an
afternoon, that is the signal to reconsider. The alternative is owning the pipeline outright —
Desktop Duplication for capture, NVENC/AMF/QSV to encode, Media Foundation or D3D11VA to decode,
rendering into Beam's own swapchain. No GPL anywhere, complete control, and Beam already owns the
hard part: the transport.

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
