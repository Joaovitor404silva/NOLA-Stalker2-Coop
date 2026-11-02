# L1 cover / crouch evidence (2026-10-17 17:04-17:18, all-C++ build 8DE41D0F, pair 1)

This is evidence only. A1 owns the fix.

Setup: GeneralNPC_Bandit_Stormtrooper NPCs were spawned on host A, 15-25 m from both players. Each NPC's active montage was read with S2Dev on the host NPC (port 27061) and on its client copy (port 27060), via Mesh -> GetAnimInstance -> GetCurrentActiveMontage. NPC pairs are matched by position.

## 1. Host NPCs (host side)
The host NPCs do use cover. Between 17:08:02 and 17:08:33 three host bandits played:
- MG_tp_ar_cov_hi_r_bend_in and MG_tp_ar_cov_hi_r_bend_out (high cover, peek right);
- MG_tp_ar_cover_high;
- MG_tp_ar_cov_lo_r_bend_up_in and MG_tp_ar_cov_lo_r_bend_up_out (low cover, pop up);
- MG_tp_ar_cover_low;
- MG_tp_ak74_cov_lo_reload;
- MG_tp_ar_combat_hi_idle, MG_tp_ar_combat_ak74_shoot and MG_tp_ar_combat_ak74_reload.

bIsCrouched (Character) was always false on the host NPCs. S2 NPCs crouch through the cover montages and the AnimBP, not through Character crouch. A fix that relays bIsCrouched would have no effect.

## 2. Client copies (natnpc)
The cover montages DO reach the copies. These are the same NPCs, sampled about 4 s apart:

| time | host NPC (host id) | host montage | copy (client id) | copy montage |
|---|---|---|---|---|
| 17:16:23 / :28 | 2147479315 @416832,530576 | combat_hi_idle | 2147479920 (same position) | cov_hi_r_bend_in |
| 17:16:32 / :37 | 2147479315 | cover_high | 2147479920 | cover_high |
| 17:16:41 / :45 | 2147479315 | cov_hi_r_bend_in | 2147479920 | cov_hi_r_bend_out |
| 17:16:49 / :54 | 2147459297 @415772,526977 | cover_low | 2147467280 | combat_ak74_shoot |
| 17:16:58 / 17:17:02 | 2147459297 | cov_lo_reload | 2147467280 | cov_lo_r_bend_up_in |
| 17:17:14 / :18 | 2147459277 @418369,527097 | cover_high | 2147467258 | none |
| 17:17:14 / :18 | 2147459297 | cover_low | 2147467280 | none |
| 17:17:22 / :27 | 2147459277 | cover_high | 2147467258 | none |

Same-moment pair: host 2147459297 vs copy 2147467280, read within 1 s of each other:

| time | host | copy |
|---|---|---|
| 17:17:48 | none | combat_ak74_shoot |
| 17:17:53 | cover_low | cover_low |
| 17:17:58 | combat_hi_idle | none |
| 17:18:03 | cover_low | cover_low |

The copy side's 10 s natnpc line (17:18) reads: montagens tocadas=68 falhou=0 sem_asset=0 paradas=40 tiros_anim=102; copia_inCombat=133/133.

## Reading
- Host: all the cover poses play (cover high and low, bend in/out, peek, reload in cover, cover fire).
- Copies: the same montage names play with 0 failures. Often, though, the copy shows "none" while the host is still holding cover_high or cover_low (17:17:14 and 17:17:22, two NPCs). The copy montage gets stopped early ("paradas=40" of 68 played) or never restarted. A held cover pose then drops to the standing combat idle on the client. That fits "crouch/cover broken": the copy stands up while the host NPC is still in cover.
- In each pair, the host sample comes about 4 s before the copy sample, except the same-moment table. A strict proof needs one read of both sides in the same frame. Next probe for A1: log the copy-side Montage_Stop or blend-out reason for MG_*cover_* montages.

## Screenshots
D:\modding\cpp\S2CoopPort\tests\L1_cover\cp_0..3_AB.png show A on the left and B on the right, both aimed at the 2147459297 / 2147467280 pair. It was night after the time sync, and the NPC sat about 16 m away behind cover, so the shots show little. The montage reads above are the real evidence.

## Unrelated, seen in the same session
The client crashed at 17:08:43. The access violation is inside the game (exe+0x1ad037, FUN_1401acfe8+0x4f) with ALifePopulationManager::vf_102 in the stack and no mod frame. It is the same stack as D:\modding\offline\b192door\dump2339.
