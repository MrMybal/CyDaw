#include <cydaw/audio/SampleBuffer.h>
#include <cydsp/math/VectorOps.h>
#include <chrono>
#include <cstdio>
#include <cmath>
static volatile float resultValue;
int main(){
 using namespace cydsp;
 constexpr int n=1024, iterations=300000;
 alignas(64) float a[n],b[n]; for(int i=0;i<n;++i){a[i]=.1f+float(i%13)*.001f;b[i]=.2f;}
 VectorOps scalar,automatic; if(VectorOps::create(CpuBackend::automatic,automatic)!=Status::ok)return 2;
 std::printf("{\"frames\":%d,\"iterations\":%d,\"defaultBackend\":%d,\"automaticBackend\":%d,\"rows\":[",n,iterations,int(scalar.backend()),int(automatic.backend()));
 bool first=true;
 for(int pass=0;pass<2;++pass)for(int op=0;op<4;++op){
  const auto& v=pass?automatic:scalar;float sum{};Status status=Status::ok;
  auto call=[&]{switch(op){case 0:v.fill(std::span<float>(b,n),.2f);break;case 1:status=v.scale(std::span<const float>(a,n),std::span<float>(b,n),.5f);break;case 2:status=v.addScaled(std::span<const float>(a,n),std::span<float>(b,n),.000001f);break;case 3:status=v.dot(std::span<const float>(a,n),std::span<const float>(b,n),sum);break;}};
  for(int i=0;i<1000;++i)call();
  auto start=std::chrono::steady_clock::now();for(int i=0;i<iterations;++i)call();
  double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
  if(status!=Status::ok||!std::isfinite(sum)||!std::isfinite(b[n-1]))return 3;resultValue=sum+b[n-1];
  std::printf("%s{\"backend\":%d,\"operation\":\"%s\",\"nsPerCall\":%.3f}",first?"":",",int(v.backend()),op==0?"fill":op==1?"scale":op==2?"addScaled":"dot",ns);first=false;
 }
 cydaw::audio::SampleBuffer<float> x(2,n);x.clear();x.copyFrom(0,0,a,n);x.copyFrom(1,0,a,n);
 for(int op=0;op<4;++op){
  auto call=[&]{switch(op){case 0:x.clear();break;case 1:x.applyGain(1.f);break;case 2:x.addFrom(0,0,a,n,.000001f);break;case 3:resultValue=x.getRMSLevel(0,0,n);break;}};
  for(int i=0;i<1000;++i)call();
  auto start=std::chrono::steady_clock::now();for(int i=0;i<iterations;++i)call();
  double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
  std::printf(",{\"wrapper\":\"SampleBuffer\",\"operation\":\"%s\",\"nsPerCall\":%.3f}",op==0?"clearStereo":op==1?"gainStereo":op==2?"addMono":"rmsMono",ns);
 }
 std::puts("]}");return 0;
}
