# xtool evaluation: making iPhone and iPad games from the shared Linux box

*Prepared Tue Oct 20, 2026 (America/Chicago, CDT). Workspace: `/workspace/xtool-eval`.*

Each claim carries one of these labels:
- **[TESTED]**: I ran it on the box.
- **[SOURCE]**: I read it in xtool's source code (commit `aa6ba75`, 2026-10-20).
- **[DOCS]**: it comes from official docs, release notes, or issue and PR threads (linked).
- **[UNVERIFIED]**: my inference, not confirmed.

---

## 1. Verdict

**xtool can handle the daily write, build, and sign loop for a SwiftPM-based Swift game (SpriteKit, SceneKit, Metal, GameKit APIs) on this box. Two things block an "entirely from Linux" workflow:**

1. **The box has no way to reach an iPhone.** It is a remote VM with no USB bus and a private NAT address. Installing, launching, and debugging on a device needs a tunnel from a machine that is physically near the phone. The Mac would do.
2. **Shipping to TestFlight or the App Store isn't solved in released xtool.** xtool only makes *development* certificates and profiles. Its asset-catalog support (`Assets.car` / AppIcon) doesn't exist yet. Apple-required Info.plist and Mach-O SDK stamps are missing. A Linux-only path exists in **open, unmerged PRs** (#321, #323, #325, #326, #327), and even those state that AssetKit-compiled `Assets.car` files get stuck in App Store "Processing".

**Recommendation:** Do day-to-day development on the box with xtool, but plan for the Mac to be needed:
- **Once** at setup (the Xcode.xip download, an `Assets.car` from `actool`, and optionally a distribution certificate).
- **Briefly per release** while the upload PRs remain unmerged.
- **Any time we need the Simulator, Instruments, or store screenshots** that don't come from a real device.

A third-party engine (Godot, Unity, Unreal) **cannot** be finished on Linux. All of them hand off to Xcode on a Mac.

---

## 2. The project itself

| Item | Finding |
|---|---|
| Repo | https://github.com/xtool-org/xtool (MIT license; created 2024-11-17; ~13.1k stars, 667 forks, 107 open issues as of today) [DOCS] |
| Latest release | **1.22.2**, published Oct 13, 2026 7:30 PM CDT (https://github.com/xtool-org/xtool/releases/tag/1.22.2) [DOCS] |
| Release cadence | 1.17.0 (May 2026), then 1.18.1, 1.19.0, 1.19.2, 1.20.0, 1.20.1, 1.21.0, and 1.22.2, all between Sep 3 and Oct 13, 2026. Very active. [DOCS] |
| Commits | 31 commits to `main` since Sep 20, 2026. The latest landed Oct 20, 2026 12:00 AM CDT. [TESTED: git log] |
| Maintainers | Mostly one person: Kabir Oberai, 42 of the last ~47 commits (a bus-factor risk). There are many outside PRs, and several open ones are labeled "AI set going, nobody reviewed". [TESTED: git log; DOCS: PR list] |
| Supported toolchain | Swift 6.4 plus **Xcode 27** SDK (Installation-Linux.md). 1.20.0 moved to SwiftBuild, which brings behavior closer to Xcode. [DOCS] |
| Auth fragility | Releases 1.19.0, 1.19.2, and 1.22.2 were all fixes for Apple ID password login breaking (xtool impersonates an Android/"Pixel 7" client against private Apple APIs). The App Store Connect **API key** login uses public APIs and is the stable option. [DOCS] |
| Scope | Builds only **SwiftPM packages** (exactly one library product equals the app, plus optional app extensions). It does not build `.xcodeproj` projects, but it can *generate* one (`xtool dev generate-xcode-project`) for use on a Mac. [SOURCE/TESTED] |

---

## 3. The box

| Item | Value | Source |
|---|---|---|
| OS | Debian GNU/Linux 13 (trixie) | [TESTED] |
| Arch | **x86_64**, 8 vCPU (VMX flag present, `/dev/kvm` exists) | [TESTED] |
| RAM | 15 GiB total, ~10 GiB available, **no swap** | [TESTED] |
| Disk | 125 GB, **~76 GB free** at start | [TESTED] |
| USB | **None.** There is no `/sys/bus/usb` or `/dev/bus/usb`. usbmuxd starts but reports "0 devices detected". | [TESTED] |
| Network | Single NIC `172.30.0.2/24` (private, NAT). No Tailscale or VPN installed. Wi‑Fi/Bonjour discovery of a phone on your home LAN isn't possible. | [TESTED] |
| sudo / FUSE | Both available. The xtool AppImage runs directly. | [TESTED] |

Disk budget for the full setup:
- Swift toolchain: 3.6 GB (installed)
- xtool: 55 MB
- Darwin SDK: ~7 GB normal, ~3 GB with `--slim` [DOCS: 1.20.0 notes]
- Xcode.xip: several GB, plus temporary extraction space [UNVERIFIED size]

All of that fits. RAM use during `xtool sdk install` (unxip) is **[UNVERIFIED]**. Given the box's current memory and KVM problems, run it while nothing else heavy is running.

---

## 4. What I actually ran on the box

1. Downloaded **xtool 1.22.2** x86_64 AppImage → `/workspace/xtool-eval/xtool`. `xtool --version` runs fine. [TESTED]
2. Installed **Swift 6.4.1** (swift.org Debian 13 build, released 2026-10-12) → `/workspace/xtool-eval/toolchain` (not on the system PATH). `swift --version` works. [TESTED]
3. `xtool new HelloGame --skip-setup` generated a SwiftPM app (iOS 17 and macOS 14 platforms, `xtool.yml`, `.bsp/xtool.json` for SourceKit-LSP). [TESTED]
4. Replaced the template with a **minimal SpriteKit game**: tap falling blocks to score, using SwiftUI `SpriteView`. I also added a **Metal kernel compiled at runtime** from a source string. Code is in `/workspace/xtool-eval/HelloGame/Sources/HelloGame/`. [TESTED: written; *not compiled*]
5. `xtool dev build` **stops before compiling.** It immediately asks for the SDK:
   ```
   Now generating the Darwin SDK.
   Please download Xcode from http://developer.apple.com/download/all/?q=Xcode
   and enter the path to the downloaded Xcode.xip.
   ```
   Without an Xcode.xip from an Apple ID, nothing iOS-related can compile. **This is the hard stop for an unauthenticated evaluation.** [TESTED]
6. Found a bug: with stdin redirected from `/dev/null`, xtool crashes with `epoll_ctl ... Operation not permitted` (NIO). It needs a pty (`script -qc`) for agent or CI use. This matches open PRs #366 and #304. [TESTED]
7. Installed `usbmuxd` 1.1.1 and `libimobiledevice-utils` from Debian. usbmuxd runs, sees 0 USB devices, and `xtool devices --no-wait` returns an empty list. [TESTED]
8. **Remote-device plumbing check.** I bridged usbmuxd over TCP with `socat TCP-LISTEN:27015 → /var/run/usbmuxd` and set `USBMUXD_SOCKET_ADDRESS=127.0.0.1:27015`. Both `xtool devices` and `idevice_id` connected through the TCP bridge (the usbmuxd log shows the clients accepted via socat). So xtool **does honor `USBMUXD_SOCKET_ADDRESS`**, which is the hook for driving a phone attached to another machine through a tunnel. I couldn't test it end-to-end without a device. [TESTED partial]
9. **Not done:** I didn't sign into any Apple account, run `xtool setup` or `auth`, or create any accounts.

---

## 5. Stage-by-stage, for games

### 5.1 Writing code
| Item | Status | Notes |
|---|---|---|
| Swift / SwiftUI / UIKit code | ✅ Linux | SwiftPM only, and the editor works with SourceKit-LSP through xtool's BSP server (1.20.0+). [DOCS/SOURCE] |
| Xcode projects (`.xcodeproj`) | ❌ Can't build them on Linux | Projects must be SwiftPM packages. xtool can *export* a `.xcodeproj` for the Mac. [SOURCE] |
| Storyboards / XIBs | ❌ | No `ibtool` on Linux. Use SwiftUI or code-only UIKit. The default plist uses `UILaunchScreen` = {} (no storyboard needed). [SOURCE] |
| SpriteKit, SceneKit, GameKit, GameController, AVFoundation, CoreHaptics, Metal APIs | ✅ (compile and link against the SDK) | They're ordinary SDK frameworks. [UNVERIFIED: no SDK, so not compiled here] |
| `.metal` shader files | ⚠️ Workaround | Xcode's `metal` compiler is a macOS binary (since Xcode 26 it's a separate "Metal Toolchain" download). On Linux, either compile shaders at runtime with `device.makeLibrary(source:)` (as in my sample) or precompile a `.metallib` on the Mac and ship it as a resource. [UNVERIFIED: that SwiftPM/SwiftBuild on Linux fails on `.metal` files; no SDK to try it] |
| Asset catalogs (`.xcassets`) incl. AppIcon, named colors, sprite images | ❌ Released xtool | No `actool` on Linux. The Xcode copy is a Mach-O binary. A `.xcassets` in SwiftPM resources makes the build fail ([#310](https://github.com/xtool-org/xtool/issues/310)). Use loose PNGs, plus `iconPath:` for the icon. Open PR [#325](https://github.com/xtool-org/xtool/pull/325) adds AssetKit compilation, but it says App Store processing hangs on its `Assets.car`. [DOCS/SOURCE] |
| SpriteKit `.atlas` texture atlases, `.scnassets`, Reality Composer Pro `.reality` | ❌ / ⚠️ | These need Xcode's TextureAtlas, scntool, and realitytool. Workarounds: load loose PNGs or `SKTextureAtlas(dictionary:)` at runtime, and use `.usdz` or `.scn` files built once on the Mac. [UNVERIFIED] |
| Info.plist customization (orientation, display name, iPad) | ✅ | Use `infoPath:`. The default is `UIDeviceFamily` [1,2] (iPhone and iPad), portrait. [SOURCE/DOCS] |
| Privacy manifest (`PrivacyInfo.xcprivacy`) | ✅ | It's just a plist. Put it at the bundle root with `resources:` in `xtool.yml`. [SOURCE: root resources copied; UNVERIFIED with ASC] |
| Unit tests | ⚠️ | `swift test` on Linux covers game *logic* that doesn't import UIKit or SpriteKit. On-device XCTest and XCUITest are in open PR [#236](https://github.com/xtool-org/xtool/pull/236) (not merged). [DOCS] |

### 5.2 Engines
| Engine | iOS from Linux? | Could xtool finish it? |
|---|---|---|
| **Godot 4** | The Linux editor exports an **Xcode project only** ("`.ipa` can only be built on macOS"). C# projects must export from macOS. ([Godot docs](https://docs.godotengine.org/en/latest/tutorials/export/exporting_for_ios.html), [macrun.dev test](https://macrun.dev/guides/godot-ios-export-without-mac)) [DOCS] | No. xtool builds only SwiftPM packages. The Mac's `xcodebuild` has to compile it. **Once the Mac produces an unsigned `.ipa`, `xtool install` can sign and install it from Linux** ([#310 comment](https://github.com/xtool-org/xtool/issues/310)). [DOCS] |
| **Unity** | Generates the Xcode project on Linux (iOS Build Support), but the build needs Xcode on macOS or paid Unity Build Automation ([Unity docs](https://docs.unity.com/en-us/engine/6000.0/manual/platform-specific/iphone/getting-started/ios-environment-setup)). [DOCS] | Same as Godot: build on the Mac, and xtool can re-sign or install the result. |
| **Unreal** | iOS builds need a Mac (remote build). [UNVERIFIED, from general knowledge] | No. |
| **SDL3 / raylib (C/C++)** | Possible in principle by cross-compiling with clang against the extracted iPhoneOS SDK (SDL3 has CMake iOS support; [community write-up](https://demensdeum.com/blog/2026/07/19/linux-to-ios-crosscompile-sdl/)). raylib has no official iOS backend. [DOCS/UNVERIFIED] | Probably, by wrapping it as a SwiftPM C target or binary xcframework. xcframeworks improved with SwiftBuild (1.20.0), but there are open link issues (PR #369 compiler-rt). [UNVERIFIED] |
| **Pure Swift + SpriteKit/SceneKit/Metal** | ✅ Best fit for xtool | This is the recommended route if we want to stay on Linux. |

### 5.3 Building
- `xtool dev build [-c release] [--ipa] [--sign]` produces `xtool/<App>.app` or `.ipa` (arm64-apple-ios). [SOURCE/TESTED help]
- Requirements: Swift 6.4 toolchain (✅ installed) plus the Darwin SDK built from **Xcode.xip** (❌ needs an Apple ID download).
- `xtool sdk build <Xcode.xip|Xcode.app> <out> --arch x86_64` can build the SDK **on the Mac** and copy over a `darwin.xtoolsdk`. `xtool sdk install` then installs it on the box (two-phase flow, 1.18.1+). [SOURCE/DOCS]
- **The Mac's role is once per Xcode major version** (roughly yearly), plus when an xtool update forces an SDK reinstall (1.20.0 did).

### 5.4 Signing and provisioning
- **Free Apple ID ("password" mode):**
  - Works through private APIs, and has broken several times this year.
  - Apps expire after 7 days. xtool adds an `XTL-xxxx.` prefix to the bundle ID.
  - Many capabilities aren't available ([Apple capability matrix](https://developer.apple.com/help/account/reference/supported-capabilities-ios)): Game Center, In‑App Purchase, push, and others.
  - The docs suggest a throwaway Apple ID. [DOCS]
- **Paid Apple Developer Program ($99/yr), "API key" mode:** uses the public App Store Connect API (Team key, App Manager role). Recommended. [DOCS]
- xtool only creates **development** certificates and **iOS App Development** profiles (`certificateType: .development`, `.iosAppDevelopment`). There's **no distribution (App Store/Ad Hoc) signing in released xtool.** [SOURCE]
- xtool may ask to **revoke existing development certificates** the first time ([#15](https://github.com/xtool-org/xtool/issues/15)). Use a team without important dev certs, or answer carefully.
- The entitlement-to-capability mapping covers a short list: app groups, keychain, push (`aps-environment`), HomeKit, HealthKit, SiriKit, Wi‑Fi Aware, and a few others. **Game Center (`com.apple.developer.game-center`) is not mapped**, and unmapped entitlements fail late at install ([#352](https://github.com/xtool-org/xtool/issues/352)). [SOURCE/DOCS]
- In-App Purchase needs no entitlement, but StoreKit `.storekit` local test configs are Xcode-only. Sandbox testing on a device should work. [UNVERIFIED]

### 5.5 Installing on a device
- Locally, xtool installs over USB through usbmuxd. **The box has no USB**, so that's impossible here. [TESTED]
- Wi‑Fi deploy isn't natively supported on Linux ([#134](https://github.com/xtool-org/xtool/issues/134)). A netmuxd + pymobiledevice3 workaround exists, but it needs LAN reachability to the phone, which the box doesn't have.
- **Viable remote option [UNVERIFIED end-to-end]:** plug the iPhone or iPad into the Mac and forward its usbmuxd socket to the box:
  - On the Mac: `socat TCP-LISTEN:27015,bind=127.0.0.1,fork UNIX-CONNECT:/var/run/usbmuxd`
  - Then tunnel that port to the box (SSH `-R`/`-L` or Tailscale).
  - On the box: `export USBMUXD_SOCKET_ADDRESS=127.0.0.1:27015`.
  - I confirmed xtool honors that variable over TCP. Others use the same mechanism for WSL ([#19](https://github.com/xtool-org/xtool/issues/19#issuecomment-2898986718)).
  - It needs network reachability between the Mac and the box, which isn't verified.
- **Simplest option:** build the `.ipa` on the box, copy it to the Mac, and install with `xtool install` (xtool runs on macOS) or Xcode.
- Developer Mode must be enabled on the device. The first deploy registers the device.

### 5.6 Simulator
- **macOS only. Confirmed.** xtool's `--simulator` flag is inside `#if os(macOS)` and shells out to `xcrun simctl`. [SOURCE]
- The box can only *compile* simulator slices (the SDK includes iPhoneSimulator). It can't run them.

### 5.7 Debugging
- `xtool launch` starts an app through debugserver, but is reported broken on iOS 17+ ([#44](https://github.com/xtool-org/xtool/issues/44), open).
- There's no `xtool debug` / LLDB attach command. [SOURCE/DOCS]
- **Workaround [UNVERIFIED]:** the Swift toolchain ships `lldb`. pymobiledevice3 can start the iOS 17+ RSD tunnel and debugserver from Linux. This needs device reachability (see 5.5) and root for the TUN device.
- Logs and crashes: `idevicesyslog` and `idevicecrashreport` (libimobiledevice) over the same mux tunnel. [UNVERIFIED]
- **Practical answer:** use Xcode on the Mac when real step-through debugging is needed.

### 5.8 Profiling
- **Instruments is macOS only.** There's no Linux equivalent for GPU frame capture, Time Profiler, or Metal System Trace.
- Partial workarounds: in-game FPS and timing overlays, MetricKit payloads, `os_signpost` + syslog. [UNVERIFIED]
- The Mac is needed for serious performance work. Games need this periodically: before each release and for GPU tuning.

### 5.9 UI tests
- Not in released xtool. Open PR [#236](https://github.com/xtool-org/xtool/pull/236) implements on-device XCTest and XCUITest from Linux (testmanagerd/DTX, RSD tunnel) but isn't merged.

### 5.10 TestFlight / App Store upload
Released xtool has **no upload command and no distribution signing.** Issue [#117](https://github.com/xtool-org/xtool/issues/117) is still open. A user there reports that a re-signed xtool IPA was rejected by Apple ("Missing DTPlatformName", then "Unsupported SDK or Xcode version"). He finally shipped by making `Assets.car` with `actool` and uploading through Xcode.

Known gaps for an App Store-valid IPA built on Linux, each with its open PR:
| Gap | PR |
|---|---|
| Distribution signing with an existing `.p12` and App Store profile | [#326](https://github.com/xtool-org/xtool/pull/326) (draft) |
| `DT*` Xcode/SDK Info.plist stamps | [#323](https://github.com/xtool-org/xtool/pull/323) |
| `LC_BUILD_VERSION` records the deployment target instead of the SDK version | [#321](https://github.com/xtool-org/xtool/pull/321) / issue [#351](https://github.com/xtool-org/xtool/issues/351) |
| `Assets.car` + `CFBundleIconName` (required icon) | [#325](https://github.com/xtool-org/xtool/pull/325) (AssetKit output hangs in ASC processing) |
| `xtool upload` through Apple's public **Build Uploads** API | [#327](https://github.com/xtool-org/xtool/pull/327) |

Linux upload tools that exist today (outside xtool):
- **App Store Connect API "Build Uploads"** (public REST: create upload → reserve file → PUT parts → commit) works from any OS with an API key ([Apple docs](https://developer.apple.com/documentation/appstoreconnectapi/build-uploads)). [DOCS]
- **iTMSTransporter for Linux** (RHEL build) is official, but on Linux it requires `-assetDescription AppStoreInfo.plist`, which is **generated by Xcode/`swinfo` on macOS** ([Transporter guide](https://help.apple.com/itc/transporteruserguide/en.lproj/static.html)). [DOCS]
- **fastlane pilot on Linux** wraps Transporter the same way and also needs the Mac-made `AppStoreInfo.plist` ([fastlane docs](https://docs.fastlane.tools/actions/pilot/)). [DOCS]
- Apple's current rule: iOS apps must be **built with the Xcode 26+ SDK** to upload ([Apple help](https://developer.apple.com/help/app-store-connect/manage-builds/upload-builds/)). The xtool docs target Xcode 27, which is fine as long as the SDK stamps are right. [DOCS]

### 5.11 App Store submission, metadata, screenshots, icons
- **Metadata, review submission, TestFlight groups:** all possible with the App Store Connect API / fastlane `deliver` and an API key from Linux. [DOCS/UNVERIFIED for deliver on Linux]
- **Screenshots:** the Simulator isn't available. Options:
  - Capture on real devices (an iPhone 6.9"/6.5" class and an iPad 13" are required sizes).
  - Render marketing images on the box.
  - Run the Simulator on the Mac once per release.
  - Upload through the ASC API.
- **App icons:**
  - For development, `iconPath:` with a 1024×1024 PNG is enough (sets `CFBundleIconFile`).
  - For the App Store, an `Assets.car` with an AppIcon is required in practice.
  - Workaround: compile `Assets.car` with `actool` on the Mac once (and again whenever the icon changes), then ship it as a root resource. This needs the PR #323-style plist keys (`CFBundleIconName`, `CFBundleIcons`) added through `infoPath`. [DOCS/UNVERIFIED]
- **Privacy manifest:** a plain plist, so it works (see 5.1).

---

## 6. Gap list

### Works fully on Linux (box), with xtool as released
- Writing Swift/SwiftUI/SpriteKit/SceneKit/Metal-API game code, with LSP and editor support
- Project scaffolding (`xtool new`) **[TESTED]**
- Compiling and linking iOS arm64 apps from SwiftPM, iPhone and iPad (once the SDK is installed) [DOCS]
- Development signing and provisioning (free or paid), including device registration and App ID creation [DOCS/SOURCE]
- Building `.ipa` (`--ipa`), Info.plist overrides, entitlements (limited set), bundle resources, privacy manifest, simple PNG app icon [SOURCE]
- App extensions (widgets) [DOCS]
- Pure-logic unit tests with `swift test`
- App Store Connect metadata and API tasks with an API key (via API / fastlane) [DOCS]

### Works with workarounds
| Need | Workaround | Mac needed? |
|---|---|---|
| Device install/run from a box with no USB | Forward the Mac's usbmuxd over TCP + tunnel (`USBMUXD_SOCKET_ADDRESS`), or copy the `.ipa` to the Mac and `xtool install` there | Mac (or any Linux/Windows PC) physically connected to the device, every device test session |
| Metal shaders | Runtime `makeLibrary(source:)`, or a prebuilt `.metallib` | Only if prebuilding |
| Asset catalogs / texture atlases | Loose PNGs + runtime loading. App icon via `iconPath`. | No (for dev) |
| Store-valid icon | `actool`-compiled `Assets.car` shipped as a resource | Once, and whenever the icon changes |
| C/C++ engines (SDL3) | Cross-compile against the SDK, wrap as a SwiftPM target | No [UNVERIFIED] |
| Godot/Unity output | Export on Linux, `xcodebuild` on Mac, sign/install with xtool | Every build |
| Debug logs and crashes | libimobiledevice over the mux tunnel | Same as device |
| Distribution signing + upload | Unmerged PRs #321/#323/#325/#326/#327 (build xtool from a branch), or the ASC Build Uploads API via a script | Possibly none once merged, but `Assets.car` still needs `actool` |

### Truly needs the Mac
| Need | How often |
|---|---|
| **Xcode.xip download** (an Apple ID login in a browser, possibly on the box's browser) | Once per Xcode major (~yearly) and on SDK-layout breaking changes |
| iOS Simulator (testing, screenshots at every required size) | Per release, or whenever device-free UI testing is wanted |
| Instruments / GPU frame capture / Metal debugger | Periodically (performance passes, before release) |
| Interactive LLDB debugging (reliable path) | As needed for hard bugs |
| Physical USB connection to the iPhone/iPad (the box has none) | Every on-device test session (the Mac acts as the USB host) |
| `actool` (`Assets.car`), storyboards, `.scnassets`, `.reality`, `.storekit` configs | When those assets change |
| App Store upload, **today** (Xcode Organizer / Transporter, or the Mac-generated `AppStoreInfo.plist` for Linux Transporter) | **Every release** until xtool's upload PRs land and are proven |
| Any Godot/Unity/Unreal iOS build | Every build |
| Creating a distribution certificate/profile | Usually once a year. The paid-account web portal can do it from any browser, and the ASC API too, so the Mac isn't strictly required. |

---

## 7. Accounts and credentials needed (none created or used)
1. **Apple ID** (free) to download Xcode.xip from developer.apple.com. This is a browser login with 2FA; the editor would do it.
2. For device testing:
   - Either the same or a **throwaway free Apple ID** ("password" mode, private APIs, 7-day apps, no Game Center/IAP),
   - Or, better, **Apple Developer Program membership ($99/yr)** plus an **App Store Connect API Team key** (`.p8`, Key ID, Issuer ID, **App Manager** role).
3. For TestFlight and App Store: the paid membership is mandatory, plus an Apple Distribution certificate (`.p12`) and an App Store provisioning profile (if using PR #326), or Xcode's automatic signing on the Mac.
4. Optional: SSH/Tailscale between the Mac and the box for the usbmuxd tunnel.

---

## 8. Artifacts on the box
- `/workspace/xtool-eval/xtool`: xtool 1.22.2 AppImage
- `/workspace/xtool-eval/toolchain/usr/bin`: Swift 6.4.1 (prepend to PATH; 3.6 GB)
- `/workspace/xtool-eval/HelloGame`: SpriteKit sample game (SwiftPM + `xtool.yml`), ready for `xtool dev build` once an SDK is installed
- `/workspace/xtool-eval/src`: shallow clone of xtool (source review)
- System packages added with apt: `usbmuxd`, `libimobiledevice-utils`, `socat`. No daemons left running.

## 9. Suggested next steps
1. Get the Xcode 27 `.xip` (the editor logs in once). Run `xtool sdk install --slim` on the box, or `xtool sdk build … --arch x86_64` on the Mac and copy it over. Then run `xtool dev build` on HelloGame to confirm SpriteKit compiles on Linux.
2. Decide between a free Apple ID and the paid program. Paid is effectively needed to publish, and for Game Center or IAP.
3. Test the Mac → box usbmuxd tunnel with a real iPhone.
4. Track xtool PRs #321, #323, #325, #326, #327, and #236. If they merge, a per-release Mac visit may stop being necessary.
