# Plan: beam-view becomes Beam's session surface

What beam-view is growing into, in steps that each close on their own. One step is open at a time;
a step is done when its gate passes and [`patches.md`](patches.md) records it -- not when the code
is written. Agreed 2026-10-12.

## Where it ends up

- **No Moonlight front end anyone can reach.** beam-view is driven only by its command line at
  start and by **calls** on stdin while a stream runs. Its status goes out as `beam:` lines, as it
  does today ([`beam-integration.md`](beam-integration.md)).
- **Every setting comes from Beam.** Nothing a stream does is read from beam-view's registry.
- **A Beam button over the stream.** It opens a list of everything that can change during a
  session: the performance graphs, window or full screen, the stream's sound, hiding the button,
  and Disconnect -- with the live numbers: ping, network, encode and decode latency.
- **Warnings as icons with a short note**, in Beam's look, instead of Moonlight's white corner text:
  the connection, the bitrate, the graphics card, the sound.

In Beam's roadmap this is 2.2's third increment (the settings), 2.4's last on-screen tell
(Moonlight's overlays), and Step 3.1's open question -- **the in-session panel is drawn by
beam-view**, the second of the three options there. Beam's `docs/roadmap.md` should say so when
step 5 starts.

## Rules for every step

These keep the cost of syncing with upstream where it is today.

1. **Delete nothing upstream wrote.** Not files, not functions, not branches -- including
   Moonlight's QML front end and every upstream branch on `origin`. Code Beam stops using is made
   unreachable or left out of the build, never removed, so a sync never meets a modify/delete
   conflict.
2. **New behaviour lives in new files**, under `app/beam/`. Upstream has never heard of them, so they
   never conflict. (Beam's existing files -- `beamstatus`, `cli/headless` -- stay where they are;
   [`patches.md`](patches.md) says why moving them would only add churn.) A change to an upstream file is the smallest hook that calls into `app/beam/`,
   marked `// BEAM:` like the existing ones.
3. **One step, one patch** (P15 onwards), each with an entry in [`patches.md`](patches.md) and its
   lines added to the "Files Beam modified" table there.
4. **The contract changes on both sides together.** Every new flag, call or `beam:` line goes into
   [`beam-integration.md`](beam-integration.md) and into Beam's `desktop/src-tauri/src/viewer.rs` in
   the same piece of work; `commandlineparser.cpp` is checked by a Beam test.
5. **Nothing may block the stream.** Calls arrive on a reader thread and are posted to the
   session's SDL event loop; nothing touches SDL or the renderer from any other thread.
6. **Watch the size of the fork.** [`patches.md`](patches.md) ("When to stop") sets the point at which
   owning the pipeline beats rebasing it. Each step's patch entry says how many upstream lines it
   touched.

**Before step 1:** `perf/launch-time` holds one commit not on `beam` (`570cc114`, Beam's icon from
its one drawing). Merge it into `beam`, and branch each step from `beam`.

---

## Step 1 -- No front end to reach (P15) -- **1a and 1b done 2026-10-12**

Today, `beam-view.exe` started with no command, or double-clicked, opens Moonlight's QML window
(`NormalStartRequested` in `app/main.cpp` loads `qrc:/gui/main.qml`).

- **1a. Unreachable.** `NormalStartRequested` and anything else that would load QML prints
  `beam: error 1 beam-view is started by Beam` and exits 1. `engine.load` is never reached.
  The QML files, `app/gui/` and `qml.qrc` stay in the tree untouched.
- **1b. Audit every other way onto the screen.** Any `SDL_ShowSimpleMessageBox`, Qt dialog or
  QML-only error path that the headless runners can still hit becomes a `beam:` line.
- **1c. Out of the build, optionally.** A `beam_headless` switch in `app/app.pro` that leaves out
  `qml.qrc`, `app/gui/*.cpp` and the Qt Quick modules -- added as new conditional blocks, nothing
  deleted. Worth it only if it measurably shrinks the deploy or the start time; measure first.
  Not small: `session.cpp` and `streamingpreferences` use Qt Quick outside `app/gui/` (see P15).
  **Not done.**

**Gate:** double-clicking `beam-view.exe`, or running it with no arguments or an unknown command,
puts nothing on screen and prints one `beam: error`. `identity`, `pair`, `quit` and `stream` behave
as before.

## Step 2 -- Calls while a stream runs (P16) -- **passed live 2026-10-13** (`Ctrl+Alt+Shift+M`'s report
still to be seen)

Beam started beam-view with stdin closed (`viewer.rs`, `Stdio::null()`). The stream's stdin is now
a pipe, and Beam writes one call per line:

```text
stats on|off            the performance overlay (the graphs, once step 6 lands)
window full|windowed    full screen (borderless) or a window
sound on|off            the stream's audio on this PC
mouse lock|free         the cursor kept inside the stream window, or free to leave it
keyboard lock|free      Windows, Alt+Tab and Alt+F4 sent to the host, or kept on this PC
disconnect              end the stream; same path as Ctrl+Alt+Shift+Q (beam: ending)
```

`button show|hide` and `menu open|close` come with step 5, which builds what they act on.

- `app/beam/control.{h,cpp}` (`BeamSessionControl`): a reader thread parses lines and posts an
  SDL user event; the session's event loop applies it. The menu in step 5 calls the same actions.
- Every change, whoever made it -- a call, `Ctrl+Alt+Shift+S` or `Ctrl+Alt+Shift+X` -- is
  reported: `beam: state sound off`, `beam: state window windowed`.
- `sound off` is its own switch, not the session's mute, which follows focus when "mute when not
  in front" is on and would undo it.
- `window` uses upstream's toggle, which on Windows rebuilds the decoder: the picture blinks once.
- An unknown call is `beam: warning` and is otherwise ignored. A closed stdin changes nothing.
- Beam side: `viewer.rs` pipes the stream's stdin and has a `viewer_call` command that writes only
  the calls above. Nothing in Beam's UI uses it until step 5; until then it is tried from the
  devtools console of a development build.

**Gate:** each call changes a live stream without interrupting it, and is answered with its
`beam: state`; `disconnect` ends the session as cleanly as the key combination does.

## Step 3 -- The settings that are left (P17)

Beam's roadmap 2.2, third increment, and the rows marked *Later* in Beam's `docs/desktop/settings.md`.

- **Flags** for what still comes only from the registry: `--renderer auto|d3d11|vulkan`,
  `--connection-warnings`/`--no-`, `--gamepad-mouse`/`--no-`, `--mdns`/`--no-` (today written to the
  registry before pairing).
- **A stream ignores the registry.** `stream` builds its `StreamingPreferences` from defaults plus
  flags and never reads `HKCU\Software\Beam\beam-view`, so nothing a program writes there can
  change a Beam stream.
- Beam side: the settings in `settings.rs` and `types.ts`, rows in the Client tab, `stream_args`,
  and the parser check.

**Gate (Beam's `settings.md`):** a stream started with that registry key emptied, and one with
every value flipped, behave the same; each new setting checked once on two machines.

## Step 4 -- Warnings as icons (P18)

Moonlight's status overlay (`OverlayStatusUpdate`, "Slow connection to PC / Reduce your bitrate",
`session.cpp`) is white text in a corner, in Moonlight's words.

- **One Beam overlay.** A new overlay type that is a whole-window transparent surface Beam composes
  itself (`app/beam/overlay.{h,cpp}`), so each renderer needs one new case at (0,0), full size,
  rather than one per element. D3D11 first (`d3d11va.cpp`), then Vulkan (`plvk.cpp`); other
  renderers keep Moonlight's text.
- **Notices, each an icon and a few words:** connection poor (`CONN_STATUS_POOR`, network drops),
  bitrate too high for the link, decoder falling behind (decode time over the frame time, pacer
  drops), software decoding, sound. Icons are Beam's Tabler set, rasterised once with Qt SVG, which
  is already linked.
- Each notice also goes to Beam as `beam: notice <kind> on|off`, so the log has it.
- `--connection-warnings` (step 3) turns them off.

**Gate:** each notice can be caused on purpose (a low bitrate cap, software decoding, a throttled
link) and shows as its icon and words, and nothing of Moonlight's text appears on the D3D11 or
Vulkan renderer.

## Step 5 -- The Beam button and its list (P19)

Beam's roadmap 3.1.

- **The button**: Beam's mark, small, at an edge of the stream. It is shown only while the mouse is
  free (`Ctrl+Alt+Shift+Z` ungrabs it today), so it never sits under a game's cursor.
- **The list it opens**:
  - **Performance** -- the graphs (step 6) on or off
  - **Window** -- full screen or windowed
  - **Sound** -- on or off
  - **Hide button** -- until the key combination brings it back
  - **Disconnect** -- red, last
  - and, at its head, the live numbers: ping, network latency, host encode, decode, frame rate.
- **A key combination** opens the list even with the button hidden: Moonlight's
  `Ctrl+Alt+Shift+` set has free letters; pick one with no clash in `keyboard.cpp`.
- **Clicks on the button and the list never reach the host.** While the list is open, the mouse
  and keyboard stay with beam-view; the session's input handlers drop them before sending.
- Drawn into the Beam overlay from step 4. Every action is `SessionControl` from step 2.

**Gate:** the button and the list open and close over a running stream without a stutter; every
item works and is reported to Beam; Disconnect ends the session cleanly; the host receives none of
the clicks; and with the button hidden, the key combination still reaches the list.

## Step 6 -- Performance as graphs (P20)

Today `Ctrl+Alt+Shift+S` shows Moonlight's text block (`ffmpeg.cpp`, `VIDEO_STATS`).

- **Rolling graphs**, about the last ten seconds each, with the current value beside them:
  - ping, from `lastRtt` and `lastRttVariance`
  - host encode, from the host processing latency
  - network and reassembly
  - decode
  - render and frame time
  - frame rate, received against rendered
  - bitrate
  - dropped frames
- Drawn into the Beam overlay with its own small line drawing (`app/beam/graphs.{h,cpp}`); no new
  library.
- The same numbers go to Beam every few seconds as `beam: stats …`, so a session's log carries them
  whether or not the graphs were open.

**Gate:** the graphs update live on a two-machine session, cost no measurable frame time with the
overlay on (render time in the graphs themselves, before and after), and match the text overlay's
numbers they replace.

## Open questions, decided when their step starts

- Step 1c: is leaving Qt Quick out of the build worth a second build configuration?
- Step 5: where the button sits, and whether it can be dragged; which key combination.
- Step 5: windowed mode mid-session on `borderless` -- does SDL switch cleanly, or does the
  decoder need rebuilding (a visible blink)?
- Steps 4-6: whether `dxva2`, the fallback renderer, gets the Beam overlay or keeps Moonlight's.
