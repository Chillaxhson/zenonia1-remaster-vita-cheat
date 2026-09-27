/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "utils/logger.h"

extern so_module so_mod;
static so_hook CMvAppC1Ev_hook;
static so_hook CMvPlayerC2Ei_hook;
static so_hook CMvGameState_hook;

static so_hook _ZN20GVUIPlayerController19InitialPlayerPadSetEv_hook;
static so_hook _ZN11CMvGraphics10SetQualityE16EnumQualityLevel_hook;

static so_hook CGsStateManagerI12CMvGameStateERunPS0_h_hook;
static so_hook _ZN9CMvPlayer13DrawCharacterEii_hook;

static so_hook get_real_path_hook;

void *g_CMvApp_instance = NULL;
void *g_CMvPlayer_instance = NULL;
void *g_CMvGameState_instance = NULL;

extern int settings_graphicsqualty;

typedef struct CGsStateManagerNode {
    void *ptr;
    int32_t adj;
    struct CGsStateManagerNode *next;
} CGsStateManagerNode;

// the game corrupts its StateManager during play and causes a crash
// this rebuilds the StateManager to a working state.
// For this portion of the code AI Assistance was used.
static int CGsStateManagerNode_Invoke(CGsStateManagerNode *node, void *pObj) {
    void *ptr = node->ptr;
    int32_t adj = node->adj;
    void *adjThis = (void *)((char *)pObj + (adj >> 1));
    int (*func)(void *);

    if (adj & 1) {
        void **vtable = *(void ***)adjThis;
        func = *(int (**)(void *))((char *)vtable + (uintptr_t)ptr);
    } else {
        func = (int (*)(void *))ptr;
    }

    return func(adjThis);
}

// the game corrupts its StateManager during play and causes a crash
// this rebuilds the StateManager to a working state.
// For this portion of the code AI Assistance was used.
int CGsStateManagerRun_patched(void *this, void *pObj, int which) {
    CGsStateManagerNode *head = *(CGsStateManagerNode **)this;
    if (!head) { return -1; }

    if ((which & 0xFF) == 0) {
        return CGsStateManagerNode_Invoke(head, pObj);
    }

    int result = 0;
    CGsStateManagerNode *node = head;
    while (node) {
        CGsStateManagerNode *next = node->next; 
        result = CGsStateManagerNode_Invoke(node, pObj);
        node = next;
    }
    return result;
}

void CMvAppC1Ev_patched(void *this) {
    g_CMvApp_instance = this;
    SO_CONTINUE(void *, CMvAppC1Ev_hook, this);
}

void CMvPlayerC2Ei_patched(void *this, int param) {
    g_CMvPlayer_instance = this;
    l_debug("CMvPlayer::CMvPlayer: Hooked into function");
    SO_CONTINUE(void *, CMvPlayerC2Ei_hook, this, param);
}

void _ZN12CMvGameStateC2Ev(void *this) {
    g_CMvGameState_instance = this;
    l_debug("CMvGameState::CMvGameState: Hooked into function");
    SO_CONTINUE(void *, CMvGameState_hook, this);
}

int get_real_path_patched(const char *in_path, char *out_path) {
    if (!in_path || !out_path) return 0;

    l_debug("get_real_path: in_path='%s'", in_path);

    if (strncmp(in_path, "ux0:", 4) == 0 || strncmp(in_path, "app0:", 5) == 0) {
        snprintf(out_path, 256, "%s", in_path);
        return 1;
    }

    const char *rel = in_path;
    while (*rel == '/') {
        rel++;
    }

    snprintf(out_path, 256, "%s%s", DATA_PATH, rel);
    l_debug("get_real_path: out_path='%s'", out_path);
    return 1;
}

void _ZN20GVUIPlayerController19InitialPlayerPadSetEv_patched(void *this) {        // how annoying. THIS is what gets rid of the touch display.
    l_debug("GVUIPlayerController::InitialPlayerPadSet: Hooked into function");
}

void _ZN11CMvGraphics10SetQualityE16EnumQualityLevel_patched(void *this, int param) {
    l_debug("CMvGraphics::SetQuality Hooked into function %d vs %d", param , settings_graphicsqualty);

    // set the upper and lower bounds, just in case.
    if(settings_graphicsqualty < 0) {
        settings_graphicsqualty = 0;
    }

    if(settings_graphicsqualty > 2) {
        settings_graphicsqualty = 2;
    }

    SO_CONTINUE(void *, _ZN11CMvGraphics10SetQualityE16EnumQualityLevel_hook, this, settings_graphicsqualty);
}

void _ZN9CMvPlayer13DrawCharacterEii_patched(void *this, int param1, int param2) {
    return 0;
}

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

// Suppress "Savefile corrupted, please create a new character" popup
static so_hook CreateInvalidDataPopup_hook;
void CreateInvalidDataPopup_patched(void *this) {
    l_warn("CMvSystemMenu::CreateInvalidDataPopup called - suppressed!");
}

void so_patch(void) {
    CMvAppC1Ev_hook = hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN6CMvAppC1Ev"),
        (uintptr_t)&CMvAppC1Ev_patched);

    CMvPlayerC2Ei_hook = hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN9CMvPlayerC2Ei"),
        (uintptr_t)&CMvPlayerC2Ei_patched);

    CMvGameState_hook = hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN12CMvGameStateC2Ev"),
        (uintptr_t)&_ZN12CMvGameStateC2Ev);

    get_real_path_hook = hook_addr((uintptr_t)so_symbol(&so_mod, "_Z13get_real_pathPcS_"),
        (uintptr_t)&get_real_path_patched);

    _ZN20GVUIPlayerController19InitialPlayerPadSetEv_hook = hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN20GVUIPlayerController19InitialPlayerPadSetEv"),
        (uintptr_t)&_ZN20GVUIPlayerController19InitialPlayerPadSetEv_patched); 

    _ZN11CMvGraphics10SetQualityE16EnumQualityLevel_hook = hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN11CMvGraphics10SetQualityE16EnumQualityLevel"),
        (uintptr_t)&_ZN11CMvGraphics10SetQualityE16EnumQualityLevel_patched); 

    CGsStateManagerI12CMvGameStateERunPS0_h_hook = hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN15CGsStateManagerI12CMvGameStateE3RunEPS0_h"),
        (uintptr_t)&CGsStateManagerRun_patched);

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

    // Bypass "!cFF2F2FSavefile corrupted, please create a new character." in CMvGameState::Initialize
    // Offset 0x274 from _ZN12CMvGameState10InitializeEv (0x000de620 -> 0x000de894)
    // Original: 0x2800 (cmp r0, #0), 0xd120 (bne.n 0xde8da)
    // Patched:  0xe021 (b.n 0xde8da), 0xbf00 (nop)
    uintptr_t init_func = (uintptr_t)so_symbol(&so_mod, "_ZN12CMvGameState10InitializeEv");
    if (init_func) {
        uintptr_t patch_init_addr = (init_func & ~1) + 0x274;
        uint16_t patch_init_code[2] = { 0xe021, 0xbf00 }; // b.n +0x42 (to de8da); nop
        kuKernelCpuUnrestrictedMemcpy((void *)patch_init_addr, patch_init_code, sizeof(patch_init_code));
        l_info("Patched CMvGameState::Initialize corruption popup bypass at 0x%08x", (unsigned int)patch_init_addr);
    }
}

