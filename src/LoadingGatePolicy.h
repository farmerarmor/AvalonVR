#pragma once
#include <cstdint>
namespace loading {
struct Decision {bool nativeOnly{},entered{},exited{};};
class Gate {
 bool blocked{};uint64_t resumeAt{};
public:
 Decision Observe(bool available,bool active,uint64_t now){
  bool next=!available||active;Decision result{};result.entered=next&&!blocked;result.exited=!next&&blocked;blocked=next;
  // Keep the native path briefly after the last loading frame so its final
  // render callbacks complete before dedicated eye cameras are reintroduced.
  if(next)resumeAt=now+250;
  result.nativeOnly=next||now<resumeAt;return result;
 }
};
}
