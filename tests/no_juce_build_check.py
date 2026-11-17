"""Reject framework code in the complete source tree and production dependency graph."""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

build = pathlib.Path(sys.argv[1]).resolve()
seen = set()
ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}

def visit(project):
    project = project.resolve()
    if project in seen:
        return
    seen.add(project)
    assert project.is_file(), f"Missing generated project: {project}"
    assert not re.search(r"(^|[/\\])juce(?:[_/\\.]|$)", str(project), re.I), project
    root = ET.parse(project).getroot()
    for element in root.iter():
        if element.tag.rsplit("}", 1)[-1] in {"AdditionalIncludeDirectories", "AdditionalDependencies", "PrecompiledHeaderFile"}:
            assert not re.search(r"(?:juce::|juce_[a-z]|[/\\]juce[/\\])", element.text or "", re.I), (project, element.text)
    for reference in root.findall(".//m:ProjectReference", ns):
        if "Include" in reference.attrib:
            visit(project.parent / reference.attrib["Include"])

for name in ("CyDAW2.vcxproj", "CyDAWPluginWorker.vcxproj"):
    visit(build / name)
assert not (build / "JUCE").exists(), "Strict build unexpectedly configured JUCE"
assert not (build / "_deps/juce-src").exists(), "Strict build unexpectedly fetched JUCE"
print(f"Production desktop + worker: {len(seen)} generated dependency projects, no JUCE includes or libraries")

source = pathlib.Path(__file__).resolve().parents[1]
for folder in ("src", "include", "tools", "tests", "third_party", "cmake", "scripts"):
    for path in (source / folder).rglob("*"):
        if path.suffix.lower() in {".h", ".hpp", ".cpp", ".c", ".inl", ".mm", ".patch"}:
            assert not re.search(r"#\s*include\s*[<\"]juce_|\bjuce::", path.read_text("utf-8")), path
for folder in (source, source / "cmake", source / "src", source / "third_party"):
    paths = folder.glob("CMakeLists.txt") if folder == source else folder.rglob("*")
    for path in paths:
        if path.is_file() and (path.name == "CMakeLists.txt" or path.suffix == ".cmake"
                               or path.name.startswith("cmake.") and path.suffix == ".txt"):
            assert not re.search(r"FetchContent_Declare\(\s*JUCE|find_package\(\s*JUCE|juce_add_|juce::",
                                 path.read_text("utf-8"), re.I), path
for relative in ("tests/fixtures/juce-benchmark-optimization.cmake",
                 "tests/fixtures/juce-benchmark-profile.patch",
                 "tests/fixtures/project-audio/VoiceTimingProbe.cpp"):
    assert not (source / relative).exists(), f"Retired framework fixture returned: {relative}"
print("Application, authoring tools, tests and vendored sources contain no JUCE includes or types")
