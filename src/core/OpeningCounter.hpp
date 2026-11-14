#pragma once
// Yellow Sand (黄砂) forces CH on the opening hit *when the hit itself has
// 与CH/CH eligibility*. Not every attack, and not every later hit.
// Profiles require explicit provenance. Unknown attacks are NOT boosted.
#include <algorithm>
#include <cmath>
#include <istream>
#include <sstream>
#include <string>
#include <vector>
#include "HudText.hpp"

namespace whm {
struct OpeningCounterRule {
    int character=-1,card=-1,level=-1;
    std::string notation;
    int firstNeutral=0; // one qualified hit; NEVER whole multi-hit total
    std::string source;
};

class OpeningCounterCatalog {
    std::vector<OpeningCounterRule> rules_;
public:
    bool read(std::istream &in) {
        rules_.clear();std::string line;bool valid=true;
        while(std::getline(in,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.empty()||line[0]=='#')continue;
            std::vector<std::string> v;
            std::istringstream s(line);std::string cell;
            while(std::getline(s,cell,'|'))v.push_back(cell);
            try {
                if(v.size()!=6) { valid=false;continue; }
                OpeningCounterRule r{std::stoi(v[0]),std::stoi(v[1]),std::stoi(v[3]),
                                      normalizeNotation(v[2]),std::stoi(v[4]),v[5]};
                if(r.character<0||r.character>=20||r.card<100||r.card>=132||
                   r.level< -1||r.level>4||r.firstNeutral<=0||r.firstNeutral>10000||
                   r.notation.empty()||r.source.rfind("https://",0)!=0) {valid=false;continue;}
                bool duplicate=false;
                for(const auto &old:rules_)
                    if(old.character==r.character&&old.card==r.card&&old.level==r.level&&
                       old.notation==r.notation)duplicate=true;
                if(duplicate) {valid=false;continue;}
                rules_.push_back(std::move(r));
            } catch(...) {valid=false;}
        }
        return valid;
    }
    int firstHit(int character,int card,const std::string &notation,int level)const {
        int fallback=0;
        const std::string command=normalizeNotation(notation);
        for(const auto &r:rules_) {
            if(r.character!=character||r.card!=card||r.notation!=command)continue;
            if(r.level==level)return r.firstNeutral;
            if(r.level==-1)fallback=r.firstNeutral;
        }
        return fallback;
    }
    std::size_t size()const{return rules_.size();}
};

inline int addOpeningCounterHit(int neutralTotal,int firstHitNeutral,
                                double effectiveMultiplier,bool eligible,
                                bool freshCombo) {
    if(neutralTotal<=0||!std::isfinite(effectiveMultiplier)||
       effectiveMultiplier<=0.||effectiveMultiplier>50.)return 0;
    const int normal=static_cast<int>(neutralTotal*effectiveMultiplier);
    if(!eligible||!freshCombo||firstHitNeutral<=0)return normal;
    // Apply CH to only the verified first hit, and never boost more neutral
    // damage than exists in this full-move reference.
    const int first=std::min(firstHitNeutral,neutralTotal);
    return normal+static_cast<int>(std::floor(first*effectiveMultiplier/3.+1e-8));
}
} // namespace whm
