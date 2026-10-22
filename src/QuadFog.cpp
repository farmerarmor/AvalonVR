#include "QuadFog.h"
#include "QuadFogMath.h"
#include "Il2Cpp.h"
#include <MinHook.h>
#include <algorithm>
namespace quadfog {namespace {
bool installed{},requested{},active{};DWORD owner{};void* cameras[2]{};float density=1;
unsigned generation=1,seenGeneration[2]{};void* seenCamera[2]{};
uint64_t applied[2]{},declined{};Grid lastNative[2]{},lastMatched[2]{};float lastVoxel[2]{};
Grid*(*original)(Grid*,void*,float*,void*);
Grid* Viewport(Grid* result,void* hd,float* voxel,void* method){
 auto value=original(result,hd,voxel,method);
 if(!hd||owner!=GetCurrentThreadId()||!value)return value;
 auto camera=*(void**)((uint8_t*)hd+56);int view=camera==cameras[0]?0:camera==cameras[1]?1:-1;if(view<0)return value;
 if(seenGeneration[view]!=generation||seenCamera[view]!=hd){*(bool*)((uint8_t*)hd+1297)=false;*(int*)((uint8_t*)hd+1300)=0;seenGeneration[view]=generation;seenCamera[view]=hd;}
 if(!active||!voxel)return value;
 auto corrected=MatchGrid(*value,*(int*)((uint8_t*)hd+1316),*(int*)((uint8_t*)hd+1320),*voxel,density);
 if(!corrected.valid){++declined;return value;}
 lastNative[view]=*value;lastMatched[view]=corrected.grid;lastVoxel[view]=corrected.voxel;
 *value=corrected.grid;*voxel=corrected.voxel;++applied[view];return value;
}
bool Field(void* cls,const char* name,unsigned offset,const char* type){auto field=cls?il::class_get_field_from_name(cls,name):nullptr;auto ft=(void*(*)(void*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_field_get_type");return field&&ft&&il::field_get_offset(field)==offset&&!strcmp(il::type_get_name(ft(field)),type);}
}
bool Install(){
 requested=GetPrivateProfileIntW(L"QuadViews",L"MatchFogSampling",0,(modulePath.parent_path()/L"AvalonVR.ini").c_str())!=0;
 auto module=GetModuleHandleW(L"GameAssembly.dll");auto hd=il::Class("UnityEngine.Rendering.HighDefinition","HDCamera"),pipeline=il::Class("UnityEngine.Rendering.HighDefinition","HDRenderPipeline");
 auto m=il::Find(pipeline,"ComputeVolumetricViewportSize",2);auto rt=(void*(*)(void*))GetProcAddress(module,"il2cpp_method_get_return_type");auto size=(int(*)(void*,uint32_t*))GetProcAddress(module,"il2cpp_class_value_size");uint32_t align{};auto vector=il::Class("UnityEngine","Vector3Int");
 bool valid=m&&rt&&size&&vector&&size(vector,&align)==12&&Field(hd,"camera",56,"UnityEngine.Camera")&&Field(hd,"<actualWidth>k__BackingField",1316,"System.Int32")&&Field(hd,"<actualHeight>k__BackingField",1320,"System.Int32")&&Field(hd,"volumetricHistoryIsValid",1297,"System.Boolean")&&Field(hd,"volumetricValidFrames",1300,"System.Int32")&&
  !strcmp(il::type_get_name(rt(m)),"UnityEngine.Vector3Int")&&!strcmp(il::type_get_name(il::method_get_param(m,0)),"UnityEngine.Rendering.HighDefinition.HDCamera")&&!strcmp(il::type_get_name(il::method_get_param(m,1)),"System.Single&");
 auto target=m?((il::Method*)m)->pointer:nullptr;const uint8_t prefix[]={0x40,0x55,0x57,0x41,0x56,0x48,0x83,0xec,0x40};valid=valid&&((uintptr_t)target-(uintptr_t)module==0x806e8e0)&&!memcmp(target,prefix,sizeof(prefix));
 if(valid){auto c=MH_CreateHook(target,(void*)&Viewport,(void**)&original),e=c==MH_OK?MH_EnableHook(target):MH_ERROR_NOT_CREATED;installed=c==MH_OK&&e==MH_OK;Log("Quad fog sampling hook=%d/%d requested=%d; only peripheral volumetric grid",c,e,requested);}
 else Log("Quad fog sampling exact-build validation refused");return installed;
}
bool Enabled(){return installed&&requested;}
bool Toggle(){requested=!requested;++generation;Log("Quad fog sampling requested=%d available=%d (Ctrl+Shift+F8)",requested,installed);return Enabled();}
void Begin(void* const* views,unsigned count,bool quad,float scale){owner=GetCurrentThreadId();bool valid=quad&&count==4;for(unsigned i=0;i<2;++i)cameras[i]=valid&&views?views[i]:nullptr;density=scale;active=valid&&Enabled();}
void End(){active=false;owner=0;cameras[0]=cameras[1]=nullptr;}
void Report(){Log("Quad fog sampling requested=%d available=%d applied=%llu/%llu declined=%llu density=%.4f",requested,installed,applied[0],applied[1],declined,density);for(unsigned i=0;i<2;++i)if(applied[i])Log("Quad fog view=%u native=%dx%dx%d matched=%dx%dx%d voxel=%.5f",i,lastNative[i].x,lastNative[i].y,lastNative[i].z,lastMatched[i].x,lastMatched[i].y,lastMatched[i].z,lastVoxel[i]);applied[0]=applied[1]=declined=0;}
}
