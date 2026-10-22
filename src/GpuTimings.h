#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <memory>
#include "Log.h"
#include "PerformanceModes.h"

// Sparse GPU timestamp queries. Poll never flushes or waits for the GPU, and no
// texture readback is involved. Separate buckets retain the desktop-render A/B.
class GpuTimings {
    template<class T>using Com=Microsoft::WRL::ComPtr<T>;
    struct Slot {Com<ID3D11Query> disjoint,start,scene,end,transferStart,transferEnd;uint32_t frame{},mode{},generation{};int state{};bool sceneMarked{},renderMarked{},transferMarked{},keep{};};
    struct Sample {double scene{},ui{},transfer{};};
    struct Bucket {std::array<Sample,128> samples{};uint32_t count{},next{};};
    std::array<Slot,8> slots;std::array<Bucket,perf::ModeCount> buckets;
    std::mutex mutex;bool attempted{},available{};uint32_t lastMode=UINT32_MAX,warmup{},generation{};
    bool Initialize(ID3D11Device* device){
        for(auto& s:slots){D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};if(FAILED(device->CreateQuery(&d,&s.disjoint)))return false;
            d.Query=D3D11_QUERY_TIMESTAMP;for(auto p:{std::addressof(s.start),std::addressof(s.scene),std::addressof(s.end),std::addressof(s.transferStart),std::addressof(s.transferEnd)})if(FAILED(device->CreateQuery(&d,p->ReleaseAndGetAddressOf())))return false;}
        Log("Sparse asynchronous native GPU timing enabled; no-flush polling");return true;
    }
public:
    void ResetSamples(){std::lock_guard lock(mutex);++generation;lastMode=UINT32_MAX;warmup=0;for(auto& bucket:buckets)bucket={};}
    void Begin(ID3D11Device* device,ID3D11DeviceContext* ctx,uint32_t frame,uint32_t mode,bool focused){
        if(!device||!ctx||!focused||mode>=perf::ModeCount)return;
        std::lock_guard lock(mutex);
        // Allow the camera/render-target caches to settle in each A/B mode.
        if(mode!=lastMode){lastMode=mode;warmup=0;}
        if(warmup<60){++warmup;return;}if(frame%4)return;
        if(!attempted){attempted=true;available=Initialize(device);}if(!available)return;
        // Only one disjoint interval may be active on this context. Present
        // closes it after the pair/UI transfer, including failed submissions.
        for(auto& s:slots)if(s.state==1)return;
        for(auto& s:slots)if(!s.state){s.frame=frame;s.mode=mode;s.generation=generation;s.state=1;s.sceneMarked=s.renderMarked=s.transferMarked=false;s.keep=true;ctx->Begin(s.disjoint.Get());ctx->End(s.start.Get());return;}
    }
    void SceneEnd(ID3D11DeviceContext* ctx,uint32_t frame){
        std::lock_guard lock(mutex);for(auto& s:slots)if(s.state==1&&s.frame==frame){ctx->End(s.scene.Get());s.sceneMarked=true;return;}
    }
    void End(ID3D11DeviceContext* ctx,uint32_t frame,bool keep){
        std::lock_guard lock(mutex);for(auto& s:slots)if(s.state==1&&s.frame==frame){ctx->End(s.end.Get());s.renderMarked=true;s.keep=keep;return;}
    }
    void TransferBegin(ID3D11DeviceContext* ctx,uint32_t frame){
        std::lock_guard lock(mutex);for(auto& s:slots)if(s.state==1&&s.frame==frame){ctx->End(s.transferStart.Get());s.transferMarked=true;return;}
    }
    void Finish(ID3D11DeviceContext* ctx,uint32_t frame,bool keep){
        std::lock_guard lock(mutex);for(auto& s:slots)if(s.state==1&&s.frame==frame){ctx->End(s.transferEnd.Get());ctx->End(s.disjoint.Get());s.keep=s.keep&&keep;s.state=2;return;}
    }
    void Poll(ID3D11DeviceContext* ctx){
        if(!ctx)return;std::lock_guard lock(mutex);
        for(auto& s:slots)if(s.state==2){
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};auto ready=ctx->GetData(s.disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(ready==S_FALSE)continue;if(FAILED(ready)||d.Disjoint||!d.Frequency||!s.keep||!s.sceneMarked||!s.renderMarked||!s.transferMarked||s.generation!=generation){s.state=0;continue;}
            uint64_t a{},b{},c{},e{},f{};
            if(ctx->GetData(s.start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||ctx->GetData(s.scene.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||ctx->GetData(s.end.Get(),&c,sizeof(c),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||ctx->GetData(s.transferStart.Get(),&e,sizeof(e),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||ctx->GetData(s.transferEnd.Get(),&f,sizeof(f),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)continue;
            if(b>=a&&c>=b&&e>=c&&f>=e){double scale=1000.0/d.Frequency;auto& bucket=buckets[s.mode];bucket.samples[bucket.next]={(b-a)*scale,(c-b)*scale,(f-e)*scale};bucket.next=(bucket.next+1)%bucket.samples.size();bucket.count=std::min<uint32_t>(bucket.count+1,(uint32_t)bucket.samples.size());}
            s.state=0;
        }
    }
    void Report(){
        std::lock_guard lock(mutex);
        for(uint32_t mode=0;mode<perf::ModeCount;++mode){auto& b=buckets[mode];if(b.count<8)continue;
            std::array<double,128> total{},scene{},ui{},transfer{};for(uint32_t i=0;i<b.count;++i){scene[i]=b.samples[i].scene;ui[i]=b.samples[i].ui;transfer[i]=b.samples[i].transfer;total[i]=scene[i]+ui[i];}
            std::sort(total.begin(),total.begin()+b.count);std::sort(scene.begin(),scene.begin()+b.count);std::sort(ui.begin(),ui.begin()+b.count);
            std::sort(transfer.begin(),transfer.begin()+b.count);
            Log("GPU native skipDesktop=%u preset=%s dlssAfterPost=%u samples=%u median=%.3fms p95=%.3fms HDRP=%.3fms UI=%.3fms batch=%u ffr=%u ham=%u quad=%u mipMatch=%u sharedExposure=%u fogMatch=%u",mode&1,perf::Name((mode>>1)&7),(mode>>4)&1,b.count,total[b.count/2],total[(b.count-1)*95/100],scene[b.count/2],ui[b.count/2],(mode>>5)&1,(mode>>6)&1,(mode>>7)&1,(mode>>8)&1,(mode>>9)&1,(mode>>10)&1,(mode>>11)&1);
            Log("GPU bridge skipDesktop=%u preset=%s dlssAfterPost=%u batch=%u ffr=%u ham=%u quad=%u mipMatch=%u sharedExposure=%u fogMatch=%u samples=%u median=%.3fms p95=%.3fms (eye/UI transfer span; excludes xrEndFrame)",mode&1,perf::Name((mode>>1)&7),(mode>>4)&1,(mode>>5)&1,(mode>>6)&1,(mode>>7)&1,(mode>>8)&1,(mode>>9)&1,(mode>>10)&1,(mode>>11)&1,b.count,transfer[b.count/2],transfer[(b.count-1)*95/100]);
        }
    }
};
