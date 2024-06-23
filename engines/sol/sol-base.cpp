#include <sol-base.hpp>
#include <map>
#include <algorithm>
#include <string.h>

namespace sol {
    struct BaseValue {
        std::vector<std::pair<vec8, void*>> props;
        std::mutex m;
        void Set(vec8 key, void* val) {
            m.lock();
            for (std::size_t i = 0; i < props.size(); i++) {
                if (vec8Compare(props[i].first, key)) {
                    props[i] = std::make_pair(key, val);
                    SOL_MUNLOCK(m)
                }
            }
            props.push_back(std::make_pair(key, val));
            SOL_MUNLOCK(m)
        }
        void* Get(vec8 key) {
            m.lock();
            for (std::size_t i = 0; i < props.size(); i++) {
                if (vec8Compare(props[i].first, key)) {
                    auto res = props[i].second;
                    SOL_MUNLOCKRET(m, res)
                }
            }
            SOL_MUNLOCKRET(m, NULL)
        }
        bool Has(vec8 key) {
            m.lock();
            for (std::size_t i = 0; i < props.size(); i++) {
                if (vec8Compare(props[i].first, key)) {
                    SOL_MUNLOCKRET(m, true)
                }
            }
            SOL_MUNLOCKRET(m, false)
        }
    };
    namespace gc {
        std::vector<void*> all;
        std::vector<void*> persistent;
        std::map<void*, std::vector<void*>> refs;
        std::mutex gc_m;
    }
    std::map<std::thread::id, Thread*> threads;
    std::mutex threads_m;
}

bool sol::vec8Compare(vec8 v1, vec8 v2) {
    if (v1.size() != v2.size()) return false;
    for (std::size_t i = 0; i < v1.size(); i++) {
        if (v1[i] != v2[i]) return false;
    }
    return true;
}

auto Noop = [](){};

void sol::Value::CoreSet(vec8 key, void* val) {
    ((sol::BaseValue*)_)->Set(key, val);
}

void* sol::Value::CoreGet(vec8 key) {
    return ((sol::BaseValue*)_)->Get(key);
}

bool sol::Value::CoreHas(vec8 key) {
    return ((sol::BaseValue*)_)->Has(key);
}

sol::Thread* sol::Thread::New(voidfn code) {
    threads_m.lock();
    Thread* th = new Thread;
    th->t = std::thread([code,th](){
        th->tm.lock();
        code();
        th->tm.unlock();
    });
    th->w = std::thread([th](){
        th->wm.lock();
        th->tm.lock();
        th->tm.unlock();
        auto st = th->st;
        for (std::size_t i = 0; i < st.size(); i++) {
            auto e = st[i];
            e->Wait();
        }
        th->wm.unlock();
    });
    threads[th->t.get_id()] = th;
    SOL_MUNLOCKRET(threads_m, th)
}

sol::Thread* sol::Thread::Spawn(voidfn code) {
    auto res = Thread::New(code);
    this->st.push_back(res);
    return res;
}

void sol::Thread::Wait() {
    this->wm.lock();
    this->wm.unlock();
}

void sol::InitsManager::AddInitNow(voidfn initer, voidfn deiniter) {
    m.lock();
    initer();
    deiniters.push_back(deiniter);
    SOL_MUNLOCK(m)
}

void sol::InitsManager::AddInitLater(voidfn initer, voidfn deiniter) {
    m.lock();
    initLaters.emplace_back(initer, deiniter);
    SOL_MUNLOCK(m)
}

void sol::InitsManager::InitLaters() {
    m.lock();
    for (auto i : initLaters) {
        i.first();
        deiniters.push_back(i.second);
    }
    initLaters.clear();
    SOL_MUNLOCK(m)
}

void sol::InitsManager::Deinit() {
    m.lock();
    std::reverse(deiniters.begin(), deiniters.end());
    for (auto i : deiniters) i();
    deiniters.clear();
    SOL_MUNLOCK(m)
}

std::function<sol::Value()> mkundef;
std::function<sol::Value()> mknull;

void sol::Init() {
    InitsManager* im = new InitsManager;
    GlobalIM.AddInitNow([im](){
        im->AddInitNow(Noop, [](){
            for (auto i : threads) delete i.second;
        });
        im->AddInitNow([](){
            mkundef = mknew([](){
                BaseValue* val = new BaseValue;
                vec8* type = new vec8(cstringToVec8("undefined"));
                val->Set(cstringToVec8("type"), type);
                val->Set(cstringToVec8("copy"), new std::function(mkundef));
                val->Set(cstringToVec8("collect"), mkcollect(val, [val](){
                    delete val->Get(cstringToVec8("type"));
                    delete val->Get(cstringToVec8("copy"));
                    delete val->Get(cstringToVec8("collect"));
                    delete val;
                }));
                return val;
            });
            mknull = mknew([](){
                BaseValue* val = new BaseValue;
                vec8* type = new vec8(cstringToVec8("null"));
                val->Set(cstringToVec8("type"), type);
                val->Set(cstringToVec8("copy"), new std::function(mknull));
                val->Set(cstringToVec8("collect"), mkcollect(val, [val](){
                    delete val->Get(cstringToVec8("type"));
                    delete val->Get(cstringToVec8("copy"));
                    delete val->Get(cstringToVec8("collect"));
                    delete val;
                }));
                return val;
            });
        }, [](){
            gc::gc_m.lock();
            std::vector<void*> all_c(gc::all.begin(), gc::all.end());
            gc::gc_m.unlock();
            for (auto i : all_c) {
                sol::Value r;
                r._ = i;
                r.Collect();
            }
        });
    }, [im](){
        im->Deinit();
        delete im;
    });
}

void sol::Teardown() {
    GlobalIM.Deinit();
}

sol::Thread* sol::SpawnThread(voidfn code) {
    threads_m.lock();
    Thread* t = threads[std::this_thread::get_id()];
    SOL_MUNLOCKRET(threads_m, t->Spawn(code))
}

sol::vec8 sol::cstringToVec8(char* cstr) {
    std::vector<uint8_t> res;
    for (size_t i = 0; true; i++) {
        if (cstr[i] == '\0') break;
        res.push_back(cstr[i]);
    }
    return res;
}

std::string sol::vec8ToStdString(vec8 v) {
    std::string result;
    for (auto i : v) {
        result.push_back(i);
    }
    return result;
}

std::function<void()>* mkcollect(sol::BaseValue* val, std::function<void()> fn) {
    return new std::function([val,fn](){
        sol::gc::gc_m.lock();
        fn();
        std::size_t i = 0;
        while (i < sol::gc::all.size()) {
            if (sol::gc::all[i] == val) break;
            i++;
        }
        sol::gc::all.erase(sol::gc::all.begin() + i);
        i = 0;
        bool persistent = false;
        while (i < sol::gc::persistent.size()) {
            if (sol::gc::persistent[i] == val) {
                persistent = true;
                break;
            }
            i++;
        }
        if (persistent) sol::gc::persistent.erase(sol::gc::persistent.begin() + i);
        auto it = sol::gc::refs.find(val);
        sol::gc::refs.erase(it);
        SOL_MUNLOCK(sol::gc::gc_m)
    });
}

std::function<sol::Value()> mknew(std::function<sol::BaseValue*()> fn) {
    return [fn](){
        sol::gc::gc_m.lock();
        sol::BaseValue* val = fn();
        sol::Value res;
        res._ = val;
        sol::gc::all.push_back(val);
        sol::gc::persistent.push_back(val);
        sol::gc::refs[val] = std::vector<void*>();
        SOL_MUNLOCKRET(sol::gc::gc_m, res)
    };
}

sol::Value sol::Value::NewUndefined() {
    return mkundef();
}

sol::Value sol::Value::Copy() {
    BaseValue* val = (BaseValue*)_;
    std::function<Value()>* fnptr = (std::function<Value()>*)val->Get(cstringToVec8("copy"));
    return (*fnptr)();
}

void sol::Value::Collect() {
    auto fnptr = (voidfn*)(((BaseValue*)_)->Get(cstringToVec8("collect")));
    (*fnptr)();
}

sol::Value sol::Value::NewNull() {
    return mknull();
}

void sol::Value::MakeNotPersistent() {
    gc::gc_m.lock();
    std::size_t i = 0;
    bool persistent = false;
    while (i < gc::persistent.size()) {
        if (gc::persistent[i] == _) {
            persistent = true;
            break;
        }
        i++;
    }
    if (persistent) gc::persistent.erase(gc::persistent.begin() + i);
    SOL_MUNLOCK(gc::gc_m)
}

void sol::Value::MakePersistent() {
    gc::gc_m.lock();
    bool persistent = false;
    for (auto i : gc::persistent) {
        if (i == _) {
            persistent = true;
            break;
        }
    }
    if (!persistent) gc::persistent.push_back(_);
    SOL_MUNLOCK(gc::gc_m)
}

sol::vec16 sol::utf8To16(vec8 val) {
    std::vector<uint32_t> codepoints;
    std::size_t i = 0;
    while (i < val.size()) {
        uint8_t curr = val[i];
        if (((curr & 0b10000000) >> 7) == 0) {
            codepoints.push_back(curr);
            i++;
            continue;
        }
        if (((curr & 0b01000000) >> 6) == 0) {
            i++;
            continue;
        }
        if (((curr & 0b00100000) >> 5) == 0) {
            uint8_t part1 = (curr & 0b00011111);
            i++;
            uint8_t next = val[i];
            if ((((next & 0b1000000) >> 7) == 0) || (((next & 0b01000000) >> 6) == 1)) {
                continue;
            }
            uint8_t part2 = (curr & 0b00111111);
            codepoints.push_back((part1 << 6) + part2);
            i++;
            continue;
        }
    }
}