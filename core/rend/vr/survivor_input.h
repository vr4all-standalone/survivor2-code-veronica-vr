// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <charconv>
#include <cstring>
#include <chrono>
#include <mutex>

namespace vr::survivor {
enum Button : uint32_t { Trigger=1, Action=2, Enter=4, Coin=8, Up=16, Down=32, Service=64, Test=128 };
struct Input { int16_t yaw=0,pitch=0,roll=0; uint32_t buttons=0; bool owned=false; };
inline bool parse(const char* line, Input& result) {
    if(!line||std::strncmp(line,"SVR ",4)!=0)return false;
    const char* p=line+4;const char* end=line+std::strlen(line);
    auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    int values[4];
    for(int i=0;i<4;++i){
        while(p!=end&&space(*p))++p;
        const auto parsed=std::from_chars(p,end,values[i]);
        if(parsed.ec!=std::errc()||parsed.ptr==p)return false;
        p=parsed.ptr;
        if(p!=end&&!space(*p))return false;
    }
    while(p!=end&&space(*p))++p;
    if(p!=end)return false;
    const int yaw=values[0],pitch=values[1],roll=values[2],buttons=values[3];
    if(yaw < -32768||yaw > 32767||pitch < -32768||pitch > 32767||roll < -32768||roll > 32767||buttons<0||buttons>255) return false;
    result={static_cast<int16_t>(yaw),static_cast<int16_t>(pitch),static_cast<int16_t>(roll),static_cast<uint32_t>(buttons),true};
    return true;
}
inline uint16_t axis(int16_t value,bool inverted) {
    const uint16_t adc=static_cast<uint16_t>(static_cast<int>(value)+32768);
    return inverted?static_cast<uint16_t>(65535-adc):adc;
}
inline int16_t normalize(float value) {
    if(!std::isfinite(value)||std::abs(value)<=.15f)return 0;
    value=std::clamp(value,-1.f,1.f);
    const float magnitude=(std::abs(value)-.15f)/.85f;
    return static_cast<int16_t>(std::lround(magnitude*(value<0?-32768.f:32767.f)));
}
inline int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
class Mailbox {
    std::mutex mutex;
    Input input;
    int64_t updated=0;
public:
    void publish(Input value,int64_t now) {std::lock_guard<std::mutex> lock(mutex);input=value;input.owned=true;updated=now;}
    Input read(int64_t now) {
        std::lock_guard<std::mutex> lock(mutex);
        if(input.owned&&(now<updated||now-updated>250))return {0,0,0,0,true};
        return input;
    }
    void release(int64_t now) {publish({},now);}
    void reset() {std::lock_guard<std::mutex> lock(mutex);input={};updated=0;}
};
inline Mailbox cabinet;
}
