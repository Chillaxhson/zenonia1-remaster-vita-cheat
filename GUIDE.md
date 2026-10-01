# Zenonia 1 PS Vita: Cheat Modification & Development Guide

This guide details the architecture, reverse-engineered engine symbols, memory offsets, and implementation steps for building the hotkey cheat system for the PS Vita port of **Zenonia 1** (`zenonia1-vita` / `so_loader`).

---

## 📑 Table of Contents
1. [One-Shot Implementation Prompt](#one-shot-implementation-prompt)
2. [Critical Technical Pitfall: The Float ABI Trap](#critical-technical-pitfall-the-float-abi-trap)
3. [Reverse-Engineered Symbols & Offsets](#reverse-engineered-symbols--offsets)
4. [Hotkey Controls Scheme](#hotkey-controls-scheme)
5. [Code Modifications Reference](#code-modifications-reference)
   - [CMakeLists.txt](#1-cmakeliststxt)
   - [source/patch.c](#2-sourcepatchc-god-mode--50x-exp-multiplier-hooks)
   - [source/main.c](#3-sourcemainc-symbols--hotkey-handler)
6. [Save System & Freezing Fix: Architecture & Root Cause](#-save-system--freezing-fix-architecture--root-cause)
7. [Compilation & Packaging](#-compilation--packaging)

---

## 🚀 One-Shot Implementation Prompt

Copy and paste the prompt below into any fresh AI agent session or use it as a standalone specification on a pristine clone of the repository:

```markdown
You are modifying the PS Vita port of Zenonia 1 (so_loader loading Android libzenonia.so).
The goal is to implement a clean, headless hotkey cheat system using L1 as a modifier button without introducing any visual or input bugs.

### ⚠️ CRITICAL COMPILATION & ABI REQUIREMENT
1. DO NOT change the float ABI to `-mfloat-abi=hard`. The Android library `libzenonia.so` uses `softfp`.
   Compiling with `hard` float ABI corrupts floating-point arguments passed to VitaGL/Android routines, causing a massive screen zoom, flipped projection matrix, and non-responsive touch controls.
2. Maintain `-mfloat-abi=softfp` in `CMakeLists.txt`.
3. ALWAYS compile using the specific softfp Docker image: `docker.io/atamanenko/vitasdk-softfp:latest`. DO NOT use the standard/latest `vitasdk/vitasdk:latest` image.

---

### CHEAT SPECIFICATIONS & CONTROLS
The L1 button (`AKEYCODE_BUTTON_L1`) does not conflict with in-game controls (it maps to 0).
Implement the following hotkey combinations when holding **L1**:

- **L1 + SELECT**: Toggle 50x EXP Multiplier ON/OFF
- **L1 + SQUARE**: Full HP & SP Refill (100% HP & SP)
- **L1 + TRIANGLE**: Add 50,000 Gold
- **L1 + CIRCLE**: Add 5 Stat Points
- **L1 + CROSS**: Add 5 Skill Points
- **L1 + START**: Toggle God Mode (Invulnerability) ON/OFF

*Note*: When L1 is held down and any of the shortcut keys are pressed, suppress forwarding the key event to the game engine (`_ZN6CMvApp10EvKeyPressEi`) so that normal actions (like opening the in-game menu, map, or attacking) are not triggered unintentionally.

---

### IMPLEMENTATION DETAILS

#### 1. `source/patch.c` (God Mode Hook & 50x EXP Multiplier)
Export global flags `int cheat_god_mode = 0;` and `int cheat_exp_multiplier = 0;`:

- Mangled Symbols:
  - `_ZN9CMvPlayer9OnDamagedEiP12CMvCharacterb15EnumElementTypeb`
  - `_ZN9CMvPlayer6IncExpEjb`
- Hook definitions:
  ```c
  int cheat_god_mode = 0;
  static so_hook CMvPlayer_OnDamaged_hook;

  void CMvPlayer_OnDamaged_patched(void *this_ptr, int damage, void* attacker, bool b1, int elem, bool b2) {
      if (cheat_god_mode) {
          return; // Suppress all damage
      }
      SO_CONTINUE(int, CMvPlayer_OnDamaged_hook, this_ptr, damage, attacker, b1, elem, b2);
  }

  int cheat_exp_multiplier = 0;
  static so_hook CMvPlayer_IncExp_hook;

  int CMvPlayer_IncExp_patched(void *this_ptr, unsigned int exp, bool b) {
      if (cheat_exp_multiplier) {
          if (exp > (4294967295U / 50)) {
              exp = 4294967295U;
          } else {
              exp *= 50;
          }
      }
      return SO_CONTINUE(int, CMvPlayer_IncExp_hook, this_ptr, exp, b);
  }
  ```
- In `so_patch(void)`:
  ```c
  CMvPlayer_OnDamaged_hook = hook_addr(
      (uintptr_t)so_symbol(&so_mod, "_ZN9CMvPlayer9OnDamagedEiP12CMvCharacterb15EnumElementTypeb"),
      (uintptr_t)&CMvPlayer_OnDamaged_patched
  );

  CMvPlayer_IncExp_hook = hook_addr(
      (uintptr_t)so_symbol(&so_mod, "_ZN9CMvPlayer6IncExpEjb"),
      (uintptr_t)&CMvPlayer_IncExp_patched
  );

  // Suppress "Savefile corrupted" popup
  CreateInvalidDataPopup_hook = hook_addr(
      (uintptr_t)so_symbol(&so_mod, "_ZN13CMvSystemMenu22CreateInvalidDataPopupEv"),
      (uintptr_t)&CreateInvalidDataPopup_patched
  );

  // Bypass DRM / save checksum & level check in CMvGameState::LoadGameData
  // Offset 0xf2 from _ZN12CMvGameState12LoadGameDataEi (0x000deb5c -> 0x000dec4e)
  // Original: 0x4288 (cmp r0, r1), 0xd110 (bne.n 0xdec74 -> calls CreateInvalidDataPopup)
  // Patched:  0xe01a (b.n 0xdec86 -> returns 1), 0xbf00 (nop)
  uintptr_t load_game_data = (uintptr_t)so_symbol(&so_mod, "_ZN12CMvGameState12LoadGameDataEi");
  if (load_game_data) {
      uintptr_t patch_addr = (load_game_data & ~1) + 0xf2;
      uint16_t patch_code[2] = { 0xe01a, 0xbf00 }; // b.n +0x34; nop
      kuKernelCpuUnrestrictedMemcpy((void *)patch_addr, patch_code, sizeof(patch_code));
      l_info("Patched CMvGameState::LoadGameData anti-tamper check at 0x%08x", (unsigned int)patch_addr);
  }
  ```

#### 2. `source/main.c` (Symbol Resolution & Input Handler)
1. Declare external pointers and resolved functions:
   ```c
   extern void *g_CMvPlayer_instance;
   extern void *g_CMvApp_instance;
   extern int cheat_god_mode;
   extern int cheat_exp_multiplier;

   void (* _ZN12CMvCharacter6FullHPEv)(void *this);
   void (* _ZN12CMvCharacter6FullSPEbb)(void *this, bool b1, bool b2);
   void (* _ZN12CMvCharacter5SetSPEib)(void *this, int sp, bool b);
   void* (*CMvItemMgr_GetInstPtr)(void);
   void (*CItemSaveData_IncMoney)(void *this, int money);
   ```

2. Resolve symbols in `main()` using `so_symbol`:
   ```c
   _ZN12CMvCharacter6FullHPEv = (void *)so_symbol(&so_mod, "_ZN12CMvCharacter6FullHPEv");
   _ZN12CMvCharacter6FullSPEbb = (void *)so_symbol(&so_mod, "_ZN12CMvCharacter6FullSPEbb");
   _ZN12CMvCharacter5SetSPEib = (void *)so_symbol(&so_mod, "_ZN12CMvCharacter5SetSPEib");
   CMvItemMgr_GetInstPtr = (void *)so_symbol(&so_mod, "_ZN12CGsSingletonI10CMvItemMgrE10GetInstPtrEv");
   CItemSaveData_IncMoney = (void *)so_symbol(&so_mod, "_ZN13CItemSaveData8IncMoneyEi");
   ```

3. Update `controls_handler_key(int32_t keycode, ControlsAction action)`:
   - Track `pressL1` on `AKEYCODE_BUTTON_L1`.
   - When `action == CONTROLS_ACTION_DOWN` and `pressL1` is active:
     - `AKEYCODE_BUTTON_SELECT` (Toggle 50x EXP Multiplier):
       ```c
       cheat_exp_multiplier = !cheat_exp_multiplier;
       return;
       ```
     - `AKEYCODE_BUTTON_X` (Square - Full HP & SP Refill):
       ```c
       if (g_CMvPlayer_instance) {
           if (_ZN12CMvCharacter6FullHPEv) _ZN12CMvCharacter6FullHPEv(g_CMvPlayer_instance);
           if (_ZN12CMvCharacter6FullSPEbb) {
               _ZN12CMvCharacter6FullSPEbb(g_CMvPlayer_instance, true, true);
           } else if (_ZN12CMvCharacter5SetSPEib) {
               _ZN12CMvCharacter5SetSPEib(g_CMvPlayer_instance, 9999, 1);
           }
       }
       return;
       ```
     - `AKEYCODE_BUTTON_Y` (Triangle - Gold):
       ```c
       if (CMvItemMgr_GetInstPtr && CItemSaveData_IncMoney) {
           void* itemMgr = CMvItemMgr_GetInstPtr();
           if (itemMgr) {
               void* saveData = (void*)((uintptr_t)itemMgr + 4);
               CItemSaveData_IncMoney(saveData, 50000);
           }
       }
       return;
       ```
     - `AKEYCODE_BUTTON_B` (Circle - Stat Points):
       ```c
       if (g_CMvPlayer_instance) {
           uint16_t* statPts = (uint16_t*)((uintptr_t)g_CMvPlayer_instance + 0x58c);
           *statPts += 5;
       }
       return;
       ```
     - `AKEYCODE_BUTTON_A` (Cross - Skill Points):
       ```c
       if (g_CMvPlayer_instance) {
           uint16_t* skillPts = (uint16_t*)((uintptr_t)g_CMvPlayer_instance + 0x58e);
           *skillPts += 5;
       }
       return;
       ```
     - `AKEYCODE_BUTTON_START` (God Mode Toggle):
       ```c
       cheat_god_mode = !cheat_god_mode;
       return;
       ```
   - When not holding `pressL1`, execute normal `_ZN6CMvApp10EvKeyPressEi(g_CMvApp_instance, avk);`.

---

### BUILD COMMAND
Clean the build directory and compile using podman/docker with the softfp toolchain:
```bash
podman run --rm -v "$(pwd):/src:z" -w /src/build docker.io/atamanenko/vitasdk-softfp:latest bash -c "make clean && make -j\$(nproc)"
```
Verify that `eboot.bin` and `zenonia1.vpk` build with exit code 0.
```

---

## ⚠️ Critical Technical Pitfall: The Float ABI Trap

### The Problem
The Android game library (`libzenonia.so`) was compiled for older ARMv7 processors using the **`softfp`** floating point ABI.
- In **`softfp`**, floating point parameters are passed via standard integer registers (`r0`–`r3` and stack).
- In modern VitaSDK releases (`vitasdk/vitasdk:latest`), toolchain defaults have migrated to **`hard`** float ABI (VFP registers `s0`–`s15`).

### The Symptom
If `so_loader` is compiled with `hard` float ABI:
1. OpenGL functions (like `glOrthof`, `glClearColor`, viewport transformations) receive corrupted floats.
2. The game renders with a **massive zoom**, **flipped/inverted projection**, and **unresponsive touch input**.
3. Re-exporting matrices or tuning viewport resolution in code will **not** fix this issue because the underlying function parameters are fundamentally misaligned at the register level.

### The Solution
- Always build with: `docker.io/atamanenko/vitasdk-softfp:latest`
- Keep `CMakeLists.txt` configured with: `-mfloat-abi=softfp`

---

## 🔍 Reverse-Engineered Symbols & Offsets

| Symbol / Offset | Description | Signature |
|---|---|---|
| `_ZN12CMvCharacter6FullHPEv` | Restores character HP to maximum | `void FullHP(void *this)` |
| `_ZN12CMvCharacter6FullSPEbb` | Restores character SP to maximum | `void FullSP(void *this, bool b1, bool b2)` |
| `_ZN12CMvCharacter5SetSPEib` | Sets character SP value | `void SetSP(void *this, int sp, bool b)` |
| `_ZN12CGsSingletonI10CMvItemMgrE10GetInstPtrEv` | Retrieves Item Manager singleton | `void* GetInstPtr(void)` |
| `_ZN13CItemSaveData8IncMoneyEi` | Adds money/gold to inventory | `void IncMoney(void *this, int money)` |
| `_ZN9CMvPlayer6IncExpEjb` | Adds player EXP | `void IncExp(void *this, unsigned int exp, bool b)` |
| `_ZN9CMvPlayer9OnDamagedEiP12CMvCharacterb15EnumElementTypeb` | Player damage handler (hooked for God Mode) | `int OnDamaged(void *this, int dmg, void *atk, bool b1, int elem, bool b2)` |
| `g_CMvPlayer_instance + 0x58c` | Player Stat Points (Unallocated) | `uint16_t` |
| `g_CMvPlayer_instance + 0x58e` | Player Skill Points (Unallocated) | `uint16_t` |
| `itemMgr + 0x4` | Offset to `CItemSaveData` inside `CMvItemMgr` | `void*` |

---

## 🎮 Hotkey Controls Scheme

The `L1` button (`AKEYCODE_BUTTON_L1`) returns `0` in `vita_to_control` and is unused in normal gameplay. It serves as the primary modifier:

| Button Combination | In-Game Action | Effect |
|---|---|---|
| <kbd>L1</kbd> + <kbd>SELECT</kbd> | Toggle 50x EXP | Toggles 50x Experience Points multiplier ON / OFF |
| <kbd>L1</kbd> + <kbd>SQUARE</kbd> | Refill Vitals | Restores HP and SP to 100% |
| <kbd>L1</kbd> + <kbd>TRIANGLE</kbd> | Add Gold | Adds +50,000 Gold directly to inventory |
| <kbd>L1</kbd> + <kbd>CIRCLE</kbd> | Add Stat Points | Adds +5 unassigned Character Stat Points |
| <kbd>L1</kbd> + <kbd>CROSS</kbd> | Add Skill Points | Adds +5 unassigned Skill Points |
| <kbd>L1</kbd> + <kbd>START</kbd> | Toggle God Mode | Toggles complete damage immunity ON / OFF |

*All combo presses consume the input event so normal gameplay actions (such as menu opening or attacking) do not trigger.*

---

## 🛠️ Code Modifications Reference

### 1. `CMakeLists.txt`
Ensure the compiler flags maintain `softfp`:
```cmake
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi")
```

### 2. `source/patch.c` (God Mode & 50x EXP Multiplier Hooks)
```c
#include <stdbool.h>

// Cheat: God Mode
int cheat_god_mode = 0;
static so_hook CMvPlayer_OnDamaged_hook;

void CMvPlayer_OnDamaged_patched(void *this_ptr, int damage, void* attacker, bool b1, int elem, bool b2) {
    if (cheat_god_mode) {
        return; // Suppress damage
    }
    SO_CONTINUE(int, CMvPlayer_OnDamaged_hook, this_ptr, damage, attacker, b1, elem, b2);
}

// Cheat: 50x EXP Multiplier
int cheat_exp_multiplier = 0;
static so_hook CMvPlayer_IncExp_hook;

int CMvPlayer_IncExp_patched(void *this_ptr, unsigned int exp, bool b) {
    if (cheat_exp_multiplier) {
        if (exp > (4294967295U / 50)) {
            exp = 4294967295U;
        } else {
            exp *= 50;
        }
    }
    return SO_CONTINUE(int, CMvPlayer_IncExp_hook, this_ptr, exp, b);
}

// Path resolution fix for saves and assets
static so_hook get_real_path_hook;
int get_real_path_patched(const char *in_path, char *out_path) {
    if (!in_path || !out_path) return 0;
    if (strncmp(in_path, "ux0:", 4) == 0 || strncmp(in_path, "app0:", 5) == 0) {
        snprintf(out_path, 256, "%s", in_path);
        return 1;
    }
    const char *rel = in_path;
    while (*rel == '/') {
        rel++;
    }
    snprintf(out_path, 256, "%s%s", DATA_PATH, rel);
    return 1;
}

void so_patch(void) {
    // ... existing patches ...

    get_real_path_hook = hook_addr(
        (uintptr_t)so_symbol(&so_mod, "_Z13get_real_pathPcS_"),
        (uintptr_t)&get_real_path_patched
    );

    CMvPlayer_OnDamaged_hook = hook_addr(
        (uintptr_t)so_symbol(&so_mod, "_ZN9CMvPlayer9OnDamagedEiP12CMvCharacterb15EnumElementTypeb"),
        (uintptr_t)&CMvPlayer_OnDamaged_patched
    );

    CMvPlayer_IncExp_hook = hook_addr(
        (uintptr_t)so_symbol(&so_mod, "_ZN9CMvPlayer6IncExpEjb"),
        (uintptr_t)&CMvPlayer_IncExp_patched
    );
}
```

### 3. `source/main.c` (Symbols & Hotkey Handler)
```c
extern void *g_CMvPlayer_instance;
extern void *g_CMvApp_instance;
extern int cheat_god_mode;
extern int cheat_exp_multiplier;

void (* _ZN12CMvCharacter6FullHPEv)(void *this);
void (* _ZN12CMvCharacter6FullSPEbb)(void *this, bool b1, bool b2);
void (* _ZN12CMvCharacter5SetSPEib)(void *this, int sp, bool b);
void* (*CMvItemMgr_GetInstPtr)(void);
void (*CItemSaveData_IncMoney)(void *this, int money);

int pressL1 = 0;

int main() {
    // ... existing initialization ...
    _ZN12CMvCharacter6FullHPEv = (void *)so_symbol(&so_mod, "_ZN12CMvCharacter6FullHPEv");
    _ZN12CMvCharacter6FullSPEbb = (void *)so_symbol(&so_mod, "_ZN12CMvCharacter6FullSPEbb");
    _ZN12CMvCharacter5SetSPEib = (void *)so_symbol(&so_mod, "_ZN12CMvCharacter5SetSPEib");
    CMvItemMgr_GetInstPtr = (void *)so_symbol(&so_mod, "_ZN12CGsSingletonI10CMvItemMgrE10GetInstPtrEv");
    CItemSaveData_IncMoney = (void *)so_symbol(&so_mod, "_ZN13CItemSaveData8IncMoneyEi");
    // ... main loop ...
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    if (g_CMvApp_instance) {
        int32_t avk = vita_to_control(keycode);

        switch (action) {
            case CONTROLS_ACTION_DOWN:
                if (keycode == AKEYCODE_BUTTON_L1) { 
                    pressL1 = 1; 
                }

                // Hotkey combinations while holding L1
                if (pressL1) {
                    if (keycode == AKEYCODE_BUTTON_SELECT) { // Select -> Toggle 50x EXP Multiplier
                        cheat_exp_multiplier = !cheat_exp_multiplier;
                        return;
                    }
                    if (keycode == AKEYCODE_BUTTON_Y) { // Triangle -> Add 50,000 Gold
                        if (CMvItemMgr_GetInstPtr && CItemSaveData_IncMoney) {
                            void* itemMgr = CMvItemMgr_GetInstPtr();
                            if (itemMgr) {
                                void* saveData = (void*)((uintptr_t)itemMgr + 4);
                                CItemSaveData_IncMoney(saveData, 50000);
                            }
                        }
                        return;
                    }
                    if (keycode == AKEYCODE_BUTTON_X) { // Square -> Refill HP & SP
                        if (g_CMvPlayer_instance) {
                            if (_ZN12CMvCharacter6FullHPEv) _ZN12CMvCharacter6FullHPEv(g_CMvPlayer_instance);
                            if (_ZN12CMvCharacter6FullSPEbb) {
                                _ZN12CMvCharacter6FullSPEbb(g_CMvPlayer_instance, true, true);
                            } else if (_ZN12CMvCharacter5SetSPEib) {
                                _ZN12CMvCharacter5SetSPEib(g_CMvPlayer_instance, 9999, 1);
                            }
                        }
                        return;
                    }
                    if (keycode == AKEYCODE_BUTTON_B) { // Circle -> Add 5 Stat Points
                        if (g_CMvPlayer_instance) {
                            uint16_t* statPts = (uint16_t*)((uintptr_t)g_CMvPlayer_instance + 0x58c);
                            *statPts += 5;
                        }
                        return;
                    }
                    if (keycode == AKEYCODE_BUTTON_A) { // Cross -> Add 5 Skill Points
                        if (g_CMvPlayer_instance) {
                            uint16_t* skillPts = (uint16_t*)((uintptr_t)g_CMvPlayer_instance + 0x58e);
                            *skillPts += 5;
                        }
                        return;
                    }
                    if (keycode == AKEYCODE_BUTTON_START) { // Start -> Toggle God Mode
                        cheat_god_mode = !cheat_god_mode;
                        return;
                    }
                }

                _ZN6CMvApp10EvKeyPressEi(g_CMvApp_instance, avk);
                break;

            case CONTROLS_ACTION_UP:
                if (keycode == AKEYCODE_BUTTON_L1) { 
                    pressL1 = 0; 
                }
                _ZN6CMvApp12EvKeyReleaseEi(g_CMvApp_instance, avk);
                break;
        }
    }
}
```

---

## 💾 Save System & Freezing Fix: Architecture & Root Cause

### The Problem: Save Freeze
When saving the game, the game froze or stalled indefinitely.

### Root Cause Analysis
1. `CMvGameState::SaveCurrentGameData` calls `SaveGameData`, which delegates to `CGsFile::Save` -> `GsFSOpen` -> `MC_fsOpen`.
2. All filesystem operations (`MC_fsOpen`, `MC_fsFileAttribute`, `MC_fsIsExist`) rely on `_Z13get_real_pathPcS_` (`get_real_path(in_path, out_path)`).
3. In the Android binary, `get_real_path` called Java `getAbsolueFilePath()` via JNI. In the original loader implementation, this JNI method returned `NULL`, leaving stack buffers uninitialized and generating corrupted file paths (e.g. `\x7f.../Save0.dat`).
4. When saving failed:
   - `SaveCurrentGameData` sets `SetSaveReserved(true)`.
   - The game drops the target framerate to 4 FPS (`SetFPS(4)`).
   - In `CMvApp::Run()`, the game continuously loops waiting for `IsSaveReserved()` to clear. Because the save failed due to path corruption, it never cleared, causing an infinite retry loop at 4 FPS (perceived as a freeze).

### The Pitfall: Why Hooking `IsExistGameData` Broke Saves ("New Game" Loop)
A previous workaround attempt hooked `IsExistGameData` and `LoadGameData` returning dummy values (0 / -1). Returning 0 caused `CMvGameState::IsExistGameData` to report that no save files existed on disk, forcing the game into "New Game" mode and preventing existing saves from being detected.

### The Correct Fix
1. **Hook `_Z13get_real_pathPcS_` directly in `source/patch.c`**:
   Ensure all relative paths resolve cleanly to `ux0:data/zenonia1/<relative_path>` without relying on JNI.
2. **Implement JNI `getAbsolueFilePath` in `source/java.c`**:
   Return `(jobject)jni->NewStringUTF(jni, "ux0:data/zenonia1")` as defense in depth.
3. **Handle filesystem calls in `source/reimpl/io.c`**:
   Ensure `fopen_soloader`, `stat_soloader`, `access_soloader`, `remove_soloader`, and `unlink_soloader` correctly map `Save*.dat` and `option.sav` without adding redundant slashes or corrupting Vita paths.
4. **Remove artificial hooks**:
   Remove `IsExistGameData` and `LoadGameData` hooks completely. The native game engine logic works reliably once `get_real_path` returns valid filesystem paths.

### Savefile Corruption Fix ("Savefile corrupted, please create a new character")

#### The Problem
After saving successfully, selecting "Continue" prompted:
> *"Savefile corrupted, please create a new character"*

#### Root Cause Analysis
1. **Broken `stat64_bionic` Struct Definition (`source/reimpl/io.h`)**:
   - `libzenonia1.so` (`MC_fsFileAttribute` at `0x00078ca8`) reads `st_size` at offset `48` (`0x30`) and `st_mode` at offset `16` (`0x10`).
   - The loader's original `stat64_bionic` struct used `__attribute__((__packed__))` with vitasdk's 16-bit `nlink_t`/`uid_t`/`gid_t`, placing `st_size` at offset `38` instead of `48`!
   - Consequently, `MC_fsFileAttribute` read `0` for the file size of `Save0.dat`.
   - `CGsEncryptFile::LoadBegin("Save0.dat", 1)` saw `GetFileSize == 0` and returned `0` (failure).
2. **Title Drop & Corruption Popup in `CMvGameState::Initialize()`**:
   - At `0x000de890`, `Initialize()` calls `LoadGameData(slot)`.
   - If `LoadGameData` returns 0, `Initialize()` calls `CMvApp::ChangeState(STATE_TITLE)` and spawns popup `!cFF2F2FSavefile corrupted, please create a new character.` at `0x000de8ce`.
3. **Anti-tamper / DRM Check in `CMvGameState::LoadGameData()`**:
   - Computes CRC checksum of save timestamp bytes and compares against `slotInfo->checksum` in `option.sav`.
   - Compares the loaded player level against `slotInfo->level` stored in `option.sav`.

#### The Fix
1. **Fixed `stat64_bionic` Definition (`source/reimpl/io.h`)**:
   - Replaced packed struct with standard 32-bit ARM Bionic alignment (`st_size` at offset 48, `st_mode` at offset 16).
   - Ensured `stat_soloader` fills POSIX-compliant `st_mode` and real file size via `sceIoGetstat`.
2. **DRM Anti-Tamper Bypass (`source/patch.c`)**:
   - Patched `CMvGameState::LoadGameData` offset `0xf2` (`0x000dec4e`):
     - Replaced `cmp r0, r1; bne.n 0xdec74` with `b.n 0xdec86; nop` (`0xbf00e01a`).
3. **Initialize Corruption Popup Bypass (`source/patch.c`)**:
   - Patched `CMvGameState::Initialize` offset `0x274` (`0x000de894`):
     - Replaced `cmp r0, #0; bne.n 0xde8da` with `b.n 0xde8da; nop` (`0xbf00e021`), completely bypassing the corruption popup and title drop.
4. **Binary I/O Mode & Native Stat**:
   - `fopen_soloader` forces `'b'` mode on open calls (e.g. `"w+"` -> `"w+b"`, `"r"` -> `"rb"`).
   - `access_soloader` uses `file_exists` (`sceIoGetstat`) when `mode == 0`.

---

## 📦 Compilation & Packaging

Run the following build command using **Podman** or **Docker**:

```bash
podman run --rm -v "$(pwd):/src:z" -w /src/build docker.io/atamanenko/vitasdk-softfp:latest bash -c "make clean && make -j\$(nproc)"
```

The resulting binaries will be produced in the `build/` directory:
- `build/eboot.bin` — Direct binary replacement for `ux0:app/ZENONIA01/eboot.bin`
- `build/zenonia1.vpk` — Full installable VPK package
