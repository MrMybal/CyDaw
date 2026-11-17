#include "CpuScopes.h"
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>
namespace cydaw_cpu_qa {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
struct Counter { std::atomic<std::uint64_t> calls{}, inclusive{}, exclusive{}; };
struct alignas(64) Slot { std::atomic<DWORD> tid{}; Counter counters[256]; };
static Slot slots[128];
static std::atomic<unsigned> used{};
static thread_local unsigned owner = 128;
static thread_local Scope* current{};
static std::uint64_t now() noexcept { LARGE_INTEGER v; QueryPerformanceCounter(&v);return v.QuadPart; }
Scope::Scope(unsigned id) noexcept : site(id), parent(current) {
 if(owner==128) { auto n=used.fetch_add(1,std::memory_order_relaxed); owner=n<128?n:129; if(owner<128)slots[owner].tid.store(GetCurrentThreadId(),std::memory_order_release); }
 slot=owner; current=this; start=now();
}
Scope::~Scope() noexcept {
 const auto elapsed=now()-start;
 current=parent; if(parent) parent->children+=elapsed;
 if(slot>=128||site>=256)return;
 auto& c=slots[slot].counters[site];
 c.inclusive.fetch_add(elapsed,std::memory_order_relaxed);
 c.exclusive.fetch_add(elapsed>=children?elapsed-children:0,std::memory_order_relaxed);
 c.calls.fetch_add(1,std::memory_order_relaxed);
}
// Observer owns all file I/O and allocation. No observer locks/calls in callbacks.
struct Observer {
 std::atomic<bool> done{}; std::thread thread;
 Observer() {
  const char* file=std::getenv("CYDAW_QA_CPU_TRACE"); if(!file||!*file)return;
  const std::string path(file);
  thread=std::thread([this,path] {
   FILE* f=nullptr;fopen_s(&f,path.c_str(),"wb");if(!f)return;
   LARGE_INTEGER freq;QueryPerformanceFrequency(&freq);
   std::fprintf(f,"qpcSeconds,tid,site,calls,inclusiveSeconds,exclusiveSeconds\n");
   while(!done.load()) {
    const auto at=double(now())/freq.QuadPart;
    for(unsigned s=0;s<128;++s)if(auto tid=slots[s].tid.load(std::memory_order_acquire))
     for(unsigned i=0;i<256;++i) { auto& c=slots[s].counters[i];const auto calls=c.calls.load(std::memory_order_relaxed);if(calls)
      std::fprintf(f,"%.9f,%lu,%u,%llu,%.9f,%.9f\n",at,tid,i,(unsigned long long)calls,double(c.inclusive.load())/freq.QuadPart,double(c.exclusive.load())/freq.QuadPart); }
    std::fflush(f);std::this_thread::sleep_for(std::chrono::milliseconds(500));
   }
   std::fclose(f);
  });
 }
 ~Observer(){done=true;if(thread.joinable())thread.join();}
};
static Observer observer;
}
