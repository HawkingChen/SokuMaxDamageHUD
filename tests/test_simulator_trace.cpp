// Optional integration check: replay a simulator's observed creation/hit stream
// through the production attribution code. Input has no game assets.
// This verifies attribution of observed paths, not simulator/native equivalence.
#include "NativeDamage.hpp"
#include "RecordTracker.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc,char **argv) {
    if(argc!=2)return 2;
    std::ifstream input(argv[1]);if(!input)return 2;
    whm::NativeActors actors;
    whm::NativeCast pendingGhost[2]{};
    whm::NativeAttribution attribution;
    whm::RecordTracker records;
    int expectedDamage[2]={};unsigned incomplete=0;
    std::vector<whm::NativeEvent> frameEvents;
    std::string line,character;
    int action=0,distance=0;
    unsigned frame=0,cases=0,damaging=0,unresolved=0,misnamed=0,hits=0;
    bool caseDamaging=false;
    auto flush=[&] {
        for(const auto &hit:attribution.consume(frameEvents,1)) {
            ++hits;caseDamaging=true;
            records.observeDamage(hit.damage,hit.action,frame,hit.instance,false,0,hit.self);
            if(hit.action<0) {
                ++unresolved;
                if(unresolved<=30)std::cout<<character<<' '<<action<<' '<<distance
                    <<" unresolved damage="<<hit.damage<<'\n';
            }
            else if(hit.action!=whm::canonicalAttackAction(action)) {
                ++misnamed;
                if(misnamed<=30)std::cout<<character<<' '<<action<<' '<<distance
                    <<" attributed="<<hit.action<<" damage="<<hit.damage<<'\n';
            }
        }
        frameEvents.clear();
    };
    auto finishCase=[&] {
        flush();if(caseDamaging)++damaging;
        const int expected=std::max(expectedDamage[0],expectedDamage[1]);
        if(records.best().damage!=expected) {
            ++incomplete;
            if(incomplete<=30)std::cout<<character<<' '<<action<<' '<<distance
                <<" expected="<<expected<<" best="<<records.best().damage<<'\n';
        }
        records.reset();expectedDamage[0]=expectedDamage[1]=0;
    };
    while(std::getline(input,line)) {
        std::istringstream row(line);char kind;row>>kind;
        if(kind=='C') {
            finishCase();
            actors.reset();attribution.reset();pendingGhost[0]=pendingGhost[1]={};caseDamaging=false;++cases;
            row>>character>>action>>distance;frame=0;continue;
        }
        whm::NativeEvent event;unsigned nextFrame=0;
        if(kind=='A') {
            event.kind=whm::NativeEvent::Action;
            int command=0;
            row>>event.owner>>event.action>>nextFrame>>event.sequence>>command;
            const unsigned side=static_cast<unsigned>(event.owner-1);
            if(command<=0)pendingGhost[side]={};
            else if(!pendingGhost[side].valid())pendingGhost[side]=actors.get(side);
            const auto cast=actors.set(side,event.action,event.sequence,side==0&&character=="suwako"?19:-1);
            event.cast=cast.id;event.castAction=cast.action;
        } else if(kind=='R') {
            event.kind=whm::NativeEvent::Rebind;
            row>>event.object>>event.owner>>nextFrame;event.creator=event.owner;
            const unsigned side=static_cast<unsigned>(event.owner-1);
            const auto cast=pendingGhost[side].valid()?pendingGhost[side]:actors.get(side);
            event.cast=cast.id;event.castAction=cast.action;
        } else if(kind=='X') {
            std::uintptr_t owner;int command;row>>owner>>nextFrame>>command;
            if(!row)return 2;
            if(command==0)pendingGhost[owner-1]={};
            continue;
        } else if(kind=='B') {
            event.kind=whm::NativeEvent::Birth;
            row>>event.object>>event.owner>>event.creator>>event.parent>>event.action>>nextFrame;
            const auto cast=actors.get(static_cast<unsigned>(event.owner-1));
            event.cast=cast.id;event.castAction=cast.action;
        } else if(kind=='H') {
            event.kind=whm::NativeEvent::Hit;
            row>>event.object>>event.owner>>event.ally>>event.victim>>event.action>>event.damage>>nextFrame>>event.boxAction;
            const auto cast=actors.get(static_cast<unsigned>(event.owner-1));
            event.cast=cast.id;event.castAction=cast.action;
            event.ownerCharacter=event.owner==1&&character=="suwako"?19:-1;
        } else return 2;
        if(!row)return 2;
        if(kind=='H'&&event.ally==1&&event.victim>=1&&event.victim<=2)
            expectedDamage[event.victim-1]+=event.damage;
        if(nextFrame!=frame){flush();frame=nextFrame;}
        event.frame=frame;frameEvents.push_back(event);
    }
    finishCase();
    std::cout<<"cases="<<cases<<" damaging="<<damaging<<" hits="<<hits
        <<" unresolved="<<unresolved<<" differentRootAction="<<misnamed<<" incompleteCast="<<incomplete<<'\n';
    return unresolved||misnamed||incomplete?1:0;
}
