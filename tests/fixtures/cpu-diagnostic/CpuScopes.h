#pragma once
#ifndef CYDAW_QA_CPU_SCOPES
#error Diagnostic scopes are only for the explicit QA build.
#endif
#include <cstdint>
namespace cydaw_cpu_qa {
struct Scope {
  std::uint64_t start{}, children{}; unsigned site{}, slot{}; Scope* parent{};
  explicit Scope(unsigned) noexcept;
  ~Scope() noexcept;
};
}
#define CPU_QA_JOIN_(a,b) a##b
#define CPU_QA_JOIN(a,b) CPU_QA_JOIN_(a,b)
#define CPU_QA_SCOPE(id) ::cydaw_cpu_qa::Scope CPU_QA_JOIN(cpuQaScope_,__LINE__)(id)
