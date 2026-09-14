# beam-view

**This is a modified version of [Moonlight PC](https://moonlight-stream.org)
([moonlight-qt](https://github.com/moonlight-stream/moonlight-qt)), modified by Beam starting
August 2026.** It is not Moonlight, and problems with it should not be reported to the Moonlight
project.

beam-view is the streaming engine that [Beam](https://github.com/Beam2026) runs as a child process.
Beam is a remote-gaming application that builds an encrypted peer-to-peer tunnel between two
machines; this program is what displays the remote screen at the viewer's end.

## What is different from Moonlight

Moonlight is a complete application: it owns a window, draws its own menus and dialogs, and reports
problems on a screen it controls. Beam needs the opposite — an engine with no interface of its own,
so that everything the user sees belongs to Beam. That is what this fork provides.

- **Headless.** No window of its own, no overlays, no dialogs. Every session is driven by command
  line arguments and ends by exiting.
- **Embeddable.** `--embed-hwnd <handle>` makes the stream window a child of a window the host
  application owns. It is created hidden, restyled, reparented and sized before it is ever shown, so
  it never exists on screen as a top-level window.
- **Machine-readable status.** Progress and failures arrive as `beam:` lines on stdout —
  `connecting`, `first-frame`, `error <code> <text>`, `ended <reason>` — rather than as text on a
  screen. The host application decides what the user is told.
- **Repeatable pairing.** Every session pairs fresh, because the address is always `127.0.0.1` and
  that is a different physical machine every time.

Every modification is catalogued in [`docs/patches.md`](docs/patches.md), and each is a commit on
the `beam` branch against a pinned upstream base.

## How it is used

```powershell
beam-view.exe pair   127.0.0.1 --pin 1234
beam-view.exe stream 127.0.0.1 "Desktop" --embed-hwnd <handle> --resolution 1920x1080
beam-view.exe quit   127.0.0.1
```

The full contract — every flag, every status line, and the rules both sides must respect — is in
[`docs/beam-integration.md`](docs/beam-integration.md). It is defined in two places, here and in
Beam's own source, with nothing enforcing that they agree.

## Building

See [`docs/building.md`](docs/building.md). It covers two traps that a normal development machine
hits, so read it before starting rather than after.

## Licence

GPL-3.0, like Moonlight — see [LICENSE](LICENSE).

The complete corresponding source for every released binary is this repository. Beam distributes
compiled builds of this program alongside its own application, and runs it strictly as a separate
process over the command line interface described above; it is never linked in as a library.

## Upstream

Moonlight is developed by the [moonlight-stream](https://github.com/moonlight-stream) project, whose
work this fork depends on entirely. For the original client, its supported platforms, its mobile
versions and its community, see
[moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) and
[moonlight-stream.org](https://moonlight-stream.org).

Upstream's own README, build documentation and downloads describe the original application rather
than this fork, so they are not reproduced here.
