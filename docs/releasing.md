# Releasing

Releases are one universal arm64-v8a APK of the official OSRS app (build 241.4 at present) with `libosrsrl.so` and the
plugin host added, plus the notices and the mapping of that build.

**Name.** `<OSRS build>-v<our version>`: tag `242.3-v1.0`, title `osrs-runelite 242.3-v1.0`, asset
`osrs-runelite-242.3-v1.0.apk`. The OSRS build is read from the repacked `base.apk` (`versionName`); its major number is
the game revision the build speaks, so a 242.3 build needs a revision-242 server. Dev builds append `-dev.N`.
`VERSION` holds our next version only (0.1, 0.1.1, 1.0.1) and must be above every released one.

**Assets.** Every release attaches:
- `osrs-runelite-<tag>.apk`, the APK;
- `THIRD_PARTY_NOTICES.md`, the licences of the third-party code inside `libosrsrl.so` (also embedded in the APK);
- `offsets.md` and `android-protocol.md`, the offset map and protocol spec for exactly this build.

**Build and checks.** `tools/release-dev.sh [--dry]` builds the pushed `master`, then checks that the repack log has all
7 `telemetry:` lines, the APK is not debuggable, it carries the server's login modulus, and it contains no keys or
personal data. It needs the server's public login modulus: `MODULUS` (256 hex digits) or `KEY_PAIR` (a directory with
`client.key`); `SERVER` defaults to `127.0.0.1:8443`.

**Gate.** Patch bumps and dev builds are automated. A minor or major bump and every official release are signed off by
the maintainer after a device run (core and Hub plugins, network capture, sign-in) of the exact artifact.
