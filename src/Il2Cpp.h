#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
#include "Log.h"
namespace il {
struct Object { void* klass; void* monitor; };
struct Method { void* pointer; };
struct Array : Object { void* bounds; uintptr_t length; void* items[1]; };
#define API(ret,name,...) inline ret (*name)(__VA_ARGS__)
API(void*,domain_get);
API(void*,thread_attach,void*);
API(void,thread_detach,void*);
API(void**,domain_get_assemblies,void*,size_t*);
API(void*,assembly_get_image,void*);
API(const char*,image_get_name,void*);
API(void*,class_from_name,void*,const char*,const char*);
API(void*,class_get_parent,void*);
API(const char*,class_get_name,void*);
API(void*,class_get_methods,void*,void**);
API(const char*,method_get_name,void*);
API(uint32_t,method_get_param_count,void*);
API(void*,class_get_fields,void*,void**);
API(const char*,field_get_name,void*);
API(size_t,field_get_offset,void*);
API(void,field_static_get_value,void*,void*);
API(void,field_get_value,void*,void*,void*);
API(void*,class_get_method_from_name,void*,const char*,int);
API(void*,class_get_field_from_name,void*,const char*);
API(void*,runtime_invoke,void*,void*,void**,void**);
API(void*,resolve_icall,const char*);
API(void*,object_unbox,void*);
API(const char*,type_get_name,void*);
API(void*,method_get_param,void*,uint32_t);
API(void*,object_new,void*);
API(void*,class_get_type,void*);
API(void*,type_get_object,void*);
API(void*,string_new,const char*);
API(void,field_set_value,void*,void*,void*);
API(uintptr_t,gchandle_new,void*,bool);
API(void*,gchandle_get_target,uintptr_t);
API(void,gchandle_free,uintptr_t);
#undef API
inline bool Init() {
    HMODULE m=GetModuleHandleW(L"GameAssembly.dll"); if(!m)return false;
#define LOAD(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(m,"il2cpp_" #name)); if(!name){Log("Missing API %s",#name);return false;}
    LOAD(domain_get);LOAD(thread_attach);LOAD(thread_detach);LOAD(domain_get_assemblies);LOAD(assembly_get_image);LOAD(image_get_name);
    LOAD(class_from_name);LOAD(class_get_parent);LOAD(class_get_name);LOAD(class_get_methods);LOAD(method_get_name);LOAD(method_get_param_count);
    LOAD(class_get_fields);LOAD(field_get_name);LOAD(field_get_offset);LOAD(field_static_get_value);LOAD(field_get_value);
    LOAD(class_get_method_from_name);LOAD(class_get_field_from_name);LOAD(runtime_invoke);LOAD(resolve_icall);LOAD(object_unbox);
    LOAD(type_get_name);LOAD(method_get_param);LOAD(object_new);LOAD(gchandle_new);LOAD(gchandle_get_target);LOAD(gchandle_free);
    LOAD(class_get_type);LOAD(type_get_object);LOAD(string_new);LOAD(field_set_value);
#undef LOAD
    return domain_get()!=nullptr;
}
inline void* Class(const char* ns,const char* name) {
    size_t count{};void** assemblies=domain_get_assemblies(domain_get(),&count);
    for(size_t i=0;i<count;++i)if(void* c=class_from_name(assembly_get_image(assemblies[i]),ns,name))return c;
    return nullptr;
}
inline void Dump(const char* ns,const char* name){
    void* c=Class(ns,name);Log("Class %s.%s = %p",ns,name,c);
    if(!c)return;
    for(void* b=c;b;b=class_get_parent(b)){
        Log(" base %s",class_get_name(b));void* it{};
        while(void* m=class_get_methods(b,&it)){
            Log("  method %s argc=%u ptr=%p",method_get_name(m),method_get_param_count(m),((Method*)m)->pointer);
        }
        it=nullptr;
        while(void* f=class_get_fields(b,&it))Log("  field %s off=%zu",field_get_name(f),field_get_offset(f));
    }
}
inline void* Invoke(void* method,void* self=nullptr,void** args=nullptr){
    if(!method){Log("Missing method invocation");return nullptr;}
    void* exception{};void* result=runtime_invoke(method,self,args,&exception);
    if(exception)Log("Managed exception method=%s exception=%p",method_get_name(method),exception);
    return result;
}
inline void* Find(void* c,const char* name,int count){
    for(;c;c=class_get_parent(c))if(auto m=class_get_method_from_name(c,name,count))return m;
    return nullptr;
}
inline void* Find(const char* ns,const char* cls,const char* name,int count){return Find(Class(ns,cls),name,count);}
inline void* Call(void* self,const char* name,int count=0,void** args=nullptr){
    return self?Invoke(Find(((Object*)self)->klass,name,count),self,args):nullptr;
}
template<class T> inline T Value(void* obj){return obj?*(T*)object_unbox(obj):T{};}
inline void Set(void* obj,const char* field,void* value){
    for(void* c=((Object*)obj)->klass;c;c=class_get_parent(c))if(auto f=class_get_field_from_name(c,field)){field_set_value(obj,f,value);return;}
}
}
