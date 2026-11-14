#pragma once
// Only rank skills which remain equipped on their command slot.
// SokuLib CharacterManager::skillMap lives at +0x6c4. Its 1-byte Skill
// contains (level:7, notUsed:1); v2::Player::skilledSkillLevel shares that
// storage. When a replacement skill is equipped, the previous variant must
// not be offered as a currently executable command.
#include "ActionIdentity.hpp"
#include <algorithm>
#include <string>

namespace whm {
constexpr int skillSlots(int character) noexcept { return character == 4 ? 5 : 4; }
constexpr int skillIndex(int character, int card) noexcept {
    const int i = card - 100;
    return character >= 0 && character < 20 &&
        i >= 0 && i < skillSlots(character) * 3 ? i : -1;
}
// SokuLib::Skill::notUsed is the high bit in the 1.10a +0x6C4 byte.
// The byte is checked together with the learned/effective levels; do not
// infer a selected skill solely from a Card ID or the deck.
inline int activeSkillLevel(int character, int card, bool notUsed,
                            int learned, int effective) noexcept {
    if (skillIndex(character,card)<0 || notUsed ||
        learned<0 || effective<0 || learned>4 || effective>4)
        return -1;
    return effective;
}
// Resolve a *specific* air/ground B/C command to its action ID so that
// level-specific, manually VERIFIED MoveProfiles can be used if available.
// Stage 0/1 are ground B/C; 2/3 are j.B/C, per SokuLib ActionIdentity.
inline int skillActionForNotation(int character,int card,const std::string &notation) {
    const int idx=skillIndex(character,card);
    if(idx<0 || notation.empty())return -1;
    bool air=false;
    std::string command=notation;
    if(command.size()>2 && (command[0]=='j'||command[0]=='J') && command[1]=='.') {
        air=true;command.erase(0,2);
    } else if(command.size()>1 && command[0]=='J' && command[1]>='0' && command[1]<='9') {
        air=true;command.erase(0,1);
    }
    const char button=command.back();
    if(button!='B' && button!='C')return -1;
    const int slots=skillSlots(character),slot=idx%slots,variant=idx/slots;
    const int action=500+slot*20+variant*5+(air?2:0)+(button=='C'?1:0);
    return skillCardForAction(character,action)==card?action:-1;
}
} // namespace whm
