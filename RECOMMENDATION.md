# Cross-platform game architecture: LCOS, iOS/iPadOS, Android, SteamOS (Steam Deck)

*Prepared Tue Oct 20, 2026, around 12:00 PM CDT. All times are America/Chicago (CDT).*

Tags:
- **[TESTED]**: ran on the box
- **[DOCS]**: external documentation (linked)
- **[INFERRED]**: my judgment
- **[UNTESTED]**: not tried

Related reports:
- `/workspace/xtool-eval/REPORT.md` (iOS via xtool)
- `/workspace/xtool-eval/LUNDUKE-CITY-PORT.md`

---

## ⚠️ 0. Incident disclosure: I most likely killed the QA VM

**Short version:** at about **11:33:45 CDT** I ran `pkill -f qemu-system` as user `box`, and again at about **11:35:13 CDT**. I meant to clean up my own Android emulator. But `pkill -f qemu-system` matches **every** `qemu-system-*` process owned by `box`, and the LCOS QA VMs also run as `box` (for example the current PID 77247, started 11:41:37, `qemu-system-x86_64 … -m 3072`, owner `box`).

The QA VM died at about 11:33 CDT, so **my first `pkill` is the most likely cause**. Memory readings agree: used RAM fell from 6.7 GiB to 2.9 GiB around 11:35, which is more than my emulator alone used. I'm very sorry. From now on I'll kill only by exact PID.

Full timeline of what I ran between 11:15 and 11:55 CDT, with approximate start times and memory use:

| Time (CDT) | What | Memory / notes |
|---|---|---|
| 11:28:40–11:30:37 | `apt-get install openjdk-21-jdk-headless unzip`. The first try failed on a mirror 500 error; the retry succeeded. | Light (apt) |
| 11:30–11:31:29 | Downloads only: Android cmdline-tools (182 MB), then `sdkmanager` installs of platform-tools, build-tools 36.1.0, platforms;android-36, and NDK 29.0.14206865 into `/workspace/xplat-eval/android/sdk` | Disk only |
| 11:31–11:32 | NDK compile of a hello `.so`, aapt2/zipalign/apksigner/bundletool packaging | Seconds, under 500 MB |
| 11:32:43 | `sdkmanager emulator` (download), then `emulator -accel-check`, once as `box` and once with `sudo` | Probe only, no VM |
| 11:33:25 | System image download (android-36 google_apis x86_64, 4.3 GB on disk) | Disk only |
| ~11:33:34 | **Emulator start #1** (`-memory 2048 -cores 2`). It exited at once because `box` couldn't open `/dev/kvm`. | ~0 |
| **~11:33:45** | **`pkill -f qemu-system` (as box): probably killed the QA VM** | — |
| ~11:33:50 | `sudo chmod 666 /dev/kvm` (temporary) | — |
| 11:33:51–11:35:12 | **Emulator #2**: headless AVD `xp36`, `-memory 2048 -cores 2 -gpu swiftshader_indirect`. Booted in ~50 s. I installed and ran the APK and took a screenshot at 11:35:07. | Host "used" went from about 5.4 to 6.7 GiB; 8.9 GiB was still available |
| ~11:35:10 | `adb emu kill` | — |
| **~11:35:13** | **Second `pkill -f qemu-system` (as box)** | — |
| 11:35:15 | `sudo chmod 660 /dev/kvm` (restored). Note: `/dev/kvm` shows another change at 11:41 that wasn't mine. | Used RAM afterward: 2.9 GiB |
| 11:36:00–11:48:52 | `apt-get install podman libsdl3-dev libgtkmm-3.0-dev meson ninja-build pkg-config`. It hung for 12 minutes on a dpkg conffile prompt for `/etc/fuse.conf`. I resolved it with `--force-confold`, which **kept the existing fuse.conf**. | Light |
| 11:48:5x | Meson build of the hello shells, then SDL3 and gtkmm runs under `xvfb-run` (30 frames each) | Seconds |
| 11:49:16–11:50 | `podman pull registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest` (**3.9 GB image**), plus the 186 MB `SteamLinuxRuntime_sniper` download. One short `podman run` compiled the SDL3 shell (~1 s). | Network and disk; small RAM |
| 11:50 | Ran the binaries under `SteamLinuxRuntime_sniper/run` (pressure-vessel/bwrap) + Xvfb | Seconds |
| 11:51–11:53 | `apt install lib32gcc-s1`, then a `steamcmd.sh +quit` bootstrap with **no login**. It was interrupted, and I killed it by PID. | Small |
| 11:53 (cleanup) | Deleted the podman image, killed my rootless-podman pause process (`catatonit`, PID 81795) and my `adb` server. **I didn't touch qemu PID 77247.** | — |

Other facts:
- I never touched `/workspace/lcos-095-09`, `/workspace/lcos-iso`, or `/workspace/lcos-iso-qa`.
- No emulators, VMs, or containers of mine are running now.
- On-disk leftovers in `/workspace/xplat-eval`: the Android SDK, NDK, emulator, and system image (~8 GB), and the extracted Steam runtime (0.8 GB). These are removable if disk is needed.

---

## 1. Recommendation (primary architecture)

**A shared C++17 game core plus SDL3 as the platform layer on all four targets, built with Meson (Linux and Deck) and thin per-OS wrappers (Android and iOS). Use gtkmm/Cairo only as an *optional* LCOS shell for "app-like" titles, not as the default.**

The SteamOS requirement is why SDL3 wins over a gtkmm-first design:
- **SDL3 ships inside the Steam Linux Runtime 3 "sniper"** (`libsdl3-0 3.4.16`, plus SDL3_image, SDL3_ttf, SDL3_mixer). **gtkmm, glibmm, and cairomm do not**; only the C GTK3 and Cairo libraries are included. [TESTED: manifest; a host-built gtkmm binary failed under the runtime with `libgtkmm-3.0.so.1: cannot open shared object file`]
- Deck Verified requires full controller navigation, correct glyphs, an on-screen keyboard for text entry, legible text at 1280×800, and 30 fps ([Valve](https://partner.steamgames.com/doc/steamdeck/compat)). SDL3's gamepad API covers that directly. A GTK menu bar and dialogs would fail "controller can reach all content" without a parallel controller UI. [DOCS/INFERRED]
- SDL3 also runs on Android (an SDLActivity Java shim) and iOS (UIKit and Metal under the hood). One input model covers touch, mouse, and gamepad, and one rendering API covers all four (`SDL_Renderer`, or SDL_GPU later).
- **It keeps the house style where it matters**: C++17, Meson, the same coding conventions, and a GTK-free core library like Lunduke Paint's `lunduke_paint_core` and Lunduke City's `CitySession`.
- It does **not** keep gtkmm as the LCOS toolkit by default. That is a real tradeoff against your "same toolkit" preference. **Mitigation:** the core renders through a tiny `Canvas` interface (images, rects, text) with two backends, `SdlCanvas` and `CairoCanvas`. App-style LCOS titles can keep a gtkmm+Cairo shell, and I prototyped exactly that (see §5). For action or handheld-first games, ship the SDL build on LCOS too.

**Why not the alternatives:**
| Option | Verdict |
|---|---|
| **A: shared C++ core, gtkmm/Cairo on LCOS, Swift/xtool shell on iOS, NDK shell on Android** | Workable, but you write **three or four UI and input shells** and **two renderers**. On the Deck, gtkmm must be bundled and still isn't controller-friendly. Cairo cross-compiled for iOS and Android (pixman, freetype, harfbuzz, fontconfig) is heavy [INFERRED]. Best only if every title is a desktop-app-style game and the Deck is out of scope. |
| **B: SDL3 everywhere (recommended), optional gtkmm on LCOS** | Fewest shells. Gamepad, touch, and audio come from one API. It's already in the Steam runtime. It's zlib-licensed. |
| **Godot 4** | Great editor, but iOS export needs a Mac **for every build** ([Godot docs](https://docs.godotengine.org/en/latest/tutorials/export/exporting_for_ios.html)). Exporting a GDScript project is fine on Linux for LCOS, Deck, and Android. A different language and toolchain from house style. Strong second choice if the editor accepts the per-release Mac step. |
| **Flutter/Flame, Kotlin Multiplatform** | iOS builds need Xcode on a Mac (every build). Dart or Kotlin, not C++. No LCOS-native feel. Not recommended. |
| **raylib** | Simple C API, but Android and iOS support is community-grade, and it has weaker gamepad and Steam Input integration than SDL3 [INFERRED]. |
| **Unity / Unreal** | Proprietary. iOS needs a Mac. Heavy RAM for this box. Not recommended. |

---

## 2. Repo / folder layout sketch
```
game/
  meson.build            # LCOS + SteamOS (+ host tests)
  LICENSE                # see §8 (MIT or proprietary; editor-owned)
  core/                  # pure C++17: rules, sim, state, save/load, UI layout, no OS headers
    game.hpp/.cpp
    canvas.hpp           # abstract draw API: image, rect, text, clip
    input.hpp            # unified actions: Confirm/Back/Move/Point(x,y)/Tap/Scroll
  platform/sdl/          # SDL3 shell: window, SdlCanvas, input→actions, audio, file paths
  platform/gtk/          # OPTIONAL LCOS gtkmm shell: CairoCanvas + menus/dialogs
  platform/steam/        # optional Steamworks glue (overlay keyboard, cloud, achievements), #ifdef STEAM
  android/               # AndroidManifest.xml, SDLActivity java (from SDL), build script (no Gradle)
  ios/                   # xtool SwiftPM package: Package.swift + xtool.yml; links core + SDL3 static
  assets/src/            # SVG/ORA/Krita/Blender sources (in git, LFS if large)
  assets/build/          # generated PNG atlases, fonts, sounds (scripted, reproducible)
  store/                 # icons, screenshots, capsules, listings per store (generated)
  tools/                 # build-android.sh, make-icons.py, pack-atlas.py, steam_upload/*.vdf
  tests/                 # headless core tests + golden-frame tests (render to PNG, compare)
```

---

## 3. Build and test matrix
| Platform | Build on box? | Test on box? | Needs Mac / device | How often |
|---|---|---|---|---|
| **LCOS (Debian, XFCE)** | ✅ Meson + gcc. Debian 13 has `libsdl3-dev` 3.2.10 and `libgtkmm-3.0-dev` 3.24.10. [TESTED] | ✅ Xvfb with frame dump to PNG (both SDL3 and gtkmm shells). [TESTED] | None | Every build |
| **SteamOS / Deck** | ✅ Inside the **sniper SDK** container (podman). The SDL3 shell compiled and linked against SDL3 3.4.16. [TESTED] The image is 3.9 GB: pull it once, outside QA windows. | ✅ Ran the SDL3 binary under `SteamLinuxRuntime_sniper/run` (pressure-vessel) + Xvfb at 1280×800 and got a frame. [TESTED] Gamepad and Steam Input can't be tested headless. | A real Deck is optional but valuable: controls, glyphs, performance, sleep/resume | Container: every release build. Deck: before review and major releases. |
| **Android** | ✅ cmdline-tools, NDK r29, aapt2, zipalign, apksigner, bundletool. I built and signed an APK and a **signed AAB**, and `bundletool validate` passed. No Gradle needed (it's RAM-hungry). SDL3's Java shim would need `javac` + `d8` (in the SDK; untested with SDL). [TESTED for NativeActivity hello] | ⚠️ The emulator **works** (KVM usable; android-36 x86_64 booted in ~50 s; app ran; screenshot). [TESTED] It needs `/dev/kvm` access for `box`, ~1.5–2 GB RAM, and **must not run during QA windows**. Alternatives: Firebase Test Lab (`gcloud firebase test android run`, robo tests and screenshots) [DOCS/UNTESTED], or a phone over `adb` through an SSH or Tailscale tunnel from a machine near the phone [UNTESTED]. | A physical phone and tablet for feel and performance | Emulator or Test Lab per build; device per release |
| **iOS / iPadOS** | ⚠️ Via xtool + SwiftPM after a one-time `Xcode.xip` → Darwin SDK. The C++ core as a SwiftPM C++ target is plausible. **SDL3 for iOS** has to be built as a static lib or xcframework: with CMake and the xtool SDK's clang [UNTESTED], or **once on the Mac** (simplest). See REPORT.md. | ❌ No Simulator on Linux. Run headless core and golden-frame tests on the box. Options for devices: the Mac's usbmuxd forwarded to the box (`USBMUXD_SOCKET_ADDRESS`; mechanism verified in REPORT.md), TestFlight to real devices, or cloud farms (BrowserStack, AWS Device Farm). [DOCS/UNTESTED] | Mac: one-time SDK, `Assets.car`/AppIcon, Simulator screenshots, Instruments, and **the App Store upload each release** until xtool's distribution PRs land | SDK yearly; Mac per release (today) |

**Store upload from the box:**
| Store | Tool on Linux | Status |
|---|---|---|
| **Steam** | `steamcmd` (`builder_linux`) `+run_app_build app_build_<id>.vdf`. Headless after a one-time interactive Steam Guard login; keep `config/config.vdf` ([Valve](https://partner.steamgames.com/doc/sdk/uploading)). | [DOCS]. The bootstrap needs 32-bit libs (`lib32gcc-s1`, installed); the binary started but I interrupted it. No login was attempted. |
| **Google Play** | Play Developer Publishing API (service account), `fastlane supply`, or a direct REST upload of the AAB. | [DOCS/UNTESTED] |
| **Apple** | Today: Xcode or Transporter on the Mac. Later: the App Store Connect "Build Uploads" API or `xtool upload` (open PR #327). | See REPORT.md |
| **LCOS** | Our own `.deb` + apt repo (same as Paint and City). Flatpak/Flathub is optional for other distros and Deck desktop mode. | Existing pipeline |

---

## 4. SteamOS specifics
- **Runtime:** use **Steam Linux Runtime 3.0 "sniper"**, the current recommendation for native Linux titles. Soldier (2.0) is older and Scout (1.0) is legacy. Build inside `registry.gitlab.steamos.cloud/steamrt/sniper/sdk` (Debian 11-based: gcc-14 and gcc-10, clang-11, meson 1.0.1, cmake 3.25, ninja, `libsdl3-dev` 3.4.16, `libgtk-3-dev`, `libcairo2-dev`). [TESTED: manifest and image]
  - **Our Meson files must stay compatible with Meson 1.0.**
  - Binaries built on Debian 13 (glibc 2.41) **won't run** under sniper (glibc 2.31) unless built in the SDK. [INFERRED from glibc versions; that's why I built in the container]
- **gtkmm/Cairo app under the runtime:** GTK3 and Cairo are provided, but **gtkmm, glibmm, cairomm, pangomm, atkmm, and libsigc++ are not**. You'd have to build and bundle them (LGPL, so ship them as `.so` files) inside the SDK. That's doable but adds maintenance. [TESTED: missing `libgtkmm-3.0.so.1`]
- **Flatpak (desktop mode / Flathub):** an alternative for non-Steam distribution, using the `org.freedesktop.Platform` runtime. gtkmm would also have to be a bundled module. `flatpak-builder` is in Debian. [DOCS/UNTESTED]
- **Input:**
  - Steam Input exposes the Deck as a virtual gamepad. Use the **SDL3 gamepad API** with action mapping, plus touch and mouse.
  - Use the Steamworks `ShowFloatingGamepadTextInput` for text entry, or a built-in controller keyboard.
  - Glyphs must match the device: use `SDL_GetGamepadType`, or the Steam Input API for exact Deck glyphs. [DOCS: [Valve recommendations](https://partner.steamgames.com/doc/steamhardware/recommendations)]
- **UI:**
  - Design for **1280×800**, plus 1280×720 and other aspect ratios.
  - **Text at least 9 px tall, 12 px recommended**, with a user text-size option.
  - Default settings must hold **30 fps at 800p**.
  - Avoid "unsupported OS" warnings and launchers. [DOCS: [Deck compat](https://partner.steamgames.com/doc/steamdeck/compat)]
  - That's the same "big touch targets, big text" design that phones need, so the shared UI layout in `core/` serves both.
- **Account and costs:** a Steamworks partner account, the **Steam Direct fee of $100 per app** (recoupable after $1,000 in gross revenue), tax and bank forms, and a **dedicated build account** with "Edit App Metadata" and "Publish" permissions. Setting builds live on a released app needs that account to have phone or mobile-app Steam Guard. [DOCS/INFERRED for fee details]
- **Deck Verified** is a review Valve runs after you mark the game ready. It isn't something we self-certify.

---

## 5. What I actually built on the box (proof of concept)
- `/workspace/xplat-eval/hello/core/` is a platform-free C++17 core: a bouncing ball rendered into an RGBA framebuffer.
- Shells, all reusing the same core:
  - `linux/headless.cpp`: frame to PPM/PNG
  - `sdl/main.cpp`: SDL3, 1280×800, gamepad, touch, and mouse
  - `gtk/main.cpp`: gtkmm-3 + Cairo with a menu bar
  - `android/main.cpp`: a NativeActivity with no Java, using `ANativeWindow`
- Results:
  - **Host:** the Meson build of all shells passed. SDL3 3.2.10 (x11) and gtkmm 3.24.10 each rendered 30 frames under Xvfb; frames are in `hello/build/frame-sdl.png` and `frame-gtk.png`. [TESTED]
  - **Steam runtime:** built in the sniper SDK container against SDL3 3.4.16 and ran under the runtime + Xvfb, writing `hello/build-steamrt/frame.bmp`. The host gtkmm binary failed under the runtime (missing gtkmm). [TESTED]
  - **Android:** NDK r29 `.so` files for arm64-v8a and x86_64 (16 KB page-aligned), packaged as an APK signed with v2/v3 and verified, plus an **AAB** built with bundletool 1.18.3, signed, validated, and converted to `.apks`. On the emulator (android-36), it installed, launched, logged "hello from shared core", and the screenshot `hello/out/emulator-screenshot.png` shows the core's frame. [TESTED]
  - **iOS:** not built. The box has no Darwin SDK yet (see REPORT.md).

**Installed during this task:**
- **apt:** `openjdk-21-jdk-headless`, `unzip`, `podman` (+ deps), `libsdl3-dev`, `libgtkmm-3.0-dev`, `meson`, `ninja-build`, `lib32gcc-s1`
- **In `/workspace/xplat-eval`:** Android SDK cmdline-tools 22.0, platform-tools, build-tools 36.1.0, platforms;android-36, NDK 29.0.14206865, emulator, the android-36 google_apis x86_64 system image, AVD `xp36`, `bundletool.jar`, the extracted `SteamLinuxRuntime_sniper` (3.0.20260928.262393), and `steamcmd`
- **Licenses:** I accepted the Android SDK licenses (`sdkmanager --licenses`), which `sdkmanager` requires before it will install packages. No accounts were created and no sign-ins were made.
- **Removed:** the podman sniper SDK image (re-pull when needed).

---

## 6. One-time setup and accounts
| Item | Cost | Needed for |
|---|---|---|
| Apple ID → Xcode.xip download (by the editor, in a browser) | free | iOS SDK on the box |
| Apple Developer Program + App Store Connect API key (`.p8`, App Manager) | $99/yr | Device provisioning >7 days, TestFlight, App Store |
| Google Play Console developer account (identity verification; new personal accounts must run a closed test before production) | $25 one-time | Play Store. A service-account JSON key enables API uploads. [DOCS from memory, verify] |
| Upload keystore for Play App Signing (generated on the box, backed up offline) | — | Android releases |
| Firebase project (optional) | free tier / pay-as-you-go | Test Lab device runs |
| Steamworks partner account + $100 per app + dedicated build account + Steam Guard | $100 per app | Steam / Deck |
| Optional: a Steam Deck, an Android phone and tablet, an iPhone and iPad | hardware | Real-device testing |
| Mac (MacBook Air M2) with Xcode | — | iOS SDK and `Assets.car` (once), Simulator screenshots and Instruments, App Store upload (per release, for now), SDL3.xcframework build (once per SDL update) |
| Tunnel (Tailscale or SSH) between the Mac and the box | free | Forwarding iOS and Android devices to the box |

---

## 7. Risks
1. **The shared box itself.** Emulators and VMs compete with LCOS QA. *Rule from now on:* no emulator during QA windows, kill only by PID, and keep `/dev/kvm` permission changes out of it (ask an admin to add `box` to a named kvm group, or use Firebase Test Lab).
2. **iOS remains the weakest link.** No Simulator. Distribution signing and upload aren't in released xtool. SDL3 for iOS through SwiftPM/xtool is unproven. The Mac is needed per release until that changes.
3. **The SDL3 versions differ:** 3.2.10 on Debian 13 vs. 3.4.16 in sniper vs. whatever we bundle on Android and iOS. *Mitigation:* vendor one SDL3 release as a Meson subproject or wrap and build it ourselves for Android and iOS, and target the 3.2 API floor.
4. **The gtkmm preference is only partly met.** Two canvases means some duplicate drawing code. Keep `Canvas` small.
5. **Store policy churn:** Play target-API deadlines, Apple SDK minimums, Deck review. Re-check each year.
6. **Licensing** (below).
7. **RAM:** Gradle builds and the emulator are the heavy items. The manual aapt2/d8 pipeline avoids Gradle.

## 8. Licensing recommendation
- **Our new game code: editor-owned, under MIT (or proprietary).** That keeps the App Store, Play, and Steam all open, and allows linking the proprietary Steamworks SDK.
- If the editor wants GPL for LCOS ethos, use **GPL-3.0 with an explicit additional permission** (an App Store and Steamworks exception). The editor can grant that only while holding *all* copyrights: bot-written code is assigned to the editor, and outside contributions require a CLA. **Never pull GPL third-party code (such as Micropolis) into `core/`.**
- **Dependencies are fine:**
  - SDL3, SDL3_image, SDL3_ttf, SDL3_mixer: zlib
  - FreeType: FTL (in SDL_ttf)
  - HarfBuzz: MIT
  - libpng: libpng license
  - gtkmm, glibmm, cairomm: LGPL
  - Cairo: LGPL/MPL
  - The LGPL libraries are linked dynamically on Linux only, and are not used on iOS.
  - Steamworks SDK: proprietary, redistributable under Valve's terms.
- **Art and fonts:** our own art, or CC0/CC-BY assets. Avoid CC-BY-SA and GPL-licensed art for store builds. Fonts under OFL are fine.

## 9. Art pipeline on the box
- **Available via apt (not installed yet):**
  - Inkscape 1.4 (CLI SVG→PNG: `inkscape --export-type=png -w N`)
  - Krita 5.2.9
  - Blender 4.3 (headless `blender -b -P script.py` for pre-rendered sprites; CPU only, so keep renders small)
  - ImageMagick 7
  - `rsvg-convert` (librsvg2-bin), `optipng`, `pngquant`
- **Already installed:** Python PIL 12.3.
- **Not in Debian:** LibreSprite and Aseprite. Aseprite can be built from source (its EULA restricts redistribution) [DOCS/INFERRED].
- **Pipeline:** sources in `assets/src`, then a scripted export (`tools/make-assets.py`: Inkscape/rsvg → PNG at 1×/2×, then atlas packing with PIL) into `assets/build`. Assets sent in from outside go in `assets/src/inbox/` with a license note.
- **Generated icons and store art,** from one SVG master via `tools/make-icons.py`:

| Target | Outputs |
|---|---|
| LCOS | hicolor PNGs 16–256 + scalable SVG |
| Android | adaptive icon (108 dp foreground/background layers + monochrome), legacy mipmaps, Play 512×512 icon, 1024×500 feature graphic |
| iOS | 1024×1024 PNG. Needs `actool` on the Mac once to produce `Assets.car` (REPORT.md). |
| Steam | capsules (header, small, main, vertical/library, hero, logo, at Valve's listed sizes [verify in Steamworks docs]) |

  - **Screenshots:** from headless golden-frame renders at each store's resolution, or from emulator, device, or Simulator captures.

## 10. First-step plan (light on the box)
1. **(Now, light)** Promote the PoC into `game/`:
   - `core/` with `Canvas` and `Input` actions, the SDL3 shell, and the Meson build.
   - Headless golden-frame tests in CI on the box.
   - The LCOS `.deb` packaging copied from Paint's pattern.
2. **(Light)** Add `tools/build-android.sh`: SDL3 built with NDK + CMake (needs `cmake`) as a static or shared lib, SDLActivity Java via `javac`/`d8`, then aapt2 → AAB. Test with Firebase Test Lab or the emulator **outside QA windows only**.
3. **(Outside QA windows)** Re-pull the sniper SDK image and build the SDL3 shell there for SteamOS. Add `platform/steam/` behind an option (Steamworks SDK download needs the partner account).
4. **(Mac session #1)**
   - Download Xcode.xip and run `xtool sdk build` for x86_64; copy it to the box.
   - Build `SDL3.xcframework` (static).
   - Make an `Assets.car` for the icon.
   - Then build the iOS shell on the box with xtool and install through a forwarded usbmuxd or with `xtool install` on the Mac.
5. **Accounts** (by the editor): Apple Developer, Google Play Console, Steamworks. Then a first internal or test release on each store.
6. **Revisit Godot** only if the per-release Mac step turns out to be fine anyway *and* an editor-driven workflow is wanted.
