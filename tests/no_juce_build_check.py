"""Check the generated production dependency graph, not just source spelling."""
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
for folder in ("src", "include", "tools"):
    for path in (source / folder).rglob("*"):
        if path.suffix in {".h", ".hpp", ".cpp", ".mm"}:
            assert not re.search(r"#\s*include\s*[<\"]juce_|\bjuce::", path.read_text("utf-8")), path
assert not re.search(r"FetchContent_Declare\(JUCE|juce_add_|juce::", (source / "CMakeLists.txt").read_text("utf-8")), "Legacy CMake dependency returned"
print("Active application and authoring tools contain no JUCE includes or types")
