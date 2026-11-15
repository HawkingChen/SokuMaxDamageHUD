// Extract simulation data from archives owned by the tester. Never ship it.
#include "GameAssets.hpp"
#include <iostream>

int main(int argc,char **argv) {
    using namespace whm::assets;
    if(argc<3)return 2;
    const fs::path output=argv[1];
    try {
        for(int i=2;i<argc;++i) {
            std::ifstream input(argv[i],std::ios::binary);
            for(const auto &entry:readIndex(input)) {
                const bool selected=
                    entry.name=="data/effect/effect.pat" ||
                    entry.name=="data/weather/effect/effect.pat" ||
                    (entry.name.starts_with("data/character/")&&entry.name.ends_with(".pat")) ||
                    (entry.name.starts_with("data/csv/")&&entry.name.ends_with("/spellcard.cv1"));
                if(!selected || entry.name.find("..")!=std::string::npos)continue;
                const auto bytes=readEntry(input,entry);
                const auto destination=output/entry.name;
                fs::create_directories(destination.parent_path());
                std::ofstream file(destination,std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                if(!file)throw std::runtime_error("cannot write simulation data");
            }
        }
    } catch(const std::exception &error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
