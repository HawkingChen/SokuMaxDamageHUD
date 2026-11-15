#pragma once
#include <string>
namespace whm {
// SokuLib Action.hpp: each skill slot occupies 20 action IDs; variants
// start at +0/+5/+10. Card IDs group all slots before the next variant.
constexpr int skillCardForAction(int character,int action) {
    if(action<500||action>=600)return -1;
    int relative=action-500,slot=relative/20,variant=(relative%20)/5;
    int slots=character==4?5:4; // Patchouli has five skill slots.
    if(slot>=slots||variant>=3||relative%5>=4)return -1;
    return 100+variant*slots+slot;
}
inline std::string mergeSkillButtons(const std::string &a,const std::string &b) {
    if(a==b)return a;
    // Charged variants keep the bracket: "236[B]" + "236[C]" -> "236[B/C]".
    // Without this, both end in ']' and only the B version would be shown.
    if(a.size()>=3&&b.size()>=3&&a.back()==']'&&b.back()==']') {
        const std::string pa=a.substr(0,a.size()-2),pb=b.substr(0,b.size()-2);
        const char ca=a[a.size()-2],cb=b[b.size()-2];
        if(pa==pb&&((ca=='B'&&cb=='C')||(ca=='C'&&cb=='B')))
            return pa+"B/C]";
        return a;
    }
    if(a.size()!=b.size()||a.empty()||a.substr(0,a.size()-1)!=b.substr(0,b.size()-1))return a;
    if((a.back()=='B'&&b.back()=='C')||(a.back()=='C'&&b.back()=='B'))return a.substr(0,a.size()-1)+"B/C";
    return a;
}
// 1.10a SokuLib Action.hpp:
//   600..619: ACTION_USING_SC_ID_200..219
//   650..669: ACTION_SC_ID_200..219_ALT_EFFECT
// The second set is frequently a scripted follow-up *of the same spell*,
// not another unrelated action. E.g. Meiling SC209: 609 -> 659.
constexpr bool isSpellActivationAction(int action) noexcept {
    return action>=600&&action<620;
}
constexpr bool isSpellFollowupAction(int action) noexcept {
    return action>=650&&action<670;
}
constexpr int spellCardForAction(int action) noexcept {
    if(isSpellActivationAction(action))return action-400;
    if(isSpellFollowupAction(action))return action-450;
    return -1;
}
constexpr int canonicalAttackAction(int action) noexcept {
    return isSpellFollowupAction(action)?action-50:action;
}
// Vanilla Suwako routes: 66A enters its airborne strike automatically;
// airborne Stone Frog God lands into the corresponding ground execution.
constexpr bool isCharacterAttackContinuation(int character,int from,int to) noexcept {
    return character==19&&((from==305&&to==306)||(from==552&&to==550)||(from==553&&to==551));
}
constexpr int cardForAction(int character,int action) {
    const int spell=spellCardForAction(action);
    return spell>=0?spell:skillCardForAction(character,action);
}
}
