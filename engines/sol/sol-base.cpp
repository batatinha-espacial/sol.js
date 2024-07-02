#include <sol-base.hpp>
#include <map>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <gmpxx.h>

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
    mpf_t symnum;
    std::vector<std::pair<mpf_ptr, BaseString>> symdescs;
    std::mutex sym_m;
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
std::function<sol::Value()> mksym;

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
            mpf_init(symnum);
            mksym = mknew([](){
                BaseValue* val = new BaseValue;
                vec8* type = new vec8(cstringToVec8("symbol"));
                val->Set(cstringToVec8("type"), type);
                val->Set(cstringToVec8("copy"), new std::function([val](){
                    auto res = mksym();
                    mpf_ptr num = new mpf_t;
                    mpf_init(num);
                    mpf_set(num, (mpf_ptr)val->Get(cstringToVec8("sym")));
                    res.CoreSet(cstringToVec8("sym"), num);
                    return res;
                }));
                val->Set(cstringToVec8("collect"), mkcollect(val, [val](){
                    mpf_clear((mpf_ptr)val->Get(cstringToVec8("sym")));
                    delete val->Get(cstringToVec8("sym"));
                    delete val->Get(cstringToVec8("type"));
                    delete val->Get(cstringToVec8("copy"));
                    delete val->Get(cstringToVec8("copy"));
                    delete val;
                }));
                return val;
            });
        }, [](){
            sym_m.lock();
            mpf_clear(symnum);
            for (auto i : symdescs) {
                mpf_clear(i.first);
                delete i.first;
            }
            sym_m.unlock();
            gc::gc_m.lock();
            std::vector<void*> all_c(gc::all.begin(), gc::all.end());
            gc::gc_m.unlock();
            for (auto i : all_c) {
                sol::Value r;
                r._ = i;
                r.Collect();
            }
        });
        im->AddInitNow([](){
            std::ios_base::sync_with_stdio();
        }, Noop);
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

sol::BaseString sol::utf8ToString(vec8 val) {
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
            if (i >= val.size()) break;
            curr = val[i];
            if ((((curr & 0b1000000) >> 7) == 0) || (((curr & 0b01000000) >> 6) == 1)) {
                continue;
            }
            uint8_t part2 = (curr & 0b00111111);
            codepoints.push_back((part1 << 6) + part2);
            i++;
            continue;
        }
        if (((curr & 0b00010000) >> 4) == 0) {
            uint8_t part1 = (curr & 0b00001111);
            i++;
            if (i >= val.size()) break;
            curr = val[i];
            if ((((curr & 0b10000000) >> 7) == 0) || (((curr & 0b01000000) >> 6) == 1)) {
                continue;
            }
            uint8_t part2 = (curr & 0b00111111);
            i++;
            if (i >= val.size()) break;
            curr = val[i];
            if ((((curr & 0b10000000) >> 7) == 0) || (((curr & 0b01000000) >> 6) == 1)) {
                continue;
            }
            uint8_t part3 = (curr & 0b00111111);
            codepoints.push_back((part1 << 12) + (part2 << 6) + part3);
            i++;
            continue;
        }
        if (((curr & 0b00001000) >> 3) == 0) {
            uint8_t part1 = (curr & 0b00000111);
            i++;
            if (i >= val.size()) break;
            curr = val[i];
            if ((((curr & 0b10000000) >> 7) == 0) || (((curr & 0b01000000) >> 6) == 1)) {
                continue;
            }
            uint8_t part2 = (curr & 0b00111111);
            i++;
            if (i >= val.size()) break;
            curr = val[i];
            if ((((curr & 0b10000000) >> 7) == 0) || (((curr & 0b01000000) >> 6) == 1)) {
                continue;
            }
            uint8_t part3 = (curr & 0b00111111);
            i++;
            if (i >= val.size()) break;
            curr = val[i];
            if ((((curr & 0b10000000) >> 7) == 0) || (((curr & 0b01000000) >> 6) == 1)) {
                continue;
            }
            uint8_t part4 = (curr & 0b00111111);
            codepoints.push_back((part1 << 18) + (part2 << 12) + (part3 << 6) + part4);
            i++;
            continue;
        }
        i++;
    }
    BaseString result;
    for (auto i : codepoints) {
        if (i >= 0xD800 && i <= 0xDFFF) {
            result.chars.push_back(i);
            result.litchars.push_back(result.chars.size() - 1);
            continue;
        }
        if (i >= 0x10000) {
            uint32_t x = i - 0x10000;
            uint16_t h = floor(x / 0x400) + 0xD800;
            uint16_t l = (x % 0x400) + 0xDC00;
            result.chars.push_back(h);
            result.chars.push_back(l);
            continue;
        }
        result.chars.push_back(i);
    }
    return result;
}

sol::vec8 sol::stringToUtf8(BaseString val) {
    std::vector<uint32_t> codepoints;
    std::size_t i = 0;
    std::function<bool(std::size_t)> includes = [&val](std::size_t index){
        for (auto i : val.litchars) {
            if (i == index) return true;
        }
        return false;
    };
    while (i < val.chars.size()) {
        uint16_t curr = val.chars[i];
        if (curr >= 0xD800 && curr <= 0xDFFF) {
            if (includes(i)) {
                codepoints.push_back(curr);
                i++;
                continue;
            }
            if (!(curr >= 0xD800 && curr <= 0xDBFF)) {
                i++;
                continue;
            }
            uint16_t h = curr;
            i++;
            if (i >= val.chars.size()) break;
            curr = val.chars[i];
            if (!(curr >= 0xDC00 && curr <= 0xDFFF)) {
                continue;
            }
            uint16_t l = curr;
            codepoints.push_back(((h - 0xD800) * 0x400) + (l - 0xDC00) + 0x10000);
        }
        codepoints.push_back(curr);
        i++;
    }
    vec8 result;
    for (auto i : codepoints) {
        if (i <= 0x7F) {
            result.push_back(i);
            continue;
        }
        if (i <= 0x7FF) {
            result.push_back((i >> 6) + 0b11000000);
            result.push_back((i & 0b00000111111) + 0b10000000);
            continue;
        }
        if (i <= 0xFFFF) {
            result.push_back((i >> 12) + 0b11100000);
            result.push_back(((i >> 6) & 0b0000111111) + 0b10000000);
            result.push_back((i & 0b0000000000111111) + 0b10000000);
            continue;
        }
        result.push_back((i >> 18) + 0b11110000);
        result.push_back(((i >> 12) & 0b000111111) + 0b10000000);
        result.push_back(((i >> 6) & 0b000000000111111) + 0b10000000);
        result.push_back((i & 0b000000000000000111111) + 0b10000000);
    }
    return result;
}

sol::Value sol::Value::NewString(BaseString vall) {
    return mknew([vall](){
        BaseValue* val = new BaseValue;
        vec8* type = new vec8(cstringToVec8("string"));
        BaseString* bstr = new BaseString;
        *bstr = vall;
        val->Set(cstringToVec8("type"), type);
        val->Set(cstringToVec8("str"), bstr);
        val->Set(cstringToVec8("copy"), new std::function([val](){
            return sol::Value::NewString(*((BaseString*)(val->Get(cstringToVec8("str")))));
        }));
        val->Set(cstringToVec8("collect"), mkcollect(val, [val](){
            delete val->Get(cstringToVec8("type"));
            delete val->Get(cstringToVec8("str"));
            delete val->Get(cstringToVec8("copy"));
            delete val->Get(cstringToVec8("collect"));
            delete val;
        }));
        return val;
    })();
}

sol::Value sol::Value::NewSymbol() {
    sym_m.lock();
    mpf_ptr sym = new mpf_t;
    mpf_init_set(sym, symnum);
    mpf_add_ui(symnum, symnum, 1);
    sym_m.unlock();
    auto val = mksym();
    val.CoreSet(cstringToVec8("sym"), sym);
    return val;
}

sol::Value sol::Value::NewSymbolWithDescription(BaseString desc) {
    auto val = sol::Value::NewSymbol();
    mpf_ptr sym = (mpf_ptr)val.CoreGet(cstringToVec8("sym"));
    sym_m.lock();
    for (std::size_t i = 0; i < symdescs.size(); i++) {
        auto vall = symdescs[i];
        if (mpf_cmp(vall.first, sym) == 0) {
            vall.second = desc;
            symdescs[i] = vall;
            SOL_MUNLOCKRET(sym_m, val)
        }
    }
    mpf_ptr s = new mpf_t;
    mpf_init_set(s, sym);
    symdescs.emplace_back(s, desc);
    SOL_MUNLOCKRET(sym_m, val)
}

template<typename T>
sol::Maybe<T> sol::Maybe<T>::FromNoError(T v) {
    Maybe<T> res;
    res.err = ErrorNoError;
    res.val = v;
    return res;
}

template<typename T>
sol::Maybe<T> sol::Maybe<T>::FromError(Error e) {
    Maybe<T> res;
    res.err = e;
    return res;
}

template<typename T>
bool sol::Maybe<T>::IsError() {
    return err != ErrorNoError;
}

template<typename T>
T sol::Maybe<T>::ToNoError() {
    return val;
}

bool sol::Value::IsPersistent() {
    gc::gc_m.lock();
    for (auto i : gc::persistent) {
        if (i == _) {
            SOL_MUNLOCKRET(gc::gc_m, true)
        }
    }
    SOL_MUNLOCKRET(gc::gc_m, false)
}

bool sol::Value::IsUndefined() {
    vec8* type = (vec8*)CoreGet(cstringToVec8("type"));
    return vec8Compare(*type, cstringToVec8("undefined"));
}

bool sol::Value::IsNull() {
    vec8* type = (vec8*)CoreGet(cstringToVec8("type"));
    return vec8Compare(*type, cstringToVec8("null"));
}

bool sol::Value::IsString() {
    vec8* type = (vec8*)CoreGet(cstringToVec8("type"));
    return vec8Compare(*type, cstringToVec8("string"));
}

bool sol::Value::IsSymbol() {
    vec8* type = (vec8*)CoreGet(cstringToVec8("type"));
    return vec8Compare(*type, cstringToVec8("symbol"));
}

sol::Maybe<sol::BaseString> sol::Value::StringGetValue() {
    if (!IsString()) return Maybe<BaseString>::FromError(ErrorWrongType);
    return Maybe<BaseString>::FromNoError(*((BaseString*)(CoreGet(cstringToVec8("str")))));
}

sol::Maybe<sol::NullType> sol::Value::StringSetValue(BaseString val) {
    if (!IsString()) return Maybe<NullType>::FromError(ErrorWrongType);
    *((BaseString*)(CoreGet(cstringToVec8("str")))) = val;
    NullType res;
    return Maybe<NullType>::FromNoError(res);
}

template<typename T>
sol::Error sol::Maybe<T>::GetError() {
    return err;
}

sol::Maybe<sol::vec8> sol::Value::StringGetUtf8Value() {
    Maybe<BaseString> r = StringGetValue();
    if (r.IsError()) return Maybe<vec8>::FromError(r.GetError());
    return Maybe<vec8>::FromNoError(stringToUtf8(r.ToNoError()));
}

sol::Maybe<sol::NullType> sol::Value::StringSetUtf8Value(vec8 val) {
    BaseString r = utf8ToString(val);
    return StringSetValue(r);
}

sol::Maybe<bool> sol::Value::SymbolHasDescription() {
    if (!IsSymbol()) return Maybe<bool>::FromError(ErrorWrongType);
    sym_m.lock();
    mpf_ptr sym = (mpf_ptr)CoreGet(cstringToVec8("sym"));
    for (auto i : symdescs) {
        if (mpf_cmp(i.first, sym) == 0) {
            Maybe<bool> res = Maybe<bool>::FromNoError(true);
            SOL_MUNLOCKRET(sym_m, res)
        }
    }
    Maybe<bool> res = Maybe<bool>::FromNoError(false);
    SOL_MUNLOCKRET(sym_m, res)
}

sol::Maybe<sol::BaseString> sol::Value::SymbolGetDescription() {
    if (!IsSymbol()) return Maybe<BaseString>::FromError(ErrorWrongType);
    sym_m.lock();
    mpf_ptr sym = (mpf_ptr)CoreGet(cstringToVec8("sym"));
    for (auto i : symdescs) {
        if (mpf_cmp(i.first, sym) == 0) {
            Maybe<BaseString> res = Maybe<BaseString>::FromNoError(i.second);
            SOL_MUNLOCKRET(sym_m, res)
        }
    }
    Maybe<BaseString> res = Maybe<BaseString>::FromError(ErrorNotFound);
    SOL_MUNLOCKRET(sym_m, res)
}

sol::Maybe<sol::NullType> sol::Value::SymbolSetDescription(BaseString desc) {
    if (!IsSymbol()) return Maybe<NullType>::FromError(ErrorWrongType);
    sym_m.lock();
    mpf_ptr sym = (mpf_ptr)CoreGet(cstringToVec8("sym"));
    for (std::size_t i = 0; i < symdescs.size(); i++) {
        auto val = symdescs[i];
        if (mpf_cmp(val.first, sym) == 0) {
            val.second = desc;
            symdescs[i] = val;
            NullType n;
            Maybe<NullType> res = Maybe<NullType>::FromNoError(n);
            SOL_MUNLOCKRET(sym_m, res)
        }
    }
    mpf_ptr s = new mpf_t;
    mpf_init_set(s, sym);
    symdescs.emplace_back(s, desc);
    Maybe<NullType> res = Maybe<NullType>::FromNoError(NullType());
    SOL_MUNLOCKRET(sym_m, res)
}