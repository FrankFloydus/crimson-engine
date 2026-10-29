#include "BrowserCatalog.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Isolated diagnostic: callers supply a disposable frozen project and catalog.
// No renderer/GPU initialization or writes to the authored project are needed.
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Expected frozen project, catalog JSON and output JSON.");
        using namespace crimson;using Clock=std::chrono::steady_clock;using Json=nlohmann::json;
        std::ifstream input(argv[2]);const auto data=Json::parse(input);std::vector<AssetEntry> assets;
        for(const auto& entry:data)assets.push_back({entry.at("id"),entry.at("name"),static_cast<AssetKind>(entry.at("kind").get<int>())});
        const auto start=Clock::now();BrowserCatalog catalog(argv[1]);catalog.synchronize(assets);
        Json report{{"assets",assets.size()},{"initializeMs",std::chrono::duration<double,std::milli>(Clock::now()-start).count()}};
        std::size_t consumed{};
        const auto measure=[&](const char* label,int samples,const auto& action) {
            std::vector<double> times;times.reserve(samples);action();
            for(int i=0;i<samples;++i){const auto before=Clock::now();consumed+=action();times.push_back(std::chrono::duration<double,std::milli>(Clock::now()-before).count());}
            std::sort(times.begin(),times.end());report[label]={{"samples",samples},{"p50Ms",times[times.size()/2]},{"p95Ms",times[times.size()*95/100]},{"maxMs",times.back()}};
        };
        const auto query=[&](std::string_view text,BrowserAssetFilter filter) {
#ifdef CC_BROWSER_VIEW_BENCHMARK
            const auto& items=catalog.itemsView(BrowserCatalog::RootFolder,text,filter);
#else
            const auto items=catalog.items(BrowserCatalog::RootFolder,text,filter);
#endif
            return items.size();
        };
        measure("defaultView",300,[&]{return query({},BrowserAssetFilter::All);});
        measure("warmSearch",100,[&]{return query("rock",BrowserAssetFilter::All);});
        measure("warmTypeFilter",300,[&]{return query({},BrowserAssetFilter::Textures);});
        int serial{};measure("changedSearch",30,[&]{return query("nonmatching-query-"+std::to_string(serial++),BrowserAssetFilter::All);});
        measure("permissions",100,[&]{std::size_t count{};for(const auto& asset:assets)count+=catalog.mutableItem(asset.id);return count;});
        report["consumed"]=consumed;
        std::ofstream output(argv[3]);output<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
