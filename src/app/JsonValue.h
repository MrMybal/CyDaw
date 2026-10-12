#pragma once

#include <juce_core/juce_core.h>

namespace cydaw::app {

// Copying and comparing the juce::var trees this application uses as its
// document format. Both used to be done by going through JSON text, which is
// the most expensive way to do either: every number formatted, the whole
// string allocated, then parsed again. Measured on this project, a copy cost
// forty times what a walk costs, and the autosave paid it fifteen times on
// the message thread.
//
// They live in a header because the workspace and the node editor are
// separate translation units and both need them; the same knowledge written
// twice is how this codebase has gone wrong before.

[[nodiscard]] inline juce::var cloneJsonValue(const juce::var& value)
{
    if (value.isVoid() || value.isUndefined())
        return {};
    if (const auto* array = value.getArray()) {
        juce::Array<juce::var> copy;
        copy.ensureStorageAllocated(array->size());
        for (const auto& element : *array)
            copy.add(cloneJsonValue(element));
        return copy;
    }
    if (auto* object = value.getDynamicObject()) {
        // The property set copied whole, then each value cloned in place:
        // inserting the properties one by one searched the set for every
        // one of them, and a class library is thousands of properties.
        auto copy = juce::DynamicObject::Ptr(new juce::DynamicObject(*object));
        auto& properties = copy->getProperties();
        for (int index = 0; index < properties.size(); ++index)
            if (auto* nested = properties.getVarPointerAt(index))
                *nested = cloneJsonValue(*nested);
        return juce::var(copy.get());
    }
    // Numbers, strings and booleans are values already; copying one is what
    // returning it does.
    return value;
}

// Deep equality. juce::var's own operator== compares two objects by address,
// so two identical documents built separately never match; that is why the
// edit history compared them as strings.
[[nodiscard]] inline bool jsonValuesMatch(const juce::var& left,
                                          const juce::var& right)
{
    if (const auto* leftArray = left.getArray()) {
        const auto* rightArray = right.getArray();
        if (rightArray == nullptr || leftArray->size() != rightArray->size())
            return false;
        for (int index = 0; index < leftArray->size(); ++index)
            if (!jsonValuesMatch((*leftArray)[index], (*rightArray)[index]))
                return false;
        return true;
    }
    if (right.getArray() != nullptr)
        return false;
    if (const auto* leftObject = left.getDynamicObject()) {
        const auto* rightObject = right.getDynamicObject();
        if (rightObject == nullptr)
            return false;
        if (leftObject == rightObject)
            return true;
        const auto& leftProperties = leftObject->getProperties();
        const auto& rightProperties = rightObject->getProperties();
        if (leftProperties.size() != rightProperties.size())
            return false;
        for (int index = 0; index < leftProperties.size(); ++index) {
            const auto name = leftProperties.getName(index);
            if (!rightProperties.contains(name)
                || !jsonValuesMatch(leftProperties.getValueAt(index),
                                    rightProperties[name]))
                return false;
        }
        return true;
    }
    if (right.getDynamicObject() != nullptr)
        return false;
    // Void and undefined are distinct to juce::var but mean the same absence
    // here, and a document that round-tripped through JSON has already lost
    // the difference.
    if (left.isVoid() || left.isUndefined())
        return right.isVoid() || right.isUndefined();
    return left == right;
}

}  // namespace cydaw::app
