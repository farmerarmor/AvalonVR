#include "GameLoading.h"
#include "Il2Cpp.h"
namespace loading {namespace {void* getter{};bool failed{};}
bool Initialize(){
 auto module=GetModuleHandleW(L"GameAssembly.dll");auto cls=il::Class("Awaken.TG.Main.UI.TitleScreen.Loading","LoadingScreenUI");
 auto field=cls?il::class_get_field_from_name(cls,"<IsLoading>k__BackingField"):nullptr;auto method=cls?il::Find(cls,"get_IsLoading",0):nullptr;
 auto fieldType=(void*(*)(void*))GetProcAddress(module,"il2cpp_field_get_type");auto fieldFlags=(uint32_t(*)(void*))GetProcAddress(module,"il2cpp_field_get_flags");auto methodFlags=(uint32_t(*)(void*,uint32_t*))GetProcAddress(module,"il2cpp_method_get_flags");auto returnType=(void*(*)(void*))GetProcAddress(module,"il2cpp_method_get_return_type");uint32_t implementation{};
 if(!field||!method||!fieldType||!fieldFlags||!methodFlags||!returnType||!(fieldFlags(field)&0x10)||!(methodFlags(method,&implementation)&0x10)||il::field_get_offset(field)!=0||strcmp(il::type_get_name(fieldType(field)),"System.Boolean")||strcmp(il::type_get_name(returnType(method)),"System.Boolean")||(uintptr_t)((il::Method*)method)->pointer!=(uintptr_t)module+0x693ed40){Log("Native loading-state validation failed; guard retains original renderer");return false;}
 getter=method;Log("Native loading-state getter validated LoadingScreenUI.IsLoading RVA=693ed40");return true;
}
State Read(){
 if(!getter||failed)return {};
 void* exception{};auto boxed=il::runtime_invoke(getter,nullptr,nullptr,&exception);
 if(exception||!boxed){failed=true;Log("Native loading-state read unavailable; retaining original renderer exception=%p",exception);return {};}
 return {true,il::Value<bool>(boxed)};
}
}
