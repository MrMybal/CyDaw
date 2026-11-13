#pragma once
#include <cydaw/base/Value.h>
#include <cydaw/engine/NativeDspGraph.h>
#include <optional>
namespace cydaw::document {
base::Value serialiseNativeDspGraphState(const engine::NativeDspGraph&);
std::optional<engine::NativeDspGraph> restoreNativeDspGraphState(const base::Value&,base::Text& error);
void writeEqBands(base::Object&,const engine::NativeDspNodeParameters&);
void readEqBands(const base::Object&,engine::NativeDspNodeParameters&);
}
