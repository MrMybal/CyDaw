#pragma once
#include <filesystem>
#include <string>
namespace cydaw::native
{
// A file the export put in the plug-in's bundle (its voice data), or empty
// when it is not there.
std::filesystem::path findResource(const std::string &name);
} // namespace cydaw::native
