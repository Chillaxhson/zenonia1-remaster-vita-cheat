# Zenonia 1 Remaster Vita - Cheat Edition

<p align="center"><img src="./extras/screenshots/screenshot1.jpg"></p>

This is an enhanced fork of the **Zenonia 1 Remastered** port for the *PlayStation Vita*, featuring an integrated hotkey cheat engine, save system freeze fix, and save corruption remedies.

The port works by loading the Android ARMv7 executables from the unofficial Android remaster by Ill-Hovercraft8548 in memory, resolving imports with native functions, and applying dynamic runtime patches.

---

## 🎮 Cheat Hotkeys

Cheats are activated in-game by holding **<kbd>L1</kbd>** and pressing a face or trigger button. Inputs during combinations are captured and suppressed so normal gameplay actions (such as menu opening or attacking) do not trigger unintentionally.

| Combination | Action | Description |
|---|---|---|
| **<kbd>L1</kbd> + <kbd>SELECT</kbd>** | **Toggle 50x EXP** | Toggles a 50x Experience Points multiplier ON / OFF |
| **<kbd>L1</kbd> + <kbd>SQUARE</kbd>** | **Refill HP & SP** | Instantly restores HP to 100% and SP to 999 |
| **<kbd>L1</kbd> + <kbd>TRIANGLE</kbd>** | **Add 10,000 Gold** | Adds +10,000 Gold directly to your inventory |
| **<kbd>L1</kbd> + <kbd>CIRCLE</kbd>** | **Add 5 Stat Points** | Adds +5 unassigned Character Stat Points |
| **<kbd>L1</kbd> + <kbd>CROSS</kbd>** | **Add 5 Skill Points** | Adds +5 unassigned Skill Points |
| **<kbd>L1</kbd> + <kbd>START</kbd>** | **Toggle God Mode** | Toggles invulnerability (damage immunity) ON / OFF |

---

## 🛠️ Save System & Stability Fixes

This fork fixes critical issues present in the original port:

1. **Save Freezing Fix**:
   - Resolved an issue where saving would freeze the game in an infinite retry loop at 4 FPS due to corrupted path resolution.
   - Filesystem calls now cleanly resolve directly to `ux0:data/zenonia1/`.
2. **"Savefile corrupted, please create a new character" Fix**:
   - Fixed a struct misalignment in `stat64_bionic` where file size was offset by 10 bytes, causing the game's file loader to report 0-byte save files.
   - Bypassed anti-tamper checksum and player level verification checks between `Save0.dat` and `option.sav` that caused valid savefiles to be rejected upon selecting "Continue".
   - Suppressed false-positive corruption popups in `CMvGameState::Initialize`.

---

## 🕹️ Standard Controls

- **Left Analog / D-Pad**: Move character / Navigate menus
- **Cross**: Attack / Confirm selection
- **Triangle**: Open In-Game Menu
- **R Trigger**: Skip dialogue / Rotate quick skill bar
- **Right Analog**: Use Skills

---

## 📥 Setup Instructions (For End Users)

### Prerequisites
- Install [kubridge](https://github.com/TheOfficialFloW/kubridge/releases/) and [FdFix](https://github.com/TheOfficialFloW/FdFix/releases/) by copying `kubridge.skprx` and `fd_fix.skprx` to your taiHEN plugins folder (`ux0:tai/`) and adding them under `*KERNEL` in `config.txt`:
  ```
  *KERNEL
  ux0:tai/kubridge.skprx
  ux0:tai/fd_fix.skprx
  ```
  *(Note: Do not install `fd_fix.skprx` if you already use the `rePatch` plugin).*
- Install `libshacccg.suprx` if not already installed ([Extraction Guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx)).
- *(Optional)*: Install [PSVshell](https://github.com/Electry/PSVshell/releases) to overclock to 500 MHz for smoother performance.

### Installation
1. Install `zenonia1.vpk` from the Releases tab on your PS Vita (or copy `eboot.bin` to `ux0:app/ZENONIA01/eboot.bin`).
2. Obtain your copy of *Zenonia 1 Remaster* APK.
3. Extract the `assets/` and `res/` directories from the APK to `ux0:data/zenonia1/`.
4. Extract `libgameDSO.so` from the APK's `lib/armeabi-v7a/` directory, place it into `ux0:data/zenonia1/`, and rename it to `libzenonia1.so`.
   *(Note: You do not need to modify `libzenonia1.so`; all cheats and fixes are injected at runtime by `eboot.bin`).*

---

## 🏗️ Build Instructions (For Developers)

> [!IMPORTANT]
> The Android library `libzenonia1.so` was compiled with `softfp` float ABI. Compiling the loader with `hard` float ABI will corrupt floating-point coordinates and matrix projections, leading to severe visual bugs and broken touch/button alignment. Always use the `vitasdk-softfp` toolchain.

Build using **Docker** or **Podman**:

```bash
# Clone the repository
git clone https://github.com/Chillaxhson/zenonia1-remaster-vita-cheat.git
cd zenonia1-remaster-vita-cheat

# Compile using the softfp toolchain
podman run --rm -v "$(pwd):/src:z" -w /src/build docker.io/atamanenko/vitasdk-softfp:latest bash -c "cmake .. && make -j\$(nproc)"
```

The resulting files will be in `build/`:
- `eboot.bin`
- `zenonia1.vpk`

---

## 📜 Credits

- [TheFloW](https://github.com/TheOfficialFlow) for the original `.so` loader.
- [Rinnegatamante](https://github.com/Rinnegatamante/) for VitaGL and loader contributions.
- [gl33ntwine (v-atamanenko)](https://github.com/v-atamanenko/) for FalsoNDK and FalsoJNI.
- [Rocroverss](https://github.com/Rocroverss) for LiveArea assets.
- Ill-Hovercraft8548 for the Zenonia 1 Remaster.
- [withlogic](https://github.com/withlogic) for the Vita port project.
