#pragma once
#include "ActionIdentity.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace whm {
// Values copied at the engine event, before objects can disappear/reuse memory.
struct NativeEvent {
    enum Kind { Hit, Birth, Action, Rebind } kind=Hit;
    unsigned frame=0,logicFrame=0,generation=0,creatorGeneration=0,parentGeneration=0;
    std::uintptr_t object=0,victim=0,owner=0,ally=0,creator=0,parent=0,caller=0;
    int action=0,sequence=0,boxAction=0,ownerAction=0,victimAction=0,ownerCharacter=-1;
    int damage=0,hp=0,ownerHP=0,comboHits=0,comboDamageBefore=0,protectedHP=0;
    int path=0,castAction=-1;
    std::uint64_t cast=0;
};

struct NativeCast {
    int action=-1;
    std::uint64_t id=0;
    bool valid() const { return id && action>=300 && action<690; }
};

// Engine action writes delimit casts. A spell's ALT_EFFECT continuation is
// the same cast; returning to its activation action starts a NEW cast.
class NativeActors {
    struct Actor { int raw=-1; NativeCast cast; } actors_[2];
    std::uint64_t serial_=0;
public:
    void reset() { for(auto &a:actors_)a={}; } // serial never reused
    NativeCast get(unsigned side) const { return side<2?actors_[side].cast:NativeCast{}; }
    NativeCast set(unsigned side,int action,int sequence=-1,int character=-1) {
        if(side>=2)return {};
        auto &a=actors_[side];
        const bool continuation=isSpellFollowupAction(action) && a.cast.valid() &&
            a.cast.action==canonicalAttackAction(action);
        const bool subsequence=sequence>0 && action==a.raw && a.cast.valid();
        const bool characterContinuation=a.cast.valid()&&isCharacterAttackContinuation(character,a.raw,action);
        a.raw=action;
        if(action<300||action>=690)a.cast={};
        else if(!continuation&&!subsequence&&!characterContinuation)a.cast={canonicalAttackAction(action),++serial_};
        return a.cast;
    }
};

struct NativeDamage {
    int damage=0,action=-1;
    std::uint64_t instance=0;
    bool self=false;
};

class NativeAttribution {
    using ObjectKey=std::pair<std::uintptr_t,unsigned>;
    struct Object {
        std::uintptr_t owner=0;
        NativeCast root;
        std::weak_ptr<Object> creator;
    };
    using Node=std::shared_ptr<Object>;
    std::map<ObjectKey,Node> objects_;
    std::vector<Node> nodes_; // keeps previous command versions alive until reset
    std::map<std::pair<std::uint64_t,std::uintptr_t>,std::uint64_t> records_;
    std::uint64_t serial_=0;

    Node node(ObjectKey key) {
        auto &result=objects_[key];
        if(!result){result=std::make_shared<Object>();nodes_.push_back(result);}
        return result;
    }
    NativeCast resolve(Node current,std::uintptr_t owner) const {
        for(unsigned depth=0;current&&depth<128;++depth) {
            if(current->owner!=owner)return {};
            if(current->root.valid())return current->root;
            current=current->creator.lock();
        }
        return {};
    }
public:
    void reset() { objects_.clear();nodes_.clear();records_.clear(); }
    std::vector<NativeDamage> consume(std::span<const NativeEvent> batch,
                                      std::uintptr_t local,bool truncated=false) {
        // Reserve physical births first: a child factory can return before
        // its parent. Bind edges in event order so a persistent helper's new
        // command cannot retag projectiles from its previous command.
        // Published nodes are immutable between batches. A rollback snapshot
        // can share their graph; replayed births must never mutate that graph.
        std::set<ObjectKey> reserved;
        for(const auto &e:batch)if(e.kind==NativeEvent::Birth) {
            const ObjectKey key{e.object,e.generation};
            if(reserved.insert(key).second) {
                auto fresh=std::make_shared<Object>();nodes_.push_back(fresh);
                objects_[key]=std::move(fresh);
            }
        }
        std::vector<Node> hitSources(batch.size());
        for(std::size_t index=0;index<batch.size();++index) {
            const auto &e=batch[index];
            if(e.kind==NativeEvent::Birth || e.kind==NativeEvent::Rebind) {
                Node current;
                if(e.kind==NativeEvent::Rebind) {
                    current=std::make_shared<Object>();nodes_.push_back(current);
                    objects_[{e.object,e.generation}]=current;
                } else current=node({e.object,e.generation});
                current->owner=e.owner;
                if(e.creator==e.owner && e.cast)current->root={e.castAction,e.cast};
                else if(e.creator)current->creator=node({e.creator,e.creatorGeneration});
                else if(e.parent)current->creator=node({e.parent,e.parentGeneration});
                continue;
            }
            if(e.kind==NativeEvent::Hit&&e.object!=e.owner) {
                const auto it=objects_.find({e.object,e.generation});
                if(it!=objects_.end())hitSources[index]=it->second;
            }
        }
        std::vector<NativeDamage> result;
        for(std::size_t index=0;index<batch.size();++index) {
            const auto &e=batch[index];
            if(e.kind!=NativeEvent::Hit || e.damage<=0)continue;
            // ally is the engine's combo credit, owner is the physical owner.
            if(e.ally!=local)continue;
            NativeCast source;
            if(!truncated && e.owner==local && e.ally==local) {
                if(e.object==e.owner) {
                    if(e.cast && (canonicalAttackAction(e.boxAction)==e.castAction ||
                        isCharacterAttackContinuation(e.ownerCharacter,e.castAction,e.boxAction)))
                        source={e.castAction,e.cast};
                } else source=resolve(hitSources[index],e.owner);
            }
            std::uint64_t instance=0;
            if(source.valid()) {
                const auto key=std::make_pair(source.id,e.victim);
                auto [it,inserted]=records_.try_emplace(key,0);
                if(inserted)it->second=++serial_;
                instance=it->second;
            }
            result.push_back({e.damage,source.valid()?source.action:-1,instance,e.victim==local});
        }
        return result;
    }
};
}
