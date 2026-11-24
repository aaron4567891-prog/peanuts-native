// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only, exact-build live party reader. Address derivation and validation
// are documented in the companion README; no native game functions are called.
#include "pb7.hpp"
#include "message_text.hpp"
#include "dsmod_module_abi.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <new>
#include <algorithm>
#include <vector>

namespace {
constexpr uint64_t kTitle = UINT64_C(0x010003f003a34000);
constexpr uint64_t kRoot = 0x15fad08;
constexpr uint64_t kPartyVtable = 0x159c668;
constexpr uint64_t kSavedPartyVtable = 0x1539ef8;
constexpr uint8_t kBuild[16] = {
    0x1c,0x37,0x56,0x92,0xde,0x4f,0x4b,0x87,
    0x9a,0xbe,0x88,0x74,0x2d,0xc0,0xca,0x60
};

struct MapRegion { uint64_t hash; uint16_t zone, text; };
uint64_t LocationHash(const std::string& text) {
    uint64_t value=UINT64_C(0xcbf29ce484222645);
    for(unsigned char c:text) value=(value^c)*UINT64_C(0x100000001b3);
    return value;
}
struct State {
    std::vector<std::string> species_names, move_names, item_names, pouch_names, place_names;
    std::vector<MapRegion> map_regions;
    std::string last_status;
    unsigned last_count = 99;
    bool last_world = false;
    uint64_t entered_context = 0;
};

bool HostValid(const EdenDsmodHostApi* h) {
    return h && h->struct_size >= sizeof(EdenDsmodHostApi) &&
        h->abi_version == EDEN_DSMOD_MODULE_ABI_VERSION &&
        h->abi_hash == EDEN_DSMOD_MODULE_ABI_HASH && h->title_id == kTitle &&
        std::memcmp(h->build_id, kBuild, sizeof kBuild) == 0 &&
        h->is_mapped && h->read_memory && h->publish_i64 && h->publish_text &&
        h->main_base && h->main_size > kRoot + 8;
}

template<class T> bool Read(const EdenDsmodHostApi* h, uint64_t at, T& out) {
    return at >= 0x10000 && at <= UINT64_MAX - sizeof(T) &&
        h->is_mapped(h->userdata, at, sizeof(T)) &&
        h->read_memory(h->userdata, at, &out, sizeof(T));
}
bool Ptr(const EdenDsmodHostApi* h, uint64_t at, uint64_t& out) {
    return Read(h, at, out) && out >= 0x10000 && !(out & 7);
}

bool Context(const EdenDsmodHostApi* h, uint64_t& context) {
    uint64_t root{}, handle{};
    return Ptr(h, h->main_base + kRoot, root) && Ptr(h, root, handle) &&
        Ptr(h, handle + 0x98, context);
}

struct PartyHeader {
    uint64_t root{}, handle{}, context{}, party{}, vtable{};
    uint64_t manager{},save{},metadata{};
    uint16_t saved_count{};
    std::array<uint16_t,6> indices{};
    std::array<uint64_t, 6> slots{};
    uint8_t count{};
    bool operator==(const PartyHeader& other) const {
        return root == other.root && handle == other.handle && context == other.context &&
            party == other.party && vtable == other.vtable && count == other.count &&
            slots == other.slots && manager==other.manager && save==other.save &&
            metadata==other.metadata && saved_count==other.saved_count && indices==other.indices;
    }
};

bool Header(const EdenDsmodHostApi* h, PartyHeader& p, const char*& why) {
    why = "Waiting for the game's party manager";
    if (!Ptr(h, h->main_base + kRoot, p.root) || !Ptr(h, p.root, p.handle) ||
        !Ptr(h, p.handle + 0x98, p.context) || !Ptr(h, p.context + 0x60, p.party))
        return false;
    why = "Waiting for a recognized party object";
    if (!Read(h, p.party, p.vtable) || (p.vtable != h->main_base + kPartyVtable &&
        p.vtable != h->main_base+kSavedPartyVtable))
        return false;
    why = "Waiting for a consistent party count";
    if (!Read(h, p.party + 0x88, p.count) || p.count > 6) return false;
    const bool saved=p.vtable==h->main_base+kSavedPartyVtable;
    if(saved && p.count && (!Ptr(h,p.context+0x58,p.manager) || !Ptr(h,p.manager+0xb0,p.save) ||
        !Ptr(h,p.save+0x3f7f8,p.metadata) || !Read(h,p.metadata+0x62,p.saved_count) || p.saved_count>1001)) return false;
    for (unsigned i = 0; i < p.count; ++i) {
        if(saved) {
            if(!Read(h,p.metadata+0x54+2*i,p.indices[i]) || p.indices[i]==1001 ||
                p.indices[i]>=p.saved_count || !Ptr(h,p.save+0x3f800+8*p.indices[i],p.slots[i])) return false;
        } else if (!Ptr(h, p.party + 0x58 + i * 8, p.slots[i])) return false;
        for (unsigned j = 0; j < i; ++j) if (p.slots[i] == p.slots[j]) return false;
    }
    return true;
}

bool PokemonAt(const EdenDsmodHostApi* h, uint64_t parameter, pikachu::Pokemon& result) {
    uint64_t accessor{}, calc_at{}, core_at{};
    uint8_t encrypted{}, encrypted_after{};
    if (!Ptr(h, parameter + 0x98, accessor) ||
        !Ptr(h, accessor + 8, calc_at) || !Ptr(h, accessor + 0x10, core_at) ||
        !Read(h, accessor + 0x18, encrypted)) return false;
    pikachu::Core core{}, second_core{};
    pikachu::Calc calc{}, second_calc{};
    // Accessor methods temporarily decrypt/re-encrypt; two complete equal
    // snapshots plus the core checksum protect against flag ABA/transitions.
    if (!Read(h, core_at, core) || !Read(h, calc_at, calc) ||
        !Read(h, core_at, second_core) || !Read(h, calc_at, second_calc) ||
        core != second_core || calc != second_calc ||
        !Read(h, accessor + 0x18, encrypted_after) || encrypted != encrypted_after)
        return false;
    uint64_t after_accessor{}, after_core{}, after_calc{};
    if (!Ptr(h, parameter + 0x98, after_accessor) || after_accessor != accessor ||
        !Ptr(h, accessor + 8, after_calc) || after_calc != calc_at ||
        !Ptr(h, accessor + 0x10, after_core) || after_core != core_at)
        return false;
    return pikachu::DecodeNative(core, calc, encrypted != 0, result);
}

std::vector<std::string> Names(const EdenDsmodHostApi* h, const char* path) {
    if (!h->read_romfs || !(h->capabilities & EDEN_DSMOD_CAP_ROMFS_READ)) return {};
    std::vector<uint8_t> bytes(1u << 20);
    const size_t n = h->read_romfs(h->userdata, path, 0, bytes.data(), bytes.size());
    if (!n || n >= bytes.size()) return {};
    bytes.resize(n);
    return pikachu::DecodeMessages(bytes);
}

void Number(const EdenDsmodHostApi* h, const char* key, int64_t n) {
    h->publish_i64(h->userdata, key, n);
}
void Text(const EdenDsmodHostApi* h, const char* key, const std::string& value) {
    h->publish_text(h->userdata, key, value.c_str());
}
void SlotNumber(const EdenDsmodHostApi* h, unsigned slot, const char* field, int64_t n) {
    char key[80]; std::snprintf(key, sizeof key, "lgp.p%u.%s", slot, field); Number(h, key, n);
}
void SlotText(const EdenDsmodHostApi* h, unsigned slot, const char* field, const std::string& text) {
    char key[80]; std::snprintf(key, sizeof key, "lgp.p%u.%s", slot, field); Text(h, key, text);
}

std::string Name(const std::vector<std::string>& names, unsigned id, const char* prefix) {
    if (id < names.size() && !names[id].empty()) return names[id];
    return std::string(prefix) + " " + std::to_string(id);
}

// The native quantity getter scans packed words in these seven fixed pouches.
// Read complete equal snapshots and recheck the chain; never call that getter.
constexpr std::array<unsigned,7> kPouchCapacity{60,108,200,150,50,150,150};
struct BagHeader {
    uint64_t context{}, manager{}, bag{};
    std::array<uint64_t,7> pouches{};
    bool operator==(const BagHeader& b) const {
        return context==b.context && manager==b.manager && bag==b.bag && pouches==b.pouches;
    }
};
bool ReadBagHeader(const EdenDsmodHostApi* h, BagHeader& b) {
    if (!Context(h,b.context) || !Ptr(h,b.context+0x58,b.manager) ||
        !Ptr(h,b.manager+0x68,b.bag)) return false;
    for (unsigned p=0;p<7;++p)
        if (!Ptr(h,b.bag+0xde8+8*p,b.pouches[p])) return false;
    return true;
}
struct BagItem { unsigned id, quantity; };
void SampleBag(State& state, const EdenDsmodHostApi* h) {
    Number(h,"lgp.bag_ready",0);
    Number(h,"lgp.bag.has_next",0);
    Number(h,"lgp.bag.count",0);
    for(unsigned i=0;i<10;++i) {
        const auto key="lgp.bag.item"+std::to_string(i)+".valid";
        Number(h,key.c_str(),0);
    }
    Text(h,"lgp.bag_status_text","Waiting for a stable live Bag");
    BagHeader before{},after{};
    if(!ReadBagHeader(h,before)) return;
    std::array<std::vector<BagItem>,7> items;
    for(unsigned p=0;p<7;++p) {
        std::array<uint32_t,200> a{},b{};
        const size_t n=kPouchCapacity[p]*sizeof(uint32_t);
        if(!h->is_mapped(h->userdata,before.pouches[p],n) ||
           !h->read_memory(h->userdata,before.pouches[p],a.data(),n) ||
           !h->read_memory(h->userdata,before.pouches[p],b.data(),n) || a!=b) return;
        std::array<bool,2048> seen{};
        for(unsigned j=0;j<kPouchCapacity[p];++j) {
            const unsigned id=a[j]&0x7fff, quantity=(a[j]>>15)&0x7fff;
            if(!id || !quantity) continue;
            if(id>=seen.size() || quantity>999 || seen[id] ||
               (!state.item_names.empty() && id>=state.item_names.size())) return;
            seen[id]=true; items[p].push_back({id,quantity});
        }
        const auto key="lgp.bag.p"+std::to_string(p)+".count";
        Number(h,key.c_str(),items[p].size());
    }
    if(!ReadBagHeader(h,after) || !(before==after)) return;
    unsigned pouch=0,page=0; int consumed=0;
    const char* selected=h->get_text ? h->get_text(h->userdata,"__page") : nullptr;
    if(selected && std::sscanf(selected,"bag_%u_%u%n",&pouch,&page,&consumed)==2) {
        if(selected[consumed] || pouch>=7 || page>19) pouch=page=0;
    } else pouch=page=0;
    const auto& list=items[pouch];
    const unsigned pages=std::max(1u,static_cast<unsigned>((list.size()+9)/10));
    page=std::min(page,pages-1);
    Number(h,"lgp.bag_ready",1); Number(h,"lgp.bag.count",list.size());
    Number(h,"lgp.bag.page",page+1); Number(h,"lgp.bag.pages",pages);
    Number(h,"lgp.bag.has_next",page+1<pages);
    Text(h,"lgp.bag.pouch_name",Name(state.pouch_names,(pouch+1)%7,"Pouch"));
    Text(h,"lgp.bag_status_text",list.empty()?"This pouch is empty":"Live Bag - read only");
    for(unsigned i=0;i<10 && page*10+i<list.size();++i) {
        const auto& item=list[page*10+i];
        const auto key="lgp.bag.item"+std::to_string(i)+".";
        Number(h,(key+"valid").c_str(),1); Number(h,(key+"id").c_str(),item.id);
        Number(h,(key+"qty").c_str(),item.quantity);
        Text(h,(key+"name").c_str(),Name(state.item_names,item.id,"Item"));
    }
}

struct World {
    uint64_t context{},field{},vector{},count{},actor{},vtable{},predicate{},world{},entry{},zone{};
    uint32_t actor_id{},active{},zone_count{};
    bool operator==(const World& b) const {
        return context==b.context && field==b.field && vector==b.vector && count==b.count &&
            actor==b.actor && vtable==b.vtable && predicate==b.predicate && world==b.world &&
            entry==b.entry && zone==b.zone && actor_id==b.actor_id && active==b.active &&
            zone_count==b.zone_count;
    }
};
bool WorldAt(const EdenDsmodHostApi* h,World& w) {
    if(!Context(h,w.context) || !Ptr(h,w.context+0x90,w.field) ||
       !Ptr(h,w.field+0x130,w.vector) || !Read(h,w.field+0x138,w.count) ||
       !w.count || w.count>4096 || !h->is_mapped(h->userdata,w.vector,w.count*8)) return false;
    for(uint64_t i=0;i<w.count;++i) {
        uint64_t actor{},vtable{},predicate{}; uint32_t id{},active{};
        if(!Ptr(h,w.vector+8*i,actor) || !Read(h,actor+0xb20,id) || id!=0 ||
           !Ptr(h,actor,vtable) || !Read(h,vtable+0x78,predicate) ||
           predicate!=h->main_base+0x212380 || !Read(h,actor+0x1d4,active) || !active) continue;
        w.actor=actor; w.vtable=vtable; w.predicate=predicate; w.actor_id=id; w.active=active;
        break;
    }
    return w.actor && Ptr(h,w.context+0xb8,w.world) && Read(h,w.world+0x378,w.zone_count) &&
        w.zone_count>0 && w.zone_count<=10 && Ptr(h,w.world+0x328,w.entry) &&
        Read(h,w.entry+0x3c0,w.zone);
}
void SampleWorld(State& state,const EdenDsmodHostApi* h) {
    Number(h,"lgp.game_ready",0); Number(h,"lgp.map_ready",0);
    Text(h,"lgp.map_status_text","Waiting for the loaded game world");
    World a{},b{};
    const bool ready=WorldAt(h,a) && WorldAt(h,b) && a==b;
    uint64_t current_context{};
    if(!Context(h,current_context) || current_context!=state.entered_context) state.entered_context=0;
    // Keep the companion visible through battles/menus after a live overworld
    // is seen. A changed/destroyed game context clears this session-local latch.
    if(ready) state.entered_context=a.context;
    Number(h,"lgp.game_ready",state.entered_context!=0);
    if(ready) {
        Number(h,"lgp.game_ready",1);
        Text(h,"lgp.map.name","Unmapped area");
        Text(h,"lgp.map_status_text","This area has no region-map marker");
        for(const auto& region:state.map_regions) {
            if(a.zone==region.hash) {
                Number(h,"lgp.map_ready",1);
                Number(h,"lgp.map_zone",region.zone);
                Text(h,"lgp.map.name",Name(state.place_names,region.text+2,"Area"));
                Text(h,"lgp.map_status_text","Current area - region marker, not precise player position");
                break;
            }
        }
    }
    if(ready!=state.last_world) {
        state.last_world=ready;
        if(h->log) {
            char message[160]; std::snprintf(message,sizeof message,
                "Pikachu world reader: %s; zone 0x%llx",
                ready?"live active player and stable area":"waiting for loaded world",
                static_cast<unsigned long long>(a.zone));
            h->log(h->userdata,EDEN_DSMOD_LOG_INFO,message);
        }
    }
}

void Sample(void* opaque, const EdenDsmodHostApi* h) {
    if (!opaque || !HostValid(h)) return;
    auto& state = *static_cast<State*>(opaque);
    if (h->begin_output) h->begin_output(h->userdata);
    SampleWorld(state,h);
    SampleBag(state,h);
    Number(h, "lgp.ready", 0); Number(h, "lgp.party_count", 0);
    for (unsigned i = 0; i < 6; ++i) SlotNumber(h, i, "valid", 0);
    const char* status = nullptr;
    PartyHeader first{}, last{};
    std::array<pikachu::Pokemon, 6> party{};
    bool valid = Header(h, first, status);
    if (valid && first.count == 0) status = "No party yet - continue to your first Pokemon";
    if (valid && first.count) {
        for (unsigned i = 0; i < first.count; ++i) {
            if (!PokemonAt(h, first.slots[i], party[i])) {
                valid = false; status = "Waiting for a stable, checksum-valid party"; break;
            }
        }
        const char* ignored = nullptr;
        if (valid && (!Header(h, last, ignored) || !(first == last))) {
            valid = false; status = "Party changed - reading again";
        }
    }
    unsigned count = valid ? first.count : 0;
    if (count) {
        status = "Live party - tap a Pokemon for details";
        Number(h, "lgp.ready", 1); Number(h, "lgp.party_count", count);
        for (unsigned i = 0; i < count; ++i) {
            const auto& p = party[i];
            const auto species = Name(state.species_names, p.species, "Species");
            SlotNumber(h, i, "valid", 1);
            SlotText(h, i, "name", p.name.empty() ? species : p.name);
            SlotText(h, i, "species_name", species);
            SlotNumber(h, i, "species", p.species); SlotNumber(h, i, "level", p.level);
            SlotNumber(h, i, "hp", p.hp); SlotNumber(h, i, "hpmax", p.hpmax);
            SlotNumber(h, i, "atk", p.atk); SlotNumber(h, i, "def", p.def);
            SlotNumber(h, i, "spa", p.spa); SlotNumber(h, i, "spd", p.spd);
            SlotNumber(h, i, "spe", p.spe);
            for (unsigned move = 0; move < 4; ++move) {
                const std::string field = "move" + std::to_string(move);
                SlotNumber(h, i, field.c_str(), p.moves[move]);
                const auto name = p.moves[move] ? Name(state.move_names, p.moves[move], "Move") : "None";
                SlotText(h, i, (field + "_name").c_str(), name);
                SlotNumber(h, i, (field + "_pp").c_str(), p.pp[move]);
            }
        }
    }
    Text(h, "lgp.status_text", status ? status : "Waiting for live party data");
    if (state.last_status != (status ? status : "") || state.last_count != count) {
        state.last_status = status ? status : ""; state.last_count = count;
        if (h->log) {
            char message[192]; std::snprintf(message, sizeof message,
                "Pikachu party reader: %s; validated slots %u", state.last_status.c_str(), count);
            h->log(h->userdata, EDEN_DSMOD_LOG_INFO, message);
        }
    }
    if (h->end_output) h->end_output(h->userdata);
}

EdenDsmodBool Supports(const char* build) {
    if (!build) return 0;
    const size_t len = std::strlen(build);
    if (len != 16 && len != 32 && len != 64) return 0;
    const char* expected = "1C375692DE4F4B879ABE88742DC0CA6000000000000000000000000000000000";
    for (size_t i = 0; i < len; ++i)
        if (std::toupper(static_cast<unsigned char>(build[i])) != expected[i]) return 0;
    return 1;
}
void* Create(const EdenDsmodHostApi* h, const char*) {
    if (!HostValid(h)) return nullptr;
    auto* state = new (std::nothrow) State;
    if (!state) return nullptr;
    state->species_names = Names(h, "bin/message/English/common/monsname.dat");
    state->move_names = Names(h, "bin/message/English/common/wazaname.dat");
    state->item_names = Names(h, "bin/message/English/common/itemname.dat");
    state->pouch_names = Names(h, "bin/message/English/common/bag_pocket.dat");
    state->place_names = Names(h, "bin/message/English/common/place_name.dat");
    if(h->read_romfs) {
        std::array<uint8_t,3097> data{};
        const auto n=h->read_romfs(h->userdata,"bin/app/townmap/bin/beluga_townmap_data.bin",0,data.data(),data.size());
        if(n==3096) {
            for(unsigned i=0;i<43;++i) {
                const auto* r=data.data()+i*72;
                const uint16_t first=pikachu::U16(r+52),count=pikachu::U16(r+54),text=pikachu::U16(r+60);
                if(!count || first>=48 || count>2 || count>48-first || text>=54) continue;
                const char* name=reinterpret_cast<const char*>(r+4);
                size_t len=0; while(len<32 && name[len]) ++len;
                if(!len || len==32) continue;
                std::string identifier(name,len);
                state->map_regions.push_back({LocationHash(identifier),first,text});
                if(count==2) {
                    if(identifier.size()>2 && identifier.substr(identifier.size()-2)=="_1") identifier.back()='2';
                    else identifier+="_2";
                    state->map_regions.push_back({LocationHash(identifier),first,text});
                }
            }
            if(state->map_regions.size()!=48) state->map_regions.clear();
        }
    }
    if (h->log) {
        char message[128]; std::snprintf(message, sizeof message,
            "Pikachu names loaded from game: %zu species, %zu moves",
            state->species_names.size(), state->move_names.size());
        h->log(h->userdata, EDEN_DSMOD_LOG_INFO, message);
    }
    return state;
}
void Destroy(void* state) { delete static_cast<State*>(state); }
void Tick(void*, const EdenDsmodHostApi*) {}
const EdenDsmodModuleApi kApi{
    EDEN_DSMOD_MODULE_ABI_VERSION, sizeof(EdenDsmodModuleApi), 0,
    EDEN_DSMOD_MODULE_ABI_HASH, kTitle, "Pokemon Let's Go Pikachu live party",
    EDEN_DSMOD_CAP_ROMFS_READ, Supports, Create, Destroy, Sample, Tick
};
} // namespace

extern "C" __attribute__((visibility("default"))) const EdenDsmodModuleApi*
eden_dsmod_get_module(uint32_t version, uint64_t hash) {
    return version == EDEN_DSMOD_MODULE_ABI_VERSION && hash == EDEN_DSMOD_MODULE_ABI_HASH ? &kApi : nullptr;
}
