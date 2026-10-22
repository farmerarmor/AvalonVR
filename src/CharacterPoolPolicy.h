#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cwchar>
#include <iterator>
namespace characterpool {
inline int Scale(int requested){return std::clamp(requested,1,4);}
inline float Effective(const wchar_t* key,size_t length,float native,int requested,bool constructed){
 constexpr wchar_t wanted[]=L"kandra.buffer_size_multiplier";
 if(!constructed||!key||length!=std::size(wanted)-1||wmemcmp(key,wanted,length)||!std::isfinite(native)||native<=0||native>16)return native;
 return std::max(native,float(Scale(requested)));
}
}
