#include "ShadowCulling.h"
#include "ShadowCullingData.h"
#include "RadialShadowMath.h"
#include "ShadowResolution.h"
#include "Il2Cpp.h"
#include <MinHook.h>
#include <array>
namespace shadowcull {namespace {
using GetParameters=bool(*)(void*,bool,void*,int,void*);
GetParameters original{};void* getParameters{};void* eyes[4]{};unsigned eyeCount=2;
std::array<uint8_t,ParametersSize> common{};
bool active{},installed{};uint64_t reportAt{};unsigned patched[4]{};
using NativeCull=void(*)(void*,void*,void*);
using Directional=bool(*)(void*,int,int,int,const Vec3*,int,float,Matrix*,Matrix*,radialshadow::Split*);
NativeCull nativeCull{};Directional directional{};
struct CullRecord{void* pointer{};float distance{};};CullRecord records[4];int cameraIDs[4]{},lastEye{};
Vec3 radialCenter{},observedLightZ{};bool radialHooks{},radialRequested{},radialReady{},radialFrameReady{};int observedResolution{},resolutionFloor=1024;DWORD ownerThread{};unsigned radialApplied[4]{};
unsigned frameMasks[4]{},expectedMask{};
struct Lod {int ortho{};Vec3 position{};float fov{},orthoSize{};int pixelHeight{};};static_assert(sizeof(Lod)==28);
bool lodLayout{};Lod observedLOD[4]{},getLOD[4]{};unsigned lodSamples[4]{},getLODSamples[4]{};
void Cull(void* parameters,void* context,void* results){
 int eye=-1;if(active&&GetCurrentThreadId()==ownerThread&&parameters&&results){int id{};memcpy(&id,(char*)parameters+1416,4);for(unsigned e=0;e<eyeCount;++e)if(cameraIDs[e]&&id==cameraIDs[e])eye=e;}
 float distance{};bool changed=false;std::array<uint8_t,108> saved{};
 if(eye>=0){if(lodLayout&&eyeCount==4){memcpy(&observedLOD[eye],parameters,sizeof(Lod));++lodSamples[eye];}memcpy(&distance,(char*)parameters+424,4);if(radialFrameReady){memcpy(saved.data(),(char*)parameters+1068,96);memcpy(saved.data()+96,(char*)parameters+1264,12);changed=radialshadow::ReceiverBounds(parameters,ParametersSize,radialCenter,distance);}}
 nativeCull(parameters,context,results);
 if(eye>=0){records[eye]={*(void**)results,distance};lastEye=eye;if(changed){memcpy((char*)parameters+1068,saved.data(),96);memcpy((char*)parameters+1264,saved.data()+96,12);}}
}
bool Compute(void* culling,int light,int cascade,int count,const Vec3* ratios,int resolution,float nearOffset,Matrix* view,Matrix* projection,radialshadow::Split* split){
 bool result=directional(culling,light,cascade,count,ratios,resolution,nearOffset,view,projection,split);
 if(!result||!active||GetCurrentThreadId()!=ownerThread||!culling||!view||!projection||!split||!ratios)return result;
 int eye=-1;if(records[lastEye].pointer==culling)eye=lastEye;else for(unsigned e=0;e<eyeCount;++e)if(records[e].pointer==culling){eye=e;break;}
 if(eye<0||light!=0)return result;
 observedResolution=resolution;observedLightZ=radialshadow::Row(*view,2);
 if(radialFrameReady&&resolution>=resolutionFloor&&radialshadow::Fit(*view,*projection,*split,radialCenter,records[eye].distance,*ratios,cascade,count,resolution)){++radialApplied[eye];frameMasks[eye]|=1u<<cascade;expectedMask=(1u<<count)-1;}
 return result;
}
bool Get(void* camera,bool stereo,void* parameters,int bytes,void* method){
    bool result=original(camera,stereo,parameters,bytes,method);
    if(result&&active&&bytes==ParametersSize&&!stereo)for(unsigned eye=0;eye<eyeCount;++eye)if(camera==eyes[eye]){
        if(lodLayout&&eyeCount==4){memcpy(&getLOD[eye],parameters,sizeof(Lod));++getLODSamples[eye];}
        if(CopyProperties(parameters,bytes,common.data(),common.size()))++patched[eye];break;
    }
    return result;
}
bool Offset(void* cls,const char* field,size_t expected){auto f=cls?il::class_get_field_from_name(cls,field):nullptr;return f&&il::field_get_offset(f)==expected+sizeof(il::Object);}
bool NativeTarget(void* target){MEMORY_BASIC_INFORMATION region{};return target&&VirtualQuery(target,&region,sizeof(region))&&region.AllocationBase==GetModuleHandleW(L"UnityPlayer.dll");}
bool InstallRadial(){
 auto split=il::Class("UnityEngine.Rendering","ShadowSplitData"),results=il::Class("UnityEngine.Rendering","CullingResults"),params=il::Class("UnityEngine.Rendering","ScriptableCullingParameters"),props=il::Class("UnityEngine.Rendering","CameraProperties");
 auto size=(int(*)(void*,uint32_t*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_class_value_size");uint32_t align{};
 if(!split||!results||!size||size(split,&align)!=sizeof(radialshadow::Split)||size(results,&align)!=16||!Offset(results,"ptr",0)||!Offset(params,"m_ShadowDistance",424)||!Offset(split,"m_CullingPlaneCount",0)||!Offset(split,"m_CullingPlanes",4)||!Offset(split,"m_CullingSphere",164)||!Offset(split,"m_ShadowCascadeBlendCullingFactor",180)||!Offset(split,"m_CullingNearPlane",184)||!Offset(split,"m_CullingMatrix",188)||!Offset(props,"m_ShadowCullPlanes",628)||!Offset(props,"shadowCullCenter",824)||!Offset(props,"coreCameraValues",968))return false;
 auto method=il::Find("UnityEngine.Rendering","CullingResults","ComputeDirectionalShadowMatricesAndCullingPrimitives_Injected",10);
 const char* types[]={"System.IntPtr","System.Int32","System.Int32","System.Int32","UnityEngine.Vector3&","System.Int32","System.Single","UnityEngine.Matrix4x4&","UnityEngine.Matrix4x4&","UnityEngine.Rendering.ShadowSplitData&"};
 if(!method)return false;for(unsigned i=0;i<10;++i)if(strcmp(il::type_get_name(il::method_get_param(method,i)),types[i]))return false;
 auto cull=il::resolve_icall("UnityEngine.Rendering.ScriptableRenderContext::Internal_Cull_Injected");auto compute=il::resolve_icall("UnityEngine.Rendering.CullingResults::ComputeDirectionalShadowMatricesAndCullingPrimitives_Injected");if(!NativeTarget(cull)||!NativeTarget(compute))return false;
 auto a=MH_CreateHook(cull,&Cull,(void**)&nativeCull);auto b=MH_CreateHook(compute,&Compute,(void**)&directional);
 auto c=a==MH_OK?MH_EnableHook(cull):MH_ERROR_NOT_CREATED;auto d=b==MH_OK?MH_EnableHook(compute):MH_ERROR_NOT_CREATED;
 bool ready=a==MH_OK&&b==MH_OK&&c==MH_OK&&d==MH_OK;Log("Radial shadow native hooks cull=%d/%d matrix=%d/%d guardedReady=%d",a,c,b,d,ready);return ready;
}
}
bool Install(){
    if(installed)return true;
    auto params=il::Class("UnityEngine.Rendering","ScriptableCullingParameters"),props=il::Class("UnityEngine.Rendering","CameraProperties");
    auto size=(int(*)(void*,uint32_t*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_class_value_size");uint32_t align{};
    if(!params||!props||!size||size(params,&align)!=ParametersSize||size(props,&align)!=PropertiesSize||
       !Offset(params,"m_CameraProperties",PropertiesOffset)||!Offset(props,"rendererCount",624)||!Offset(props,"coreCameraValues",968)||!Offset(props,"cameraType",980)||!Offset(props,"useInteractiveLightBakingData",992)){
        Log("Shared shadow culling unsupported value layout; native parameters retained");return false;
    }
    auto lod=il::Class("UnityEngine.Rendering","LODParameters");
    lodLayout=lod&&size(lod,&align)==sizeof(Lod)&&Offset(params,"m_LODParameters",0)&&Offset(lod,"m_IsOrthographic",0)&&Offset(lod,"m_CameraPosition",4)&&Offset(lod,"m_FieldOfView",16)&&Offset(lod,"m_OrthoSize",20)&&Offset(lod,"m_CameraPixelHeight",24);
    Log("Quad LOD read-only layout validated=%d",lodLayout);
    getParameters=il::Find("UnityEngine","Camera","GetCullingParameters_Internal",4);
    if(!getParameters)return false;
    const char* expected[]={"UnityEngine.Camera","System.Boolean","UnityEngine.Rendering.ScriptableCullingParameters&","System.Int32"};
    for(unsigned i=0;i<4;++i)if(strcmp(il::type_get_name(il::method_get_param(getParameters,i)),expected[i]))return false;
    auto target=((il::Method*)getParameters)->pointer;auto c=MH_CreateHook(target,&Get,(void**)&original);auto e=c==MH_OK?MH_EnableHook(target):MH_ERROR_NOT_CREATED;
    installed=c==MH_OK&&e==MH_OK;Log("Shared shadow culling hook=%d/%d layout=%zu/%zu",c,e,ParametersSize,PropertiesSize);if(installed)radialHooks=InstallRadial();return installed;
}
void ConfigureRadial(Vec3 center,bool enabled,int floor){radialCenter=center;radialRequested=enabled;resolutionFloor=std::clamp(floor,64,8192);radialReady=shadowres::Update(enabled&&radialHooks,observedResolution,resolutionFloor,observedLightZ);}
void StopRadial(){radialRequested=radialReady=radialFrameReady=false;observedResolution=0;shadowres::Update(false,0,1024,{});}
bool RadialActive(){if(!radialFrameReady||!expectedMask)return false;for(unsigned e=0;e<eyeCount;++e)if(frameMasks[e]!=expectedMask)return false;return true;}
bool Prepare(void* commonCamera,void* leftCamera,void* rightCamera,bool enabled){
    void* cameras[]={leftCamera,rightCamera};return PrepareViews(commonCamera,cameras,2,enabled);
}
bool PrepareViews(void* commonCamera,void* const* cameras,unsigned count,bool enabled){
    active=false;if(!cameras||(count!=2&&count!=4))return false;eyeCount=count;bool valid=true;
    for(unsigned e=0;e<4;++e){void* next=e<count?cameras[e]:nullptr;if(eyes[e]!=next)cameraIDs[e]=next?il::Value<int>(il::Call(next,"GetInstanceID")):0;eyes[e]=next;records[e]={};frameMasks[e]=0;if(e<count)valid&=next!=nullptr;}
    ownerThread=GetCurrentThreadId();expectedMask=0;lastEye=0;
    radialFrameReady=radialReady&&observedResolution>=resolutionFloor;
    if(installed&&enabled&&commonCamera&&valid)active=original(commonCamera,false,common.data(),int(common.size()),getParameters);
    auto now=GetTickCount64();if(now>=reportAt){Log("Shared shadow properties requested=%d prepared=%d appliedLeft=%u appliedRight=%u radialRequested=%d radialReady=%d nativeResolution=%d radialLeft=%u radialRight=%u views=%u focusLeft=%u focusRight=%u",enabled,active,patched[0],patched[1],radialRequested,radialReady,observedResolution,radialApplied[0],radialApplied[1],eyeCount,patched[2],patched[3]);for(unsigned e=0;e<4;++e){patched[e]=radialApplied[e]=0;if(eyeCount==4&&(lodSamples[e]||getLODSamples[e])){auto& l=lodSamples[e]?observedLOD[e]:getLOD[e];Log("Quad native LOD view=%u stage=%s ortho=%d fov=%.5f pixelHeight=%d position=(%.4f %.4f %.4f) finalSamples=%u getSamples=%u",e,lodSamples[e]?"FinalCull":"GetParameters",l.ortho,l.fov,l.pixelHeight,l.position.x,l.position.y,l.position.z,lodSamples[e],getLODSamples[e]);}lodSamples[e]=getLODSamples[e]=0;}reportAt=now+5000;}
    return active;
}
void End(){active=false;}
}
