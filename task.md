# Task List

## Phase 1: Fix Core Hooks & Offset Reliability
- [x] Fix CreateMove to pattern-scan instead of hardcoding offset
- [x] Fix double-application of angles in CreateMove (pre + post original)
- [x] Add offset validation checks

## Phase 2: Fix Ragebot & Legitbot
- [x] Remove SendInput mouse simulation from ragebot
- [x] Fix ragebot wiring in cheat_core.cpp
- [x] Fix weapon ID inconsistencies

## Phase 3: Fix No Recoil / RCS
- [x] Fix misc.cpp wrong m_pAimPunchServices offset (0x1490 → use Offsets::Get)
- [x] Unify recoil sign conventions across all files
- [x] Fix NoSpread recoil compensation sign error

## Phase 4: Proper Internal Chams (DX11 DrawIndexed hook)
- [x] Create chams.h with Chams namespace
- [x] Create chams.cpp with DX11 DrawIndexed chams
- [x] Modify dx11_hook.cpp to install DrawIndexed hooks
- [x] Wire chams into cheat_core initialization
- [x] Remove overlay chams from visuals.cpp

## Verification
- [x] Build the DLL successfully
