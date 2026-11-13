# RuneLight

**RuneLite plugins on your phone.** RuneLight repacks the official Old School RuneScape Android app so it runs unmodified
[RuneLite](https://runelite.net) plugins natively on Android: overlays, ground-item prices, boss info, Plugin Hub
plugins, the lot.

RuneLite has never had a mobile client. RuneLight is an independent one. It hooks the official app's game thread through
a native library (`native/`) and embeds a Java plugin host (`host/`) that implements the RuneLite API on top of the game's
own state, on Android's runtime. Plugins run as they do on desktop, with no per-plugin rewrites.

> **Status: early (v0.1.x).** It works end to end against a private test server, and the API is largely implemented,
> but it is not a finished product. See [Progress](#progress).

## How it works

```
 official OSRS app (arm64)
        │  tools/repack.sh patches and repacks it
        v
 libosrsrl.so  ── hooks the game thread ──>  events + task queue
        │ JNI
        v
 Java plugin host (ART)
   ├─ RuneLite API implementation, backed by live game memory
   ├─ event bus, per-plugin Guice injectors, overlays, config
   └─ java.awt / javax.sound shims so desktop plugins link unchanged
```

Two design points:

- **No client talks to Jagex or RuneScape servers.** Every APK goes through a telemetry patch (`tools/repack.sh`), and
  the client is developed and tested against [OpenRune](https://github.com/OpenRune), an open-source OSRS server.
- **Core plugins** come from the bundled RuneLite client jar. **Plugin Hub plugins** are built from source
  (`host/plugins/hub/build.sh`) and converted for ART.

## Usage

Requirements: an arm64 Android device or emulator, `adb`, and a server to play on (see the OpenRune project).

**Install a release**

```sh
# get osrs-runelite.apk from the latest GitHub release
adb uninstall com.jagex.oldscape.android   # an installed official/Play copy has a different signer
adb install osrs-runelite.apk
```

The release is one universal APK (arm64-v8a only). The app is Jagex's; RuneLight's part is `libosrsrl.so`, which
holds the native hooks and the embedded plugin host.

**Build from source**

```sh
host/build.sh             # Java plugin host -> host.jar
ninja -C re/build         # native hooks -> libosrsrl.so
tools/repack.sh           # patch + repack the official APK -> re/out/osrs-runelite.apk
tools/repack.sh --install # ...and install it on the connected device
```

You supply the official APK (`base.apk` and `split_config.arm64_v8a.apk`, build 241.4) in the repo root. A correct build
prints 7 `telemetry:` lines, confirming the privacy patches applied.

**Measure API coverage**

```sh
ANDROID_SERIAL=<device> tools/api-probe.sh
```

This loads a probe plugin into the running app and reports how many RuneLite API methods behave correctly on device.

## Progress

Client goal: **RuneLite compatibility on mobile.**

| Measure | Status |
|---|---|
| RuneLite API methods working on device (api-probe) | **94.1%** (1178 / 1252) |
| Zone sub-protocols implemented | 15 / 15 |
| Core plugins running | 89 / 90 at last full run |
| Plugin Hub plugins running | 84 / 109 at last full run |
| Release | v0.1.2 |

Per-plugin status is in [docs/plugin-host.md](docs/plugin-host.md). The remaining API gaps are mostly rows the probe
cannot exercise without a specific game state, such as clan chat or world entities, plus a few engine-side setters.
The protocol spec ([docs/android-protocol.md](docs/android-protocol.md)) and offset map
([docs/offsets.md](docs/offsets.md)) are attached to every release.

## Roadmap

- Close the remaining API gaps, and exercise the rows the probe can't reach yet.
- More Plugin Hub plugins, prioritised by popularity.
- **GPU plugin and 117 HD on Android** (the original stretch goals; feasibility work is under way).
- Touch-first ergonomics for plugin UI.
- Verify against official mechanics, not just headline numbers.

## Contributing

Issues and PRs are welcome. To get oriented:

1. Read [docs/android-protocol.md](docs/android-protocol.md) for the protocol,
   [docs/offsets.md](docs/offsets.md) for reverse-engineered addresses and [docs/plugin-host.md](docs/plugin-host.md)
   for plugin status.
2. Run the three build commands above and check for the 7 `telemetry:` lines.
3. Put reverse-engineering evidence in `re/evidence/<slug>/`, so findings can be reproduced.
4. Never add code or tests that contact Jagex or RuneScape servers, and keep personal identifiers out of requests.

Server-side work (game content, protocol handlers) belongs in the OpenRune fork, not here.

## Licensing

RuneLight's own code (`native/src`, `host/`, `tools/`) is under the **ISC licence**. See [LICENSE](LICENSE). Third-party
components inside RuneLight's library (Dear ImGui, parts of RuneLite and its runtime libraries, fonts and shaders) are
listed with their licences in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

**The official Old School RuneScape app is Jagex's property** and is not covered by this licence. RuneLight does not
grant any rights to it. RuneLight is an independent project, not affiliated with or endorsed by Jagex Ltd or the RuneLite
project. "Old School RuneScape" and "RuneScape" are trademarks of Jagex Ltd.
