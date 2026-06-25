// =================================================================
// misc.h - Misc features header
// =================================================================

#pragma once

#include <windows.h>
#include <string>
#include <vector>

class Misc {
public:
    Misc();

    void Update();

    // Settings
    bool m_knifeBot;
    bool m_voteReveal;
    bool m_skinChanger;
    bool m_nameSpammer;
    bool m_clanTagSpammer;
    bool m_autoAccept;
    bool m_rankRevealer;
    bool m_damageReport;
    bool m_hudRemoval;
    bool m_skyboxRemoval;
    bool m_shadowRemoval;
    bool m_scopeRemoval;
    bool m_fogRemoval;
    bool m_smokeRemoval;
    bool m_flashReduction;
    float m_flashAmount;
    bool m_chatSpamBlock;
    bool m_messageFilter;
    bool m_autoPistol;
    bool m_autoReload;

private:
    void DoKnifeBot();
    void DoVoteReveal();
    void DoSkinChanger();
    void DoNameSpammer();
    void DoClanTagSpammer();
    void DoAutoAccept();
    void DoRankRevealer();
    void DoDamageReport();
    void DoHUDRemoval();
    void DoSkyboxRemoval();
    void DoShadowRemoval();
    void DoScopeRemoval();
    void DoFogRemoval();
    void DoSmokeRemoval();
    void DoFlashReduction();
    void DoChatSpamBlock();
    void DoMessageFilter();
    void DoAutoPistol();
    void DoAutoReload();

    int m_nameSpammerIndex;
    int m_tagSpammerIndex;
    DWORD m_lastNameChange;
    DWORD m_lastTagChange;
};