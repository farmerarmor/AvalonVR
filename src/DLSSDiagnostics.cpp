#include "DLSSDiagnostics.h"
#include "Il2Cpp.h"
#include <MinHook.h>
#include <atomic>

namespace {
// Layouts are checked against exact-build metadata before any hooks are installed.
struct Init {uint32_t inW,inH,outW,outH,quality,flags,slot;};
struct Exec {int32_t reset;float sharpness,mvX,mvY,jitterX,jitterY,exposure;uint32_t x,y,w,h,invertX,invertY,slot;};
static_assert(sizeof(Init)==28&&sizeof(Exec)==56);
void*(*createFeature)(void*,void*,Init*,void*);
void(*destroyFeature)(void*,void*,void*,void*);
void(*executeDLSS)(void*,void*,void*,void*,void*);
void*(*getInitPtr)(void*,void*);void* initMethod{};
void*(*getExecPtr)(void*,void*);void* execMethod{};
std::atomic<uint64_t> creates{},destroys{},executes{},resets{},nextReport{};
std::atomic<int> reportRemaining{};
bool Layout(const char* name,const char* const* fields,size_t count,size_t expectedSize){
    auto cls=il::Class("UnityEngine.NVIDIA",name);auto module=GetModuleHandleW(L"GameAssembly.dll");
    auto size=(int(*)(void*,uint32_t*))GetProcAddress(module,"il2cpp_class_value_size");uint32_t alignment{};
    if(!cls||!size||size(cls,&alignment)!=expectedSize)return false;
    for(size_t i=0;i<count;++i){auto field=il::class_get_field_from_name(cls,fields[i]);if(!field||il::field_get_offset(field)!=sizeof(il::Object)+4*i)return false;}
    return true;
}
void* Create(void* self,void* cmd,Init* data,void* method){
    auto result=createFeature(self,cmd,data,method);auto count=++creates;
    if(data&&count<=8)Log("DLSS native create=%llu context=%p slot=%u input=%ux%u output=%ux%u quality=%u flags=%x",count,result,data->slot,data->inW,data->inH,data->outW,data->outH,data->quality,data->flags);
    return result;
}
void Destroy(void* self,void* cmd,void* context,void* method){++destroys;destroyFeature(self,cmd,context,method);}
void Execute(void* self,void* cmd,void* context,void* textures,void* method){
    ++executes;
    auto data=context?(Exec*)getExecPtr(context,execMethod):nullptr;if(data&&data->reset)++resets;
    auto now=GetTickCount64(),deadline=nextReport.load();
    if(now>=deadline&&nextReport.compare_exchange_strong(deadline,now+5000))reportRemaining=2;
    if(data&&reportRemaining.load()>0&&reportRemaining.fetch_sub(1)>0){
        auto init=(Init*)getInitPtr(context,initMethod);
        if(init)Log("DLSS native context=%p slot=%u create=%llu destroy=%llu execute=%llu resets=%llu init=%ux%u->%ux%u quality=%u flags=%x subrect=%ux%u reset=%d jitter=(%.4f,%.4f) mv=(%.1f,%.1f) exposure=%.4f",context,data->slot,creates.load(),destroys.load(),executes.load(),resets.load(),init->inW,init->inH,init->outW,init->outH,init->quality,init->flags,data->w,data->h,data->reset,data->jitterX,data->jitterY,data->mvX,data->mvY,data->exposure);
    }
    executeDLSS(self,cmd,context,textures,method);
}
bool Hook(void* method,void* replacement,void** original){
    if(!method)return false;auto address=((il::Method*)method)->pointer;
    auto result=MH_CreateHook(address,replacement,original);if(result!=MH_OK)return false;
    return MH_EnableHook(address)==MH_OK;
}
}
void dlssdiag::Install(){
    const char* initFields[]={"m_InputRTWidth","m_InputRTHeight","m_OutputRTWidth","m_OutputRTHeight","m_Quality","m_Flags","m_FeatureSlot"};
    const char* execFields[]={"m_Reset","m_Sharpness","m_MVScaleX","m_MVScaleY","m_JitterOffsetX","m_JitterOffsetY","m_PreExposure","m_SubrectOffsetX","m_SubrectOffsetY","m_SubrectWidth","m_SubrectHeight","m_InvertXAxis","m_InvertYAxis","m_FeatureSlot"};
    if(!Layout("DLSSCommandInitializationData",initFields,7,sizeof(Init))||!Layout("DLSSCommandExecutionData",execFields,14,sizeof(Exec))){Log("DLSS native diagnostics disabled: layout mismatch");return;}
    initMethod=il::Find("UnityEngine.NVIDIA","DLSSContext","GetInitCmdPtr",0);execMethod=il::Find("UnityEngine.NVIDIA","DLSSContext","GetExecuteCmdPtr",0);
    if(!initMethod||!execMethod){Log("DLSS native diagnostics disabled: pointer getter missing");return;}
    getInitPtr=(decltype(getInitPtr))((il::Method*)initMethod)->pointer;getExecPtr=(decltype(getExecPtr))((il::Method*)execMethod)->pointer;
    auto cls=il::Class("UnityEngine.NVIDIA","GraphicsDevice");
    bool a=Hook(il::Find(cls,"CreateFeature",2),(void*)&Create,(void**)&createFeature);
    bool b=Hook(il::Find(cls,"DestroyFeature",2),(void*)&Destroy,(void**)&destroyFeature);
    bool c=Hook(il::Find(cls,"ExecuteDLSS",3),(void*)&Execute,(void**)&executeDLSS);
    Log("DLSS native sparse diagnostics hooks create=%d destroy=%d execute=%d",a,b,c);
}
