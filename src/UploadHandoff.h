#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
namespace upload {
struct Pending {void* owner{};ID3D11Buffer* gpu{};ID3D11Buffer* staging{};void* base{};uint64_t offset{},capacity{};bool accepted{};};
// BeginWriteThreadSafe may run on a writing/job thread; EndWrite runs on the
// graphics worker. No thread-local assumption and no target lifetime extension.
class Handoff {
 SRWLOCK lock=SRWLOCK_INIT;std::array<Pending,128> slots{};
public:
 bool Store(const Pending& value){AcquireSRWLockExclusive(&lock);Pending* target{};for(auto& p:slots)if(p.owner==value.owner){target=&p;break;}if(!target)for(auto& p:slots)if(!p.owner){target=&p;break;}if(target)*target=value;ReleaseSRWLockExclusive(&lock);return target!=nullptr;}
 Pending Take(void* owner){Pending result{};AcquireSRWLockExclusive(&lock);for(auto& p:slots)if(p.owner==owner){result=p;p={};break;}ReleaseSRWLockExclusive(&lock);return result;}
};
}
