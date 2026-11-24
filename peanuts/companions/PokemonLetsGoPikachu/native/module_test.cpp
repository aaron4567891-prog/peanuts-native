// SPDX-License-Identifier: GPL-3.0-or-later
// Fake guest memory only. Exercises the production module without a game/device.
#include "module.cpp"
#include <cassert>
#include <map>

struct Fake {
    uint64_t base = UINT64_C(0x100000000), heap = UINT64_C(0x200000000);
    std::vector<uint8_t> image = std::vector<uint8_t>(kRoot + 0x100);
    std::vector<uint8_t> data = std::vector<uint8_t>(0x60000);
    std::string selected_page="party";
    std::map<std::string, int64_t> numbers;
    std::map<std::string, std::string> texts;
    unsigned writes = 0;
    bool tear = false;
    unsigned core_reads = 0;

    uint8_t* At(uint64_t at, size_t size) {
        for (auto r : {std::pair<uint64_t, std::vector<uint8_t>*>(base,&image), {heap,&data}})
            if (at >= r.first && at - r.first <= r.second->size() &&
                size <= r.second->size() - size_t(at - r.first))
                return r.second->data() + size_t(at - r.first);
        return nullptr;
    }
    template<class T> void Put(uint64_t at, const T& v) {
        auto* dest = At(at, sizeof v); assert(dest); std::memcpy(dest, &v, sizeof v);
    }
    static EdenDsmodBool Mapped(void* p, uint64_t at, uint64_t n) {
        return static_cast<Fake*>(p)->At(at,n) != nullptr;
    }
    static EdenDsmodBool ReadMemory(void* p, uint64_t at, void* out, size_t n) {
        auto& f = *static_cast<Fake*>(p);
        auto* source = f.At(at,n); if (!source) return 0;
        std::memcpy(out,source,n);
        if (f.tear && at == f.heap + 0xc00 && ++f.core_reads == 2)
            static_cast<uint8_t*>(out)[0x20] ^= 1;
        return 1;
    }
    static EdenDsmodBool WriteMemory(void* p,uint64_t,const void*,size_t) {
        ++static_cast<Fake*>(p)->writes; return 0;
    }
    static void PublishNumber(void* p,const char* name,int64_t value) {
        static_cast<Fake*>(p)->numbers[name] = value;
    }
    static void PublishText(void* p,const char* name,const char* value) {
        static_cast<Fake*>(p)->texts[name] = value;
    }
    static const char* GetText(void* p,const char* name) {
        return std::strcmp(name,"__page")==0 ? static_cast<Fake*>(p)->selected_page.c_str() : nullptr;
    }
    EdenDsmodHostApi Host() {
        EdenDsmodHostApi h{};
        h.abi_version = EDEN_DSMOD_MODULE_ABI_VERSION; h.struct_size = sizeof h;
        h.abi_hash = EDEN_DSMOD_MODULE_ABI_HASH; h.title_id = kTitle;
        std::memcpy(h.build_id,kBuild,sizeof kBuild);
        h.main_base = base; h.main_size = image.size(); h.userdata = this;
        h.is_mapped=Mapped; h.read_memory=ReadMemory; h.write_memory=WriteMemory;
        h.publish_i64=PublishNumber; h.publish_text=PublishText;
        h.get_text=GetText;
        return h;
    }
    void Prepare() {
        Put(base+kRoot,heap); Put(heap,heap+0x100);
        Put(heap+0x198,heap+0x200); Put(heap+0x260,heap+0x400);
        Put(heap+0x400,base+kPartyVtable); Put(heap+0x458,heap+0x800);
        Put<uint8_t>(heap+0x488,1); Put(heap+0x898,heap+0xa00);
        Put(heap+0xa08,heap+0xe00); Put(heap+0xa10,heap+0xc00);
        Put<uint8_t>(heap+0xa18,0);
        pikachu::Core core{}; pikachu::Put16(core.data()+8,25);
        core[0x40]='T'; core[0x42]='e'; core[0x44]='s'; core[0x46]='t';
        pikachu::Put16(core.data()+0x5a,85);
        uint16_t sum=0; for(unsigned i=8;i<core.size();i+=2) sum+=pikachu::U16(core.data()+i);
        pikachu::Put16(core.data()+6,sum); Put(heap+0xc00,core);
        pikachu::Calc calc{}; calc[4]=8;
        const uint16_t fields[]={21,30,17,14,23,16,15};
        for(unsigned i=0;i<7;++i) pikachu::Put16(calc.data()+8+2*i,fields[i]);
        Put(heap+0xe00,calc);
    }
    void ClearOutput() { numbers.clear(); texts.clear(); core_reads=0; }
    void PrepareBagWorld() {
        Put(heap+0x258,heap+0x2000); Put(heap+0x2068,heap+0x2200);
        for(unsigned p=0;p<7;++p) Put(heap+0x2200+0xde8+8*p,heap+0x4000+0x400*p);
        for(unsigned i=0;i<17;++i) Put<uint32_t>(heap+0x4000+4*i,(i+1)|((i+2)<<15));
        Put(heap+0x290,heap+0x8000); Put(heap+0x8130,heap+0x8200);
        Put<uint64_t>(heap+0x8138,1); Put(heap+0x8200,heap+0x8400);
        Put(heap+0x8400,heap+0x9000); Put(heap+0x9078,base+0x212380);
        Put<uint32_t>(heap+0x85d4,1); Put<uint32_t>(heap+0x8f20,0);
        Put(heap+0x2b8,heap+0xa000); Put<uint32_t>(heap+0xa378,1);
        Put(heap+0xa328,heap+0xa800); Put<uint64_t>(heap+0xabc0,LocationHash("road001"));
    }
    void PrepareSavedParty() {
        Put(heap+0x400,base+kSavedPartyVtable);
        Put(heap+0x458,uint64_t(0));
        Put(heap+0x20b0,heap+0x10000); Put(heap+0x4f7f8,heap+0x52000);
        Put<uint16_t>(heap+0x52062,3); Put<uint16_t>(heap+0x52054,2);
        Put(heap+0x4f810,heap+0x800);
    }
};

int main() {
    Fake f; f.Prepare(); auto host=f.Host();
    auto* api=eden_dsmod_get_module(1,EDEN_DSMOD_MODULE_ABI_HASH);
    assert(api && !(api->capabilities & (EDEN_DSMOD_CAP_WRITE_MEMORY | EDEN_DSMOD_CAP_GUEST_CALL)));
    assert(!eden_dsmod_get_module(2,EDEN_DSMOD_MODULE_ABI_HASH));
    assert(api->supports_build("1C375692DE4F4B87"));
    assert(api->supports_build("1c375692de4f4b879abe88742dc0ca60"));
    assert(!api->supports_build("0000000000000000"));
    auto wrong=host; wrong.build_id[0]^=1; assert(!api->create(&wrong,"{}"));
    auto* state=api->create(&host,"{}"); assert(state);
    static_cast<State*>(state)->map_regions.push_back({LocationHash("road001"),11,1});
    assert(LocationHash("road001")==UINT64_C(0x66e0830c07060466));
    assert(LocationHash("town002")==UINT64_C(0x3a4d781a208ce22d));
    api->sample(state,&host);
    assert(f.numbers["lgp.ready"]==1 && f.numbers["lgp.party_count"]==1);
    assert(f.texts["lgp.p0.name"]=="Test" && f.numbers["lgp.p0.hp"]==21);
    assert(f.numbers["lgp.p0.spe"]==23 && f.numbers["lgp.p0.move0"]==85);
    assert(f.texts["lgp.p0.move0_name"]=="Move 85");
    assert(f.numbers["lgp.p1.valid"]==0 && f.writes==0);

    f.PrepareBagWorld(); f.selected_page="bag_0_1";
    f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.game_ready"]==1 && f.numbers["lgp.map_ready"]==1 && f.numbers["lgp.map_zone"]==11);
    assert(f.numbers["lgp.bag_ready"]==1 && f.numbers["lgp.bag.count"]==17);
    assert(f.numbers["lgp.bag.page"]==2 && f.numbers["lgp.bag.has_next"]==0);
    assert(f.numbers["lgp.bag.item0.id"]==11 && f.numbers["lgp.bag.item0.qty"]==12);
    assert(!f.numbers["lgp.bag.item7.valid"]);
    f.PrepareSavedParty(); f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.ready"]==1 && f.numbers["lgp.p0.hp"]==21);
    f.Put<uint16_t>(f.heap+0x52054,1001); f.ClearOutput(); api->sample(state,&host);
    assert(!f.numbers["lgp.ready"] && !f.numbers["lgp.p0.valid"]);
    f.Put<uint16_t>(f.heap+0x52054,2);
    f.Put<uint32_t>(f.heap+0x85d4,0); f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.game_ready"]==1 && !f.numbers["lgp.map_ready"]);
    f.Put<uint32_t>(f.heap+0x4000,1|(1000<<15)); f.ClearOutput(); api->sample(state,&host);
    assert(!f.numbers["lgp.bag_ready"] && !f.numbers["lgp.bag.item0.valid"]);

    f.Put<uint16_t>(f.heap+0xe08,18); f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.p0.hp"]==18);
    f.Put<uint16_t>(f.heap+0xe08,31); f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.ready"]==0 && !f.numbers["lgp.p0.valid"]);
    assert(!f.numbers.count("lgp.p0.hp"));
    f.Prepare(); f.tear=true; f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.ready"]==0); f.tear=false;
    f.Put<uint8_t>(f.heap+0x488,7); f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.ready"]==0);
    f.Put<uint8_t>(f.heap+0x488,0); f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.ready"]==0 && f.numbers["lgp.party_count"]==0);
    f.Prepare(); f.Put<uint64_t>(f.base+kRoot,0); f.ClearOutput(); api->sample(state,&host);
    assert(f.numbers["lgp.ready"]==0 && f.writes==0);
    assert(!f.numbers["lgp.game_ready"]);
    api->destroy(state);
    std::puts("Pikachu module: build gate, read-only party chain, live HP, invalid/empty/torn snapshots passed");
}
