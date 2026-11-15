#pragma once
#include "HitAttribution.hpp"
#include "FirstHit.hpp"
#include <SokuLib.hpp>
#include <cstring>
#include <cstddef>

namespace whm {
inline ActorContact actorContact(const SokuLib::v2::Player &p) {
    return {p.frameState.actionId,p.frameState.sequenceId,p.frameState.currentFrame,
        p.collisionType,static_cast<unsigned char>(p.collisionLimit),p.hitStop,
        p.boxData.hitBoxCount>0};
}
struct ProjectileSnapshotDiag {
    // 0=ok, 1=null list, 2=unexpected vtable getter, 3=invalid list.
    // A zero projectile count is meaningful only with status=0.
    int status=0;
    std::uintptr_t getter=0;
    unsigned listCount=0;
    unsigned accepted=0;
};
inline std::vector<ProjectileContact> projectileContacts(
    const SokuLib::v2::Player &p,ProjectileSnapshotDiag *diag=nullptr) {
    if(diag)*diag={};
    static_assert(offsetof(SokuLib::v2::Player,objectList)==0x6f8);
    static_assert(offsetof(SokuLib::v2::GameObject,handleId)==0x350);
    using OriginalList=SokuLib::v2::GameObjectList<SokuLib::v2::GameObjectMeirin>;
    static_assert(offsetof(OriginalList,objectList)==0x58);
    std::vector<ProjectileContact> out;
    if(!p.objectList){if(diag)diag->status=1;return out;}
    // Vanilla getList is `lea eax,[ecx+58h]; ret`. Verify the getter before
    // copying that list header. No virtual game function is called here.
    const auto vtable=*reinterpret_cast<const std::uintptr_t *const*>(p.objectList);
    if(diag)diag->getter=vtable?vtable[9]:0;
    if(!vtable || vtable[9]!=0x4d2600) {
        if(diag)diag->status=2;
        return out;
    }
    const auto &list=*reinterpret_cast<const SokuLib::List<SokuLib::v2::GameObject*>*>(
        reinterpret_cast<const char*>(p.objectList)+0x58);
    if(diag)diag->listCount=static_cast<unsigned>(list.m_size);
    if(!list.m_head||list.m_size>2048) {
        if(diag)diag->status=3;
        return out;
    }
    out.reserve(list.m_size);
    auto node=list.m_head->next;
    for(unsigned i=0;i<list.m_size&&node&&node!=list.m_head;++i,node=node->next) {
        const auto *o=node->val;
        if(!o||o->gameData.owner!=&p)continue;
        out.push_back({reinterpret_cast<std::uintptr_t>(o),
            reinterpret_cast<std::uintptr_t>(o->parentObject),
            static_cast<unsigned>(o->handleId),o->collisionType,
            static_cast<unsigned char>(o->collisionLimit),o->hitStop,
            o->boxData.hitBoxCount>0,true});
    }
    if(diag)diag->accepted=static_cast<unsigned>(out.size());
    return out;
}
inline ComboReading comboReading(const SokuLib::v2::Player &own,
                                const SokuLib::v2::Player &target) {
    const auto bytes=reinterpret_cast<const char*>(&target);
    unsigned short delay{};std::memcpy(&delay,bytes+0x4c0,sizeof(delay));
    // The browser port of 0x463f50 keeps comboRate at PLAYER+0x4B0 (1200),
    // immediately before comboCount at +0x4B4. Read-only; fall back to 1.0
    // when the value is not a sane proration multiplier.
    const auto ownBytes=reinterpret_cast<const char*>(&own);
    float rate=1.0f;std::memcpy(&rate,ownBytes+0x4b0,sizeof(rate));
    if(!(rate>0.0f&&rate<=1.0001f))rate=1.0f;
    return {static_cast<unsigned short>(own.comboCount),
        static_cast<unsigned short>(own.comboDamage),
        canStartCombo(target.frameState.actionId,delay),
        static_cast<double>(rate)};
}
} // namespace whm
