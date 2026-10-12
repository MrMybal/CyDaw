#include <cydaw/modules/ModuleLibrary.h>

#include <functional>
#include <queue>
#include <unordered_set>

namespace cydaw::modules {
namespace {
ModuleResult failure(const ModuleError error, std::string message) { return { error, std::move(message) }; }
}

ModuleResult ModuleLibrary::add(ModuleDefinition module)
{
    if (module.id == 0 || module.slug.empty())
        return failure(ModuleError::invalidId, "A module needs a non-zero id and a stable slug");
    if (modules_.contains(module.id))
        return failure(ModuleError::duplicateId, "A module with this id already exists");
    if (slugs_.contains(module.slug))
        return failure(ModuleError::duplicateSlug, "A module with this slug already exists");

    for (const auto& parameter : module.parameters) {
        if (parameter.minimum > parameter.maximum || parameter.defaultValue < parameter.minimum || parameter.defaultValue > parameter.maximum)
            return failure(ModuleError::invalidParameterRange, "A published parameter has an invalid range");
    }

    std::unordered_set<ParameterId> parameterIds;
    for (const auto& parameter : module.parameters)
        parameterIds.insert(parameter.id);
    for (const auto& element : module.interface) {
        if (element.parameterBinding && !parameterIds.contains(*element.parameterBinding))
            return failure(ModuleError::missingParameterBinding, "An interface element targets an unknown parameter");
    }

    slugs_.emplace(module.slug, module.id);
    modules_.emplace(module.id, std::move(module));
    return {};
}

ModuleResult ModuleLibrary::remove(const ModuleId id)
{
    const auto position = modules_.find(id);
    if (position == modules_.end())
        return failure(ModuleError::invalidId, "Cannot remove an unknown module");
    slugs_.erase(position->second.slug);
    modules_.erase(position);
    return {};
}

ModuleResult ModuleLibrary::validate() const
{
    std::unordered_map<ModuleId, std::size_t> incoming;
    std::unordered_map<ModuleId, std::vector<ModuleId>> dependents;
    for (const auto& [id, unused] : modules_) { (void) unused; incoming.emplace(id, 0); }

    for (const auto& [id, module] : modules_) {
        std::unordered_set<ModuleId> uniqueDependencies;
        for (const auto dependency : module.dependencies) {
            if (!modules_.contains(dependency))
                return failure(ModuleError::missingDependency, "A module dependency is not installed");
            if (uniqueDependencies.insert(dependency).second) {
                ++incoming[id];
                dependents[dependency].push_back(id);
            }
        }
    }

    std::priority_queue<ModuleId, std::vector<ModuleId>, std::greater<>> ready;
    for (const auto& [id, count] : incoming)
        if (count == 0) ready.push(id);

    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto id = ready.top(); ready.pop(); ++visited;
        for (const auto dependent : dependents[id])
            if (--incoming[dependent] == 0) ready.push(dependent);
    }
    if (visited != modules_.size())
        return failure(ModuleError::cyclicDependency, "Module composition contains a dependency cycle");
    return {};
}

std::optional<CompiledModuleLibrary> ModuleLibrary::compile() const
{
    if (!validate()) return std::nullopt;

    std::unordered_map<ModuleId, std::size_t> incoming;
    std::unordered_map<ModuleId, std::vector<ModuleId>> dependents;
    for (const auto& [id, unused] : modules_) { (void) unused; incoming.emplace(id, 0); }
    for (const auto& [id, module] : modules_) {
        std::unordered_set<ModuleId> uniqueDependencies;
        for (const auto dependency : module.dependencies) {
            if (uniqueDependencies.insert(dependency).second) {
                ++incoming[id]; dependents[dependency].push_back(id);
            }
        }
    }

    std::priority_queue<ModuleId, std::vector<ModuleId>, std::greater<>> ready;
    for (const auto& [id, count] : incoming)
        if (count == 0) ready.push(id);

    CompiledModuleLibrary compiled;
    while (!ready.empty()) {
        const auto id = ready.top(); ready.pop(); compiled.loadOrder.push_back(id);
        for (const auto dependent : dependents[id])
            if (--incoming[dependent] == 0) ready.push(dependent);
    }
    return compiled;
}

const ModuleDefinition* ModuleLibrary::find(const ModuleId id) const noexcept
{
    const auto position = modules_.find(id);
    return position == modules_.end() ? nullptr : &position->second;
}

const ModuleDefinition* ModuleLibrary::findBySlug(const std::string& slug) const noexcept
{
    const auto position = slugs_.find(slug);
    return position == slugs_.end() ? nullptr : find(position->second);
}

std::size_t ModuleLibrary::size() const noexcept { return modules_.size(); }

} // namespace cydaw::modules

