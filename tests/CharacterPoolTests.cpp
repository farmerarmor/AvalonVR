#include "CharacterPoolPolicy.h"
#include <cassert>
#include <limits>
#include <iterator>
int main(){
 constexpr wchar_t key[]=L"kandra.buffer_size_multiplier";auto len=std::size(key)-1;
 assert(characterpool::Effective(key,len,1,2,false)==1);
 assert(characterpool::Effective(key,len,1,2,true)==2);
 assert(characterpool::Effective(key,len,3,2,true)==3);
 assert(characterpool::Effective(key,len,1,1,true)==1);
 assert(characterpool::Effective(key,len,1,99,true)==4);
 assert(characterpool::Effective(key,len,1,-3,true)==1);
 assert(characterpool::Effective(L"other",5,1,2,true)==1);
 assert(characterpool::Effective(nullptr,len,1,2,true)==1);
 assert(characterpool::Effective(key,len-1,1,2,true)==1);
 assert(characterpool::Effective(key,len,0,2,true)==0);
 assert(characterpool::Effective(key,len,-1,2,true)==-1);
 assert(characterpool::Effective(key,len,20,2,true)==20);
 auto nan=std::numeric_limits<float>::quiet_NaN();assert(std::isnan(characterpool::Effective(key,len,nan,2,true)));
 for(int capacity:{1000,6400,50,2500,750000,3850000,13000,2750000})assert(int(capacity*characterpool::Effective(key,len,1,2,true))==capacity*2);
}
