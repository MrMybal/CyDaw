#include <cydaw/base/Text.h>
#include "NativeDspRuntime.h"
namespace cydaw::app
{
namespace
{
cydaw::base::Result desktopResult(const cydaw::native::Result &result)
{
    return result.failed() ? cydaw::base::Result::fail(cydaw::base::Text::fromUTF8(result.error.c_str())) : cydaw::base::Result::ok();
}
} // namespace
cydaw::base::Result NativeDspRuntime::setGraph(const engine::NativeDspGraph &graph)
{
    return desktopResult(Base::setGraph(graph));
}
cydaw::base::Result NativeDspRuntime::updateParameters(const engine::NativeDspGraph &graph)
{
    return desktopResult(Base::updateParameters(graph));
}
} // namespace cydaw::app
