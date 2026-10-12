#include "NodeTree.h"

#include "Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <initializer_list>

namespace cydaw::v2::node_tree {

using ui::Ls;

namespace {

bool among(const juce::String& key, const std::initializer_list<const char*> keys)
{
    return std::any_of(keys.begin(), keys.end(), [&key](const char* candidate) { return key == candidate; });
}

// The order branches come in; others follow, by name.
int rank(const juce::String& name)
{
    static const std::initializer_list<std::pair<const char*, const char*>> order {
        { "Audio", "Audio" },
        // The graphs ready made, right after the sound's own nodes.
        { "Graphes", "Graphs" },
        { "Instruments (voix)", "Instruments (voices)" },
        { "Générateurs", "Generators" },
        { "Effets", "Effects" },
        { "Modulation", "Modulation" },
        { "Échos et réverbération", "Echoes and reverb" },
        { "Distorsion", "Distortion" },
        { "Filtres et EQ", "Filters and EQ" },
        { "Filtres", "Filters" },
        { "Égaliseurs", "Equalisers" },
        { "Dynamique", "Dynamics" },
        { "Utilitaires", "Utilities" },
        { "Gain et mixage", "Gain and mixing" },
        { "Canaux et stéréo", "Channels and stereo" },
        { "MIDI", "MIDI" },
        { "Contrôle", "Control" },
        { "Modulateurs", "Modulators" },
        { "Maths", "Maths" },
        { "Logique", "Logic" },
        { "Transport", "Transport" },
        { "Analyse et affichage", "Analysis and display" },
        { "Entrées et sorties", "Inputs and outputs" },
        { "Entrées", "Inputs" },
        { "Sorties", "Outputs" },
        { "Réutilisables", "Reusable" },
        { "Ce projet", "This project" },
    };
    auto index = 0;
    for (const auto& [fr, en] : order) {
        if (name == Ls(fr, en))
            return index;
        ++index;
    }
    return 1'000;
}

void sortBranch(Branch& branch)
{
    for (auto& child : branch.children)
        sortBranch(child);
    std::stable_sort(branch.children.begin(), branch.children.end(), [](const Branch& a, const Branch& b) {
        const auto ra = rank(a.name);
        const auto rb = rank(b.name);
        return ra != rb ? ra < rb : a.name.compareNatural(b.name) < 0;
    });
    std::stable_sort(branch.leaves.begin(), branch.leaves.end(),
                     [](const Type* a, const Type* b) { return a->title.compareNatural(b->title) < 0; });
    branch.count = static_cast<int>(branch.leaves.size());
    for (const auto& child : branch.children)
        branch.count += child.count;
}

void drawBranch(const Branch& branch, const bool openAll, const int depth, const std::function<void(const Type&)>& leaf)
{
    for (std::size_t index = 0; index < branch.children.size(); ++index) {
        const auto& child = branch.children[index];
        ImGui::PushID(static_cast<int>(index));
        if (openAll)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        // The first level open, but for the long list of inputs and outputs.
        if (depth == 0 && child.name != Ls("Entrées et sorties", "Inputs and outputs"))
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const auto label = child.name + "  " + juce::String(child.count) + "###branch";
        if (ImGui::TreeNodeEx(label.toRawUTF8(), flags)) {
            drawBranch(child, openAll, depth + 1, leaf);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    // Leaves line up with the names of the branches beside them.
    const auto indent = depth > 0 ? ImGui::GetTreeNodeToLabelSpacing() * 0.55f : 0.0f;
    if (indent > 0.0f)
        ImGui::Indent(indent);
    for (const auto* type : branch.leaves) {
        ImGui::PushID(type->key.toRawUTF8());
        leaf(*type);
        ImGui::PopID();
    }
    if (indent > 0.0f)
        ImGui::Unindent(indent);
}

} // namespace

std::vector<juce::String> pathOf(const Type& type)
{
    const auto& key = type.key;
    const auto& category = type.categoryKey;
    const auto audio = Ls("Audio", "Audio");
    const auto control = Ls("Contrôle", "Control");
    if (category == "nodes.category.instruments")
        return { audio, Ls("Instruments (voix)", "Instruments (voices)") };
    if (category == "nodes.category.generators")
        return { audio, Ls("Générateurs", "Generators") };
    if (category == "nodes.category.effects") {
        const auto effects = Ls("Effets", "Effects");
        if (among(key, { "chorus", "flanger", "phaser", "tremolo-pan", "tremolo" }))
            return { audio, effects, Ls("Modulation", "Modulation") };
        if (among(key, { "delay", "ping-pong", "reverb", "reverb-rack", "reverb-pro", "echo-bande", "halo" }))
            return { audio, effects, Ls("Échos et réverbération", "Echoes and reverb") };
        if (among(key, { "drive", "hard-clip", "bitcrusher", "saturation", "fournaise" }))
            return { audio, effects, Ls("Distorsion", "Distortion") };
        if (among(key, { "amp-sim", "cab-sim", "mic-sim" }))
            return { audio, effects, Ls("Amplis et micros", "Amps and microphones") };
        if (among(key, { "patine" }))
            return { audio, effects, Ls("Couleur et usure", "Colour and wear") };
        if (among(key, { "tranche", "finition" }))
            return { audio, effects, Ls("Suites de mixage et de mastering", "Mixing and mastering suites") };
        return { audio, effects };
    }
    if (category == "nodes.category.filters") {
        const auto filters = Ls("Filtres et EQ", "Filters and EQ");
        if (among(key, { "eq-band", "eq", "eq-three", "eq-pro" }))
            return { audio, filters, Ls("Égaliseurs", "Equalisers") };
        return { audio, filters, Ls("Filtres", "Filters") };
    }
    if (category == "nodes.category.dynamics")
        return { audio, Ls("Dynamique", "Dynamics") };
    if (category == "nodes.category.utilities") {
        const auto utilities = Ls("Utilitaires", "Utilities");
        if (among(key, { "stereo-width", "mono-to-stereo", "stereo-to-mono" }))
            return { audio, utilities, Ls("Canaux et stéréo", "Channels and stereo") };
        return { audio, utilities, Ls("Gain et mixage", "Gain and mixing") };
    }
    if (category == "nodes.category.midi")
        return { Ls("MIDI", "MIDI") };
    if (category == "nodes.category.modulators")
        return { control, Ls("Modulateurs", "Modulators") };
    if (category == "nodes.category.math")
        return { control, Ls("Maths", "Maths") };
    if (category == "nodes.category.logic")
        return { control, Ls("Logique", "Logic") };
    if (category == "nodes.category.transport")
        return { control, Ls("Transport", "Transport") };
    if (category == "nodes.category.analyzers")
        return { Ls("Analyse et affichage", "Analysis and display") };
    if (category == "nodes.category.io" || category == "nodes.category.graphPorts") {
        const auto both = Ls("Entrées et sorties", "Inputs and outputs");
        if (key.contains("-input") || key == "midi-input")
            return { both, Ls("Entrées", "Inputs") };
        if (key.contains("-output") || key == "master")
            return { both, Ls("Sorties", "Outputs") };
        return { both };
    }
    if (category == "nodes.category.modules") {
        // A factory graph under its own category: Graphes › Effets › Délai et espace.
        std::vector<juce::String> path { Ls("Graphes", "Graphs") };
        for (const auto& part : juce::StringArray::fromTokens(type.moduleCategory, "/", ""))
            if (part.trim().isNotEmpty())
                path.push_back(part.trim());
        if (path.size() == 1)
            path.push_back(Ls("Réutilisables", "Reusable"));
        return path;
    }
    if (category == "nodes.category.inlineGraphs")
        return { Ls("Graphes", "Graphs"), Ls("Ce projet", "This project") };
    // Anything else: its category's own levels ("A / B").
    std::vector<juce::String> path;
    for (const auto& part : juce::StringArray::fromTokens(type.category, "/", ""))
        if (part.trim().isNotEmpty())
            path.push_back(part.trim());
    if (path.empty())
        path.push_back(Ls("Autres", "Other"));
    return path;
}

juce::String pathText(const Type& type)
{
    juce::String text;
    for (const auto& part : pathOf(type))
        text << (text.isEmpty() ? "" : juce::String::fromUTF8(" \xe2\x80\xba ")) << part;
    return text;
}

Branch build(const std::vector<const Type*>& types)
{
    Branch root;
    for (const auto* type : types) {
        auto* branch = &root;
        for (const auto& name : pathOf(*type)) {
            auto found = std::find_if(branch->children.begin(), branch->children.end(),
                                      [&name](const Branch& child) { return child.name == name; });
            if (found == branch->children.end()) {
                branch->children.push_back({ name });
                found = branch->children.end() - 1;
            }
            branch = &*found;
        }
        branch->leaves.push_back(type);
    }
    sortBranch(root);
    return root;
}

void draw(const Branch& root, const bool openAll, const std::function<void(const Type&)>& leaf)
{
    drawBranch(root, openAll, 0, leaf);
}

} // namespace cydaw::v2::node_tree
