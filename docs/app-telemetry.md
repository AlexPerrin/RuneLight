# Official app telemetry (third-party SDKs in the APK)

Off-device, read-only review (client dev gen 37, 2026-10-18) of the network traffic that the official
OSRS Android app (241.4) makes by itself. This traffic is separate from the host. NetGuard
(`host/src/net/runelite/host/NetGuard.java`) only guards the host's OkHttp client, so it never covered
the app's own SDKs. Sources: `re/jadx/sources` (jadx of the base APK) and `re/apk/base/AndroidManifest.xml`.
Nothing here has been changed in the repack.

Trigger: gen 37 logcat at app start showed `D Singular: SingularRequestHandler [api] - 200 {"admon_batching":...,"resolve":{...}}`.
This is Singular's config response, so a request to Singular's API completed.

## SDKs present

| SDK | Wired up by the app? | Network at start | Notes |
|---|---|---|---|
| **Singular** (attribution) | yes: `sa.f` wraps it; `JagexMobileSDKWrapper` inits it via `sa.b.a(activity, "jagex_games_8f0ecb09", <secret>)` | **yes** (seen) | see below |
| **OneTrust** (consent / CMP) | yes: `za.p` (`initialiseMobileCMP`, called from native) | yes, `cdn-ukwest.onetrust.com` (consent config) | its callback decides whether Singular starts |
| **Firebase Analytics** (Google App Measurement) | yes: `ra.a.b` = `setAnalyticsCollectionEnabled(true)`, from `initialiseAnalyticsService` (called from native, string at ELF `0x0c0f15`); events through `ra.a.c` (`sendAnalyticsEvent`) | likely, once enabled | manifest ships `firebase_analytics_collection_enabled=false`; the app turns it on at runtime |
| **Firebase Crashlytics** (+ NDK) | registrars in the manifest; no `firebase_crashlytics_collection_enabled` meta-data, so it is on by default | on a crash or a pending report upload | sends crash reports with device info |
| **Firebase Messaging / Installations / Sessions / datatransport** | registrars in the manifest; `firebase_messaging_auto_init_enabled=false` | FCM token once messaging inits; Sessions may report app sessions | push only after the app opts in |
| **Braze** (engagement / push) | API key and EU endpoint in `res/values/strings.xml` (`com_braze_api_key`, `com_braze_custom_endpoint` = `sdk.fra-01.braze.eu`); manifest activities and receivers | unverified (not seen in logcat) | lifecycle hooks are in obfuscated classes; whether it opens sessions at start isn't confirmed |

## Singular in detail

- **Init path**: native → `JagexMobileSDKWrapper.initialiseMobileCMP` → OneTrust (`za.p.c` or `za.p.a`) →
  callback `r(boolean)` → `c(wrapper, consent, ...)`. When `consent` is true, it calls `sa.b.a(...)`, which runs
  `Singular.init(config.withLoggingEnabled().withLogLevel(1)...)` and then `Singular.limitDataSharing(true)`.
  The OneTrust boolean on this device has evidently been true, since Singular initialised.
- **Endpoints** (`com/singular/sdk/internal`): `https://sdk-api-v1.singular.net/api/v1` with `/start`
  (session, `ApiStartSession`), `/event` (`ApiSubmitEvent`), the config call (the logged `"resolve"` reply),
  plus `https://exceptions.singular.net/v2/exceptions/android` (SDK exception reports) and `/shorten_link`.
- **Payload of `/start`** (ApiStartSession): app version, ABI, device brand/build/manufacturer/model/product,
  OS version, locale, install/update/current times, device type, user agent, install source, install
  referrer, Do-Not-Track flag, FCM/GCM registration ids when set, preload campaign fields and the encrypted
  API key. Every request identifies the device as keyspace `k` plus id `u` (`BaseApi`, Constants
  keyspaces `aifa` advertising id, `andi` Android id, `asid` App Set id, `imei`, `oaid`, `sdid` Singular's
  own id). The gen 37 response echoed `"k":"AIFA"`, so this device sends its Google advertising id (the
  manifest declares `com.google.android.gms.permission.AD_ID`). `limitDataSharing(true)` flags
  the data as limited; it does not stop sending.
- **When**: `/start` on every app start (and on resume after a session timeout). Events come from
  `sendAttributionEvent` (native) and the one-time `af_started_first_time` after the first attribution
  callback (guarded by the `singular_boot` prefs flag `install_event_fired`).
  `setAttributionAccountId` → `Singular.setCustomUserId(<account id>)`.
- **Earlier captures**: the repo keeps no logcat archives (only `re/jadx.log` and `re/ghidra_import.log`).
  So "since the first gen" can't be shown from files. It is very likely, though: the init path doesn't depend
  on anything we patch, and every LOCAL_AUTH build reaches the same CMP step.

## Options to stop it in our repack (none applied)

| Option | What | Risk to the game |
|---|---|---|
| A. smali: make `sa.b.a` return at once | Singular never initialises. Every other `sa.b` entry already null-checks the instance (`b`, `c`, `d`, `e`), so events, consent and user id become no-ops | **low**: Singular is attribution only; the consent callback still runs the game's continuation (`c(...)` sets the flags and runs the runnable regardless) |
| B. smali: make `ra.a.b` / `ra.a.c` no-ops | Firebase Analytics stays at the manifest default (collection off) | **low** |
| C. manifest meta-data `firebase_analytics_collection_deactivated=true` | Google's permanent kill switch; it overrides `setAnalyticsCollectionEnabled(true)` | **very low**: documented flag, no code change |
| D. manifest meta-data `firebase_crashlytics_collection_enabled=false` | Crashlytics stops automatic collection and uploads | **very low** |
| E. Singular kill switch | none found: Singular has no manifest meta-data switch; `Singular.stopAllTracking()` is only reached through `sa.f.c` when the CMP says no consent | n/a |
| F. smali: force OneTrust's callback to `false` (skip the CMP fetch) | also stops `cdn-ukwest.onetrust.com` | **medium**: the CMP flow gates `setSSOTokenState` and login continuations (`syncConsentFederatedLoginAfterCMP`); getting it wrong can block sign-in |
| G. block hosts outside the app (Windows hosts file / firewall for `*.singular.net`, `*.braze.eu`, `app-measurement.com`, `firebase*.googleapis.com`, `cdn-ukwest.onetrust.com`) | no APK change | **low for the game**, but it changes the user's machine-wide network and affects every app on the PC/emulator; the user's call |
| H. native: skip the JNI calls to `initialiseAnalyticsService` / `initialiseMobileCMP` | stops both at the source | **medium**: needs RE of the caller; the CMP path may also gate login |

Suggested minimal set if the user wants it off: **A + C + D**. All three are in our existing repack step
(`tools/repack.sh` / `tools/patch_server.py`, which the server session owns): one smali method body and two
manifest meta-data lines. Each can be verified on the device by the absence of the `Singular:` /
`FA` / `FirebaseCrashlytics` log lines at start.

## Applied and verified

The user chose **A + C + D**. `tools/patch_telemetry.py` runs as step 1b of `tools/repack.sh` for every
build, normal and LOCAL_AUTH; normal builds now decode resources too, so that the manifest is text. A
normal build rebuilt fine but was not installed. The LOCAL_AUTH build was verified on the emulator from a
clean app start (force-stop, cleared logcat, `debug.osrsrl.netlog 1`), then sign-in, Play Now, LOGGED_IN
and a few minutes of play (walking, tabs, `::nproj`), then a second restart:

- **Singular: gone.** No `Singular` line at all (no "Singular Initialized", no `SingularRequestHandler`), so
  no `/start`, `/event`, config or exception requests. Sign-in and play are unaffected.
- **Firebase Analytics: off.** `FA: App measurement deactivated via the manifest`. Play services' own
  `PackageMeasurementTaskService` upload jobs still get scheduled in the gms process; they belong to
  Google Play services, not the app.
- **Crashlytics: collection off.** It still initialises (`Initializing Firebase Crashlytics 20.0.2`, native
  handler installed), and `FirebaseSessions: Sessions SDK disabled through data collection. Events will
  not be sent.` No upload lines.
- **Still active (not part of A/C/D):**
  - **OneTrust**: `GET https://mobile-data.onetrust.io/cfw/cmp/v1/banner` returns 200 (consent banner
    config; it logs a geolocation country/region), with parameters for `cdn-ukwest.onetrust.com`. This
    is the read-only fetch we expected.
  - **Braze**: active. It registers the FCM push token, "Requesting immediate data flush to Braze", sets
    a user, and resolves `sdk.fra-01.braze.eu`. It is configured from resources (`com_braze_api_key`),
    not from the code paths patched here. To stop it, an option is to blank `com_braze_api_key` /
    `com_braze_custom_endpoint` in `res/values/strings.xml`, or to disable Braze's lifecycle hook (risk
    not assessed yet).
  - DNS lookups netd logged during the run: `api.runelite.net` and `static.runelite.net` (the host's
    read-only GETs: prices and NPC data), `sdk.fra-01.braze.eu` (Braze), and **`account.jagex.com`**,
    which the app looked up right after Play Now, next to its billing client starting ("In-app billing
    API version 3 is not supported"). Whether a connection followed is not visible in logcat. netd only
    logs some lookups, so this list is not exhaustive.

## account.jagex.com consent sync (found gen 37; nothing changed yet)

**The app contacts Jagex at Play Now.** In the gen 37 15:11 run, netd logged a DNS lookup of
`account.jagex.com` right after Play Now, and the app logged `D ConsentSync: Consent sync failed with
HTTP 401`. So an HTTPS request reached Jagex's server, and Jagex rejected it.

- **What**: `POST https://account.jagex.com/consent/sync`, `Authorization: Bearer <token>`,
  `Content-Type: application/json`. The body is the OneTrust consent record (`za.g`: a timestamp plus five
  consent booleans), serialised with Gson. Client: `za.l` (an AsyncTask on `HttpURLConnection`; the
  result is only logged).
- **Where the URL comes from**: native string `"https://account.jagex.com/consent/sync"` (ELF `0x0aa6b4`,
  used by Ghidra `FUN_00847014`), passed to `JagexMobileSDKWrapper` as its sync URL (`f11852i`).
  There is also a dev variant, `https://account.development.jxp.jagex.com/consent/sync`.
- **When**: `initialiseMobileCMP` (from native) → `za.p.a(..., syncUrl, AppAuth state, ...)` when an SSO
  identity is present, which is always the case after a LOCAL_AUTH sign-in. `za.f.b` gets the token
  from the AppAuth state (`ta.l.m`: refreshed if needed), then `za.l.a` posts. It skips with "sync client
  is not configured" when the URL is empty, or with "no authenticated identity token" when there is
  no token.
- **What leaked**: in LOCAL_AUTH builds the bearer token is the test server's token
  (`server/src/main/kotlin/rsps/LocalAuth.kt`): issuer = our local server URL, `sub` = the fixed test
  account id `100000001`, `aud` = the app's client id, `nickname` = the local display name,
  `login_provider` = `jagex`, and a fake signature (`.local`). There is no email or password. Jagex also
  received the consent flags and the device's public IP, and it rejects the token (401).
- **LOCAL_AUTH does not suppress it**: `tools/patch_local_auth.py` only redirects the AppAuth
  endpoints; the consent sync URL stays Jagex's. This has very likely happened at every LOCAL_AUTH
  Play Now since this CMP flow shipped. Normal (non-LOCAL_AUTH) builds would send the user's real Jagex
  token, which is the app's intended behaviour when the user signs in for real.
- **Whether a connection followed**: yes. The 401 is Jagex's HTTP response.

Options (for the user to decide):

| Option | What | Risk |
|---|---|---|
| S1. smali: `za.l.a(String, String, String)` returns at once | never POSTs; the CMP flow continues (the result was only logged anyway) | **very low** |
| S2. native string patch: blank the sync URL (zero the first byte) | `za.f.b` takes the "sync client is not configured" path | low (touches the game library; one byte) |
| S3. point the URL at the test server (same length or shorter, e.g. `http://10.0.2.2:8080/consent/sync`) | the POST goes to our server, which would need a stub endpoint | low-medium (server change, and cleartext is allowed only because of LOCAL_AUTH) |
| S4. leave it | Jagex keeps receiving the test token and consent flags at each Play Now | violates the never-connect-to-Jagex rule |

Recommended: **S1** (same patch step as the telemetry switches, applied to every build).

## Braze (what it sends; nothing changed yet)

- Configured from resources: `com_braze_api_key`, `com_braze_custom_endpoint` = `sdk.fra-01.braze.eu`.
  Logged at start: it registers the FCM push token ("registered and immediately being flushed"),
  "Requesting immediate data flush to Braze", and `change current user <id>`. So the app sets a Braze
  user id (a numeric account-derived id) and Braze uploads its session data, device data (model, OS,
  locale, time zone, carrier, resolution), the push token and any custom events to the EU endpoint.
- Whether bytes went out: the DNS lookup of `sdk.fra-01.braze.eu` was logged and Braze requested a
  flush, so very likely yes. Braze logs no HTTP status at the default log level.
- Options:

| Option | What | Risk |
|---|---|---|
| B1. blank `com_braze_api_key` (res/values/strings.xml) | the SDK can't authenticate; current Braze SDKs log an error and do not send without a key | low; in-app messages and push from Braze stop (none matter here); the app's Braze calls stay no-ops |
| B2. point `com_braze_custom_endpoint` at an unroutable host (e.g. `127.0.0.1`) | requests fail fast | low; retries add log noise |
| B3. both B1 and B2 | belt and braces | low |
| B4. leave it | device data, the account-derived user id and the push token keep going to Braze | — |

### Consent sync fix: applied and verified

The user approved "Patch it out". `tools/patch_telemetry.py` (commit e986782) makes `za.l.a` return at
once and zeroes both native sync URLs; it runs for every build. Verified on the emulator with the
LOCAL_AUTH build across a clean start, Play Now, LOGGED_IN, a `::drop` reconnect, an app restart,
a second Play Now, and about a minute of play:

- **No `ConsentSync` line at all** (neither the HTTP line nor a "skipped" line) and no `Singular` line.
- **netd DNS lookups** (whole run): only `sdk.fra-01.braze.eu`, `static.runelite.net` and `api.runelite.net`.
  **No `*.jagex.com`** (earlier runs had `account.jagex.com` right after Play Now).
- **Established TCP sockets of the app's uid** (`/proc/net/tcp*`, sampled at the login screen, 2/4/6/8 s
  after Play Now, after LOGGED_IN, after the reconnect, at the second Play Now and after play), always
  the same four: `10.0.2.2:43594` (OpenRune game server), `104.26.9.233:443` (Cloudflare, `api`/`static.runelite.net`),
  `104.18.35.7:443` (`sdk.fra-01.braze.eu`) and `172.64.155.231:443` (`mobile-data.onetrust.io`).
  There were none to Jagex's addresses (`account`/`auth.jagex.com` 104.18.40.92 / 172.64.147.164,
  `payments.jagex.com` 18.67.39.x).
- OneTrust's banner GET still returns 200 (expected). Braze is unchanged (left as is by the user's decision).
- The game works: sign-in, LOGGED_IN, teleport, tabs, reconnect.

**History**: the consent sync is part of the CMP flow, which runs at every Play Now once an SSO identity
exists. On LOCAL_AUTH builds (the only kind we install), it most likely ran at every Play Now
since LOCAL_AUTH sign-in was first used on the device (2026-10-16), sending the
test server's unsigned token described above. No earlier logcat captures were kept to confirm a
per-day count.

### Remaining Jagex hosts (static audit, gen 37)

| Host | Path | Status under LOCAL_AUTH |
|---|---|---|
| `account.jagex.com/consent/sync` (+ dev) | CMP consent POST | **patched** (above) |
| `auth.jagex.com`, `social.auth.jagex.com`, `auth.runescape.com`, `account.jagex.com/` (game-session) | jav_config params 11/20/22/28 | rewritten to the sidecar (`server/…/Http.kt` AUTH_PARAMS) |
| `payments.jagex.com` | jav_config param 2 → payments SDK: `operator/v1/shop/<shop>`, `operator/v1/initiate/...` (price + android_id), `operator/v2/completion/...` | **not rewritten in template mode**; the orchestrator is adding param 2 to the sidecar's AUTH_PARAMS. Until then, don't open store/membership/bond UIs |
| `pp-%s.%s.jagex.com:13406/v1/groupScoreDetails/...` | native, group-ironman score lookup (FUN_0064da14) | only when a GIM score interface asks; **patched** (5befe07): the one full format string's host -> `pp-%s.%s.x.invalid` (RFC 2606, never resolves; same length, exactly-one-match assert). The regular hiscore branch builds `secure` + jav_config param 13 domain, i.e. not Jagex under our config |
| `legal.jagex.com`, `support.runescape.com`, `secure.runescape.com`, `www.jagex.com/terms`, `oldschool.runescape.com/play` | browser links on tap | no automatic traffic |
| `www.runescape.com` / `www.jagex.com` (`adverturl`, `termsurl`, `privacyurl`), `secure.runescape.com` (`gedigesturl`) | desktop-loader jav_config keys | not seen used by the Android app (no lookups) |
| `res/values` `com_jagex_mobilesdk_runeScapeAuthURL`, `paymentUri` (a billing dev host) | only referenced from R | unused |

## Gen 38 device checks (2026-10-18)

Build: master c531d81 (5befe07 included), LOCAL_AUTH, repacked with `patch_telemetry.py`. The log shows
`telemetry: group score host -> pp-%s.%s.x.invalid`, which prints only on the "exactly one original, no patched" path.
The packaged `liblibs.hal.system.osclient.so` has 0 `jagex.com:13406` and 1 `x.invalid:13406`.

**Slot 1 (15:40-15:47)**: 4 app restarts with Play Now (two through "Try again"), ~6 min of play. Socket sampler:
`/proc/net/tcp*`, uid 10071, every 2 s. netd DNS: only `api.runelite.net`, `static.runelite.net`, `sdk.fra-01.braze.eu`.
Sockets: 10.0.2.2:43594 and :443 to 104.26.8.233 / 172.67.74.139 (runelite), 104.18.35.7 / 172.64.152.249 (braze),
172.64.155.231 (onetrust). No `pp-*.x.invalid` lookup, no Jagex address.

**Slot 2 (15:49-15:59)**, after the sidecar's param 2 rewrite (`param=2=http://10.0.2.2:8080/`). Steps: restart, Sign out ->
"Sign in with Email / Username" (LOCAL_AUTH) -> Play Now ("Try again" once), play, the Account tab's "Upgrade Now"
(the single allowed store attempt), `::drop` reconnect, restart + Play Now, teleports/walking.
- **New Jagex contact: `secure.runescape.com`.** At 15:51:42.634 netd logged `asked for "secure.runescape.com IN A"`,
  during LOADING of the first login after the fresh sign-in (LOGGED_IN at 15:51:43). In the same sample the app uid
  had an ESTABLISHED socket to 104.18.35.117:443 (= secure.runescape.com), gone 2 s later. It happened once: not on
  the reconnect, the later restart + login, nor any slot-1 login. Reported; not reproduced (that would contact Jagex again).
- Everything else was as expected. DNS: runelite and braze only. Sockets: the hosts above plus 104.18.32.25 (onetrust)
  and 172.217.116.4 (Google, most likely FCM for Braze's push token). No `*.jagex.com`, no `pp-*.x.invalid`, no
  `ConsentSync`/`Singular`, no `payments.jagex.com` and no 18.67.39.x.
- **Payments (param 2): served-config-only, not proven in-app.** "Upgrade Now" is a server-handled CC_OP (109:32 op 1).
  OpenRune doesn't answer it, so the app made no shop request: no `/operator/` line in the sidecar log, and no
  payments DNS or socket. The in-app path is JNI `requestShopData` -> `GET <param 2>operator/v1/shop/<shop>`
  (`com.jagex.mobilesdk.payments.f`, `eb.e.d`, bearer token header); it needs a server-side trigger to exercise.

### Where secure.runescape.com can come from (static, gen 38)

| Path (ELF address) | URL | When |
|---|---|---|
| `gedigesturl` jav_config key (parsed in 0x40ee18 into Client +0x418628, must start with `http`) -> 0x4d5320 builds the digest object, called from 0x4101d4 right after the config parse | official config: `https://secure.runescape.com/m=itemdb_oldschool/g=oldscape/digest.csv` (GE price digest) | object made at every start; the download evidently happens later and is cached (one fetch seen, at the first login after a sign-in). **Most likely source of the 15:51:42 contact.** The sidecar now serves `http://10.0.2.2:8080/m=itemdb_oldschool/g=oldscape/digest.csv` (orchestrator, 2026-10-18) |
| URL builder 0x41102c: `"%s%s%s.%s/l=%s/a=%i%s/"` = scheme, subdomain (or `local`), env suffix (`-wtrc`/`-wtqa`/`-wtwip`/`-wti`), hard-coded `"runescape.com"` (0xd6812), language, affiliate, optional `/p=<x>` | `https://secure.runescape.com/l=<lang>/a=<aff>/...` | callers below |
| 0x4101d4 (same startup step) | `https://secure.runescape.com/l=../a=../` handed to a platform object (vtable +0x40) with five other config strings | at every start; consumer not traced. Not a request by itself (no lookup at most starts) |
| 0x47b548 (from 0x47bc84, 0x41615c) | `https://secure.runescape.com/l=../a=../m=account-creation/g=oldscape/create_account_funnel.ws` | account creation link (user action) |
| 0x3ec1a0 / 0x3ec558 (from 0x47c878) | `http://services.runescape.com/l=../a=../m=accountappeal/login.ws?data1=req`, native HTTP GET (`connection: close`, 5 s timeout, reads an 8-byte answer) | login-screen account-appeal/recovery status flow (result codes 2-7 -> login messages); not seen in any capture |
| 0x54da14 | `https://pp-%s.%s.x.invalid:13406/...` (GIM scores) | patched (5befe07) |
| static strings `secure.runescape.com/m=offence-appeal/...`, `m=accountappeal/passwordrecovery`, `m=news/...` | browser links | on tap only |

Fix options for the builder paths (not applied): patch `runescape.com` at 0xd6812 to a same-length reserved name
(for example `x.invalid` padded with NULs; the format's `%s.%s` then gives `secure.x.invalid`), in `patch_telemetry.py --native`
with an exactly-one-match assert like the GIM rewrite. That would cover 0x4101d4, 0x47b548 and 0x3ec1a0/0x3ec558 at once.
The gedigesturl fix is server-side (sidecar).

### runescape.com builder patch: applied and verified

Approved by the orchestrator under the user's "patch it out" decision; commit 85f7d62. `patch_telemetry.py --native`
rewrites `"\0runescape.com\0"` (ELF 0xd6812) to `"\0x.invalid"` plus NULs, with an exactly-one-match assert. Safety:
Ghidra finds one reference into those 14 bytes, a `%s` vararg of the std::string vsnprintf-style formatter
(Ghidra FUN_00505ef0), so nothing reads the buffer past its NUL. At the same time the sidecar now serves
`gedigesturl`, `adverturl`, `termsurl` and `privacyurl` from 10.0.2.2:8080.

Verified on the emulator (socket sampler every 0.5 s): Sign out -> "Sign in with Email / Username" -> Play Now
(LOGGED_IN), play, app restart -> "Try again" -> Play Now, play. netd DNS: only `api.runelite.net`, `static.runelite.net`,
`sdk.fra-01.braze.eu`. There were no `*.runescape.com`, `*.jagex.com`, `secure.x.invalid`, `services.x.invalid` or
`pp-*.x.invalid` lookups. Sockets: 10.0.2.2 (43594, 8080), runelite, braze, onetrust and a Google 443 address only.
Sign-in, the login screen and Play Now work.

**Caveat: the source of the 15:51:42 secure.runescape.com contact is not confirmed.** In this run the client fetched
no GE digest: the sidecar logged no `digest.csv` request. The digest download is evidently periodic or cached, not
once per sign-in, so this capture doesn't exercise the gedigesturl redirect. Both candidates are now covered (the
sidecar's gedigesturl rewrite and the native builder patch), but neither was observed firing.

## Network QA (rule from 2026-10-18, gen 39)

Device slots no longer end with a DNS and socket check. That check is now a QA step, run only when a build can
change the app's network behaviour:

- changes to `tools/repack.sh`, `tools/patch_telemetry.py` or `tools/patch_server.py`;
- a new APK or game version;
- changes to the sidecar or to jav_config (served URLs and params);
- host networking code: `NetGuard`, the host's OkHttp client, new GET sources;
- a hub plugin that makes network calls.

Always, even without QA: every build goes through `tools/repack.sh` (with `patch_telemetry.py`, which prints its
`telemetry:` lines), and anyone who happens to see a new outbound host (logcat, netd, sockets) stops and reports it at once.

**Procedure** (LOCAL_AUTH build, your own Agent_Phone emulator, app uid 10071):

1. Before starting: `adb logcat -c`, then start a socket sampler that runs every 0.5 s and keeps ESTABLISHED (state `01`) rows of the app uid
   from `adb shell cat /proc/net/tcp /proc/net/tcp6` (remote address = column 3, hex little-endian), timestamped.
2. Run through: Sign out -> "Sign in with Email / Username" (LOCAL_AUTH; screenshot first, never a real Jagex login) ->
   Play Now (tap "Try again" at 958,668 only on that screen) -> LOGGED_IN -> play (walk, tabs, a teleport) ->
   `::drop` relog -> app restart (force-stop + launch) -> Play Now -> play for about a minute.
3. DNS: `adb logcat -d | grep -i "asked for"` (netd lookups; this doesn't list every lookup, which is why
   the socket sampler is needed).
4. Expected hosts, and nothing else:
   - `10.0.2.2` (43594 game server, 8080 sidecar: jav_config, auth, digest, payments param 2);
   - `api.runelite.net`, `static.runelite.net` (Cloudflare, e.g. 104.26.8.233 / 104.26.9.233 / 172.67.74.139): the host's read-only GETs;
   - `sdk.fra-01.braze.eu` (104.18.35.7 / 172.64.152.249): Braze, left on by the user's decision;
   - `mobile-data.onetrust.io` / `cdn-ukwest.onetrust.com` (172.64.155.231, 104.18.32.25): the consent banner GET;
   - one Google 443 address (e.g. 172.217.116.4): most likely FCM for Braze's push token;
   - a netd lookup of `dns.google` (**system, not app: Private DNS validation**). Android Pie's opportunistic Private DNS (DoT)
     validates the 8.8.8.8 resolver. Evidence: netd logs no uid; `dumpsys connectivity` shows `UsePrivateDns: true`,
     `DnsAddresses: [8.8.8.8,10.0.2.3]`, `private_dns_mode` unset (the default, opportunistic); no app-uid socket goes to it.
5. Must not appear: any `*.jagex.com`, `*.runescape.com`, `*.singular.net`, `app-measurement.com`, `pp-*.x.invalid`,
   `secure.x.invalid` / `services.x.invalid` lookups; no `ConsentSync` or `Singular` log lines; no socket to Jagex addresses
   (104.18.40.92, 172.64.147.164, 104.18.35.117, 18.67.39.x).
6. Record the build, the time window, the DNS list and the distinct socket addresses here, under a dated heading.

### Plugins and java.net

NetGuard only guards the host's shared OkHttp client. Plugin code that uses `java.net` directly (`URL.openConnection`/`openStream`,
`HttpURLConnection`, `Socket`, `InetAddress`, `java.net.http.HttpClient`, `javax.net.*`) would bypass it. The case that showed this:
`multi-lines` fetches its area data from raw.githubusercontent.com with `java.net.http.HttpClient`. That class doesn't exist on
Android, so the plugin threw before sending anything; its pin was removed.

**Static check (in place): `host/plugins/hub/netcheck.sh <jar>...`**, which `host/plugins/hub/build.sh` runs on every hub build.
It reads each class's constant pool and gives a verdict per jar:
- `NET-RAW`: java.net/javax.net I/O classes. A bare `java/net/URL` counts only when the class also references `openConnection`/`openStream`.
  The build stops and doesn't dex the jar unless `ALLOW_NET=1`, which needs the orchestrator's OK.
- `NET-OKHTTP`: okhttp3 only. Requests go through the injected client, so NetGuard applies, but the plugin still counts as a network plugin (Network QA).
- `LINKS`: only `LinkBrowser` (a browser opens on a tap). `clean`: none of these.

Scan of all 99 built hub jars: 85 clean, 7 LINKS, 5 NET-OKHTTP (c-engineer-completed, collection-log, dropsounds,
equipment-inspector, resource-packs), and 2 NET-RAW:
- multi-lines: dropped.
- loot-lookup: the raw references are only in its bundled jsoup (`org/jsoup/Connection`, `HttpConnection`). The plugin fetches through the
  injected OkHttp client and parses with jsoup, so it doesn't call `Jsoup.connect`. Rebuilt with `ALLOW_NET=1`; **approved by the orchestrator** and on R1's vetted list for that reason.

**Runtime guard for java.net: proposal (not implemented).** Options, rated by risk to the game process:

| Option | Scope | Risk |
|---|---|---|
| R1. Host plugin loader refuses NET-RAW jars: before `new DexClassLoader`, scan the jar's `classes.dex` string table for the same descriptors (`Ljava/net/HttpURLConnection;`, `Ljava/net/Socket;`, `Ljava/net/http/`, `Ljavax/net/`, plus `Ljava/net/URL;` with `openConnection`/`openStream`), and skip the plugin with a log line unless it's on an allow-list (`debug.osrsrl.netallow`, comma-separated plugin names) | plugin jars only | **none for the game**: nothing process-wide changes. Cost: a byte scan per jar at start. Bundled libraries (jsoup) trip it, hence the allow-list |
| R2. Rewrite at dex time (`tools/dex-plugin.sh`): redirect plugin calls to `URL.openConnection/openStream` and `new Socket` to a host shim that applies NetGuard's rule (GET/HEAD, no xp/update) and logs, or throws | plugin bytecode only | low for the game; medium effort (a bytecode rewrite step, e.g. ASM before d8). Needed only if a wanted plugin really uses java.net |
| R3. `URL.setURLStreamHandlerFactory` wrapping the default http/https handlers, with a stack check (`plugin classloader on the stack` -> apply the rule) | **whole process** | **high**: the app's own Java SDKs use `HttpURLConnection` in the same process: AppAuth token exchange with the sidecar (sign-in), the OneTrust banner, Braze, the payments SDK. The factory can be set only once per process (an SDK that sets one first makes ours throw). Wrapping needs hidden-API reflection into `com.android.okhttp` on Pie. A bug here breaks sign-in |
| R4. `ProxySelector.setDefault` returning an unreachable proxy for plugin callers | whole process (HttpURLConnection) | medium-high: the same shared path as R3. It sees only the URI, not the method, so it can't express GET-only. Raw sockets mostly skip it |
| R5. `Socket.setSocketImplFactory` / a default `SocketFactory` | whole process (Java sockets) | high: catches every Java socket the app's SDKs open. The game's native sockets (game server, sidecar) are not Java, so they are untouched either way |

The game's own networking is native (libc sockets in the game library). None of R1-R5 touches it, but R3-R5 do reach the app's
Java SDKs, including the AppAuth sign-in. **Recommended: R1** (deny by default, allow-list per plugin), plus the build-time netcheck.
R2 only if a plugin that needs java.net is wanted. R3-R5 are not recommended.

### R1 load-time gate: implemented

Approved by the orchestrator. `Host.loadJar` copies each plugin jar to the cache dir as before, then calls
`NetScan.refusal(copy)` on that copy (so the checked bytes are the loaded bytes) before `new DexClassLoader`. It searches every
`classes*.dex` for the descriptors listed under R1 (the same list as netcheck.sh). A refused jar is not loaded; its cache copy is
deleted, and the host logs `W osrsrl-host: netscan: refused <jar>: java.net outside NetGuard: [...] (vet it, or list it in debug.osrsrl.netallow)`.

- **Vetted** (hard-coded in `NetScan.VETTED`, by jar name without `.jar`):
  - `hub-loot-lookup`: its raw references are only in bundled jsoup (`HttpConnection`); the plugin fetches through the injected OkHttp client (approved, gen 40).
  - `devtools`: the devtools bridge's local server (`InetAddress`/`InetSocketAddress`), reached over adb only.
- **Allow-list for testing**: `setprop debug.osrsrl.netallow <name>[,<name>...]` (jar names without `.jar`). Clear it after the slot.
- Core plugin jars (grounditems, opponentinfo, quest-helper) hold only `plugin-classes.txt`, so they never trip it.
- **Off-device check**: `host/plugins/hub/netscan-check.sh <jar>...` compiles NetScan with a stub `Native`, runs a self-test on
  synthetic jars (clean, Socket, java.net.http, URL as a value vs. URL+openStream, a reference only in classes2.dex, allow-list
  matching, vetted name), then prints `load` / `REFUSE <reason>` per jar. On gen 40's 189 dexed jars (`re/host/plugins`, `re/host/plugins-extra`)
  all load (loot-lookup and devtools through the vetted list; copies of either under another name are refused).
- **Device check**: with `hub-netscan-test.jar` (an inert dex holding one uncalled `new java.net.Socket()`, empty plugin
  list) pushed, the host logged `W osrsrl-host: netscan: refused hub-netscan-test.jar: java.net outside NetGuard: [Ljava/net/Socket;] (vet it, or list it in debug.osrsrl.netallow)`
  and started the default 3 plugins. With `debug.osrsrl.netallow hub-netscan-test`, the next start had no netscan line. That run also loaded `devtools`
  (vetted) and `hub-max-hit-calculator`: 5 plugins running.

### Network QA, gen 40 (2026-10-18 17:20:38-17:31:43)

Build: master 40c9ed2 (the R1 gate), LOCAL_AUTH OpenRune build through `tools/repack.sh` (7 `telemetry:` lines). Run: app start (R1 refusal),
Sign out -> "Sign in with Email / Username" -> Play Now -> LOGGED_IN -> tabs, two `::tele` -> `::drop` relog -> restart (netallow, devtools,
Max Hit) -> "Try again" -> Play Now -> another restart (devtools bridge) -> Play Now, attacked a Man, teleport home. 4482 samples at 0.5 s.
- netd DNS: `api.runelite.net`, `static.runelite.net`, `sdk.fra-01.braze.eu` (3 each), and `dns.google` once at 17:21:25, right after the
  LOCAL_AUTH sign-in (system Private DNS validation, see the expected list). No `*.jagex.com`, `*.runescape.com`, `*.x.invalid`, `singular`, `app-measurement`.
- App-uid sockets: 10.0.2.2:43594; 104.26.8.233, 104.26.9.233, 172.67.74.139 (runelite); 172.64.152.249 (braze); 104.18.32.25 (onetrust);
  172.217.119.4 (Google), all :443. No Jagex address. 0 `ConsentSync`/`Singular` lines.
- No GE digest fetch happened in this run (still unconfirmed, as in gen 38).

### Network QA, gen 46 Drop Sounds (2026-10-19 19:33:50-19:41:18)

Build: client/host-javax-sound-shim (javax.sound.sampled shim), `OPENRUNE=1 SERVER=10.0.2.2:8080 GAME_PORT=43594 LOCAL_AUTH=1
MODULUS=... tools/repack.sh` (7 `telemetry:` lines). Plugins: the default 3, devtools and `hub-dropsounds.jar` (its read-only GETs to
raw.githubusercontent.com/MitzDK/DropSounds approved by the orchestrator, gen 46). Sampler: ESTABLISHED uid-10071 rows every 0.5 s,
585 samples. Run: app start, Sign out -> "Sign in with Email / Username" -> Play Now -> LOGGED_IN, walk, tabs, `::tele` to Fossil Island
and back, `::drop` relog, force-stop + launch, Play Now, a minute of walking.
- App-uid sockets: 10.0.2.2:43594, 10.0.2.2:8080; 104.26.8.233 (runelite); 172.64.152.249, 104.18.35.7 (Braze); 172.64.155.231
  (OneTrust); **185.199.110.133:443** (GitHub, raw.githubusercontent.com range) at 19:34:19, one second after `started plugin Drop
  Sounds`: its two wav GETs (both 404 upstream, see plugin-host.md). All :443 but the 10.0.2.2 ones.
- netd DNS: `api.runelite.net`, `static.runelite.net`, `sdk.fra-01.braze.eu` (one each, after the restart). No `*.jagex.com`,
  `*.runescape.com`, `*.singular.net`, `app-measurement.com` or `*.x.invalid` lookup; 0 `ConsentSync`/`Singular` lines; no Jagex
  address. **PASS** (only the approved GitHub host is new).

### Network QA, v0.1.0 release gate (2026-10-18 22:22:02-22:27:38)

Run for the v0.1.0 release gate (the full record is kept in the harness). Build: master 09c05c9, `OPENRUNE=1 SERVER=10.0.2.2:8080
GAME_PORT=43594 LOCAL_AUTH=1 tools/repack.sh` (7 `telemetry:` lines), default plugins + devtools (and briefly an R1 test jar,
refused). Sampler: ESTABLISHED uid-10071 rows every ~0.6 s, 469 samples. Run: app start, Sign out -> "Sign in with Email /
Username" -> Play Now -> LOGGED_IN, walk, `::tele` Varrock, tabs, `::drop` relog, force-stop + launch, "Try again" after ~45 s,
Play Now, `::tele` home, attacked a Man, about a minute of walking.
- netd DNS: `api.runelite.net` x2, `static.runelite.net` x2, `sdk.fra-01.braze.eu` x2, `dns.google` x1 at 22:23:11 right
  after sign-in (system Private DNS validation, expected).
- App-uid sockets: 10.0.2.2:43594 (game), 10.0.2.2:8080 (sidecar); 104.26.8.233, 172.67.74.139 (runelite); 172.64.152.249
  (Braze); 104.18.32.25 (OneTrust); 172.217.114.4 (Google), all :443.
- Must-not-appear: no `*.jagex.com`, `*.runescape.com`, `*.singular.net`, `app-measurement.com` or `*.x.invalid` lookup;
  0 `ConsentSync`/`Singular` lines; no socket to a Jagex address. **PASS.**
- Extra run, not the release's own traffic (22:28:44-22:34:34, 594 samples, all 87 core and 106 hub plugins loaded): the same
  expected hosts (plus 104.18.35.7 Braze and 172.64.155.231 OneTrust) and **185.199.111.133:443**, a GitHub address
  (raw.githubusercontent.com range), from 22:32:46, the moment the host started the hub batch. No log line names the
  requester. Of the documented hub plugins that GET from raw.githubusercontent.com, Resource packs fetches only with a pack
  selected (none was), so it is most likely C Engineer: Completed's approved sound-list/sound-file GETs (approved gen 26,
  NET-OKHTTP through NetGuard). Not a Jagex host, and hub plugins are not in the APK.
