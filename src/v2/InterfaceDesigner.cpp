#include "InterfaceEditor.h"
#include "CustomInterface.h"
#include "DeviceVisuals.h"
#include "InstrumentPanels.h"
#include "Theme.h"
#include "Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <set>

namespace cydaw::v2
{
using ui::dp;
using ui::L;
namespace
{
juce::var designValue(const app::ModuleInterfaceControl &c, const char *key, const juce::var &fallback = {})
{
    return c.design.getProperty(key, fallback);
}
void setDesign(app::ModuleInterfaceControl &c, const char *key, const juce::var &value)
{
    auto next = c.design.isObject() ? c.design.clone() : juce::var(new juce::DynamicObject);
    next.getDynamicObject()->setProperty(key, value);
    c.design = std::move(next);
}
} // namespace

void InterfaceEditor::setDeviceLayout(bool device)
{
    if (deviceLayout_ == device)
        return;
    deviceLayout_ = device;
    slot_ = nullptr;
    selected_ = 0;
    selection_.clear();
    page_.clear();
    drag_ = Drag::none;
    code_[0] = 0;
}
bool InterfaceEditor::visible(const Control &c) const
{
    if (static_cast<bool>(designValue(c, "hidden", false)))
        return false;
    const auto page = designValue(c, "page").toString();
    return page.isEmpty() || page == page_;
}
void InterfaceEditor::drawPageBar()
{
    std::set<juce::String> pages;
    for (const auto &c : interface_.controls)
        if (auto page = designValue(c, "page").toString(); page.isNotEmpty())
            pages.insert(page);
    if (pages.empty())
        return;
    if (!pages.contains(page_))
        page_ = *pages.begin();
    if (ImGui::BeginTabBar("##interfacePages"))
    {
        for (const auto &page : pages)
            if (ImGui::BeginTabItem(page.toRawUTF8()))
            {
                page_ = page;
                ImGui::EndTabItem();
            }
        ImGui::EndTabBar();
    }
}
void InterfaceEditor::drawLayers(GraphSlot &slot)
{
    if (!ImGui::CollapsingHeader(L("Calques et groupes", "Layers and groups"), ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::BeginChild("##layers", ImVec2(-1, dp(190)), ImGuiChildFlags_Borders);
    for (auto it = interface_.controls.rbegin(); it != interface_.controls.rend(); ++it)
    {
        if (it->kind == Kind::inputPort || it->kind == Kind::outputPort)
            continue;
        ImGui::PushID(static_cast<int>(it->id));
        auto shown = !static_cast<bool>(designValue(*it, "hidden", false));
        if (ImGui::Checkbox("##visible", &shown))
        {
            setDesign(*it, "hidden", !shown);
            push(slot, true);
        }
        ImGui::SameLine();
        const auto name =
            it->label.isNotEmpty() ? it->label : juce::String("#") + juce::String(static_cast<juce::int64>(it->id));
        if (ImGui::Selectable(name.toRawUTF8(), selection_.contains(it->id) || selected_ == it->id))
        {
            selected_ = it->id;
            if (ImGui::GetIO().KeyCtrl)
            {
                if (selection_.contains(it->id))
                    selection_.erase(it->id);
                else
                    selection_.insert(it->id);
            }
            else
                selection_ = {it->id};
            const auto page = designValue(*it, "page").toString();
            if (page.isNotEmpty())
                page_ = page;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (selection_.size() > 1)
    {
        const auto align = [&](int axis, int edge) {
            float minimum = 1, maximum = 0;
            for (const auto &c : interface_.controls)
                if (selection_.contains(c.id))
                {
                    minimum = std::min(minimum, axis ? c.y : c.x);
                    maximum = std::max(maximum, axis ? c.y + c.height : c.x + c.width);
                }
            for (auto &c : interface_.controls)
                if (selection_.contains(c.id))
                {
                    auto &pos = axis ? c.y : c.x;
                    const auto size = axis ? c.height : c.width;
                    pos = edge == 0 ? minimum : edge == 1 ? (minimum + maximum - size) * .5f : maximum - size;
                }
            push(slot, true);
        };
        if (ImGui::SmallButton(L("Gauche", "Left")))
            align(0, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Centre", "Center")))
            align(0, 1);
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Droite", "Right")))
            align(0, 2);
        if (ImGui::SmallButton(L("Haut", "Top")))
            align(1, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Milieu", "Middle")))
            align(1, 1);
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Bas", "Bottom")))
            align(1, 2);
        for (int axis = 0; axis < 2; ++axis)
        {
            if (axis)
                ImGui::SameLine();
            if (ImGui::SmallButton(axis ? L("Distribuer V", "Distribute V") : L("Distribuer H", "Distribute H")))
            {
                std::vector<Control *> controls;
                for (auto &c : interface_.controls)
                    if (selection_.contains(c.id))
                        controls.push_back(&c);
                std::sort(controls.begin(), controls.end(),
                          [axis](auto *a, auto *b) { return axis ? a->y < b->y : a->x < b->x; });
                if (controls.size() > 2)
                {
                    const auto first = axis ? controls.front()->y : controls.front()->x;
                    const auto last = axis ? controls.back()->y : controls.back()->x;
                    for (std::size_t i = 1; i + 1 < controls.size(); ++i)
                        (axis ? controls[i]->y : controls[i]->x) =
                            first + (last - first) * static_cast<float>(i) / static_cast<float>(controls.size() - 1);
                    push(slot, true);
                }
            }
        }
        if (ImGui::SmallButton(L("Grouper", "Group")))
        {
            const auto group = "group-" + juce::String(static_cast<juce::int64>(interface_.nextControlId++));
            for (auto &c : interface_.controls)
                if (selection_.contains(c.id))
                    setDesign(c, "group", group);
            push(slot, true);
        }
    }
}
bool InterfaceEditor::addInstrumentInterface(GraphSlot& slot,std::uint64_t nodeId,bool whole,const juce::String& panelId)
{
    pull(slot);
    const auto nodes=slot.graph().editorNodes();const auto node=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.id==nodeId;});
    if(node==nodes.end())return false;
    const auto type=node->typeKey.toStdString();const auto& panels=instrument_ui::panels(type,deviceLayout_);
    if(panels.empty())return false;
    const auto model=engine::synth::modelForType(type);
    const auto prefix=model==engine::synth::Model::none?juce::String(type=="kick-synth"?"kick":"prism"):juce::String(engine::synth::info(model).prefix);
    std::vector<juce::String> bindings;juce::Array<juce::var> keys;
    for(const auto& p:node->parameters)if(p.binding.contains("."+prefix+".")) {
        bindings.push_back(p.binding);keys.add(p.binding.fromFirstOccurrenceOf("."+prefix+".",false,false));
    }
    if(bindings.empty())return false;
    const auto authored=deviceLayout_?interface_.deviceDesigned:interface_.designed;
    if(whole && !authored)std::erase_if(interface_.controls,[](const auto& c){return c.kind!=Kind::inputPort && c.kind!=Kind::outputPort;});
    std::map<std::string,int> rows;
    for(const auto& p:panels)rows[p.page]=std::max(rows[p.page],p.row+1);
    selection_.clear();
    for(const auto& p:panels) {
        if(!whole && p.id!=panelId.toStdString())continue;
        Control c;c.id=interface_.nextControlId++;c.kind=Kind::nativePanel;c.label=juce::String(p.title);c.extraParameterBindings=bindings;
        c.design=new juce::DynamicObject;auto* d=c.design.getDynamicObject();
        d->setProperty("instrumentType",node->typeKey);d->setProperty("instrumentPanel",juce::String(p.id));d->setProperty("instrumentCompact",deviceLayout_);d->setProperty("instrumentKeys",keys);
        const auto color=ImGui::ColorConvertU32ToFloat4(p.colour);d->setProperty("accent",juce::Colour::fromFloatRGBA(color.x,color.y,color.z,color.w).toString());
        if(whole) {
            float total=0,before=0;for(const auto& sibling:panels)if(sibling.page==p.page && sibling.row==p.row){if(sibling.id==p.id)before=total;total+=sibling.weight;}
            c.x=.02f+.96f*before/std::max(.1f,total);c.width=.96f*p.weight/std::max(.1f,total)-.008f;
            c.y=.02f+.96f*p.row/std::max(1,rows[p.page]);c.height=.96f/std::max(1,rows[p.page])-.012f;
            if(!p.page.empty())d->setProperty("page",node->title+" / "+juce::String(p.page));
        } else {c.x=.04f;c.y=.08f;c.width=.6f;c.height=.75f;if(page_.isNotEmpty())d->setProperty("page",page_);}
        interface_.controls.push_back(c);selection_.insert(c.id);selected_=c.id;
    }
    if(selection_.empty())return false;
    if(whole) {
        if(deviceLayout_ && !authored){interface_.windowWidth=1200;interface_.windowHeight=300;}
        else if(!interface_.windowWidth){interface_.windowWidth=1280;interface_.windowHeight=900;}
        selected_=*selection_.begin();
        if(const auto* c=find(selected_))page_=c->design.getProperty("page",{}).toString();
        selection_={selected_};
    }
    return push(slot,true);
}

bool InterfaceEditor::addInstrumentSection(GraphSlot& slot,const juce::String& type,const juce::String& panelId)
{
    pull(slot);
    const auto* spec=instrument_ui::panel(type.toStdString(),panelId.toStdString(),deviceLayout_);
    if(!spec)return false;
    Control c;c.id=interface_.nextControlId++;c.kind=Kind::nativePanel;c.label=juce::String(spec->title);
    c.x=.05f;c.y=.08f;c.width=.65f;c.height=.75f;c.design=new juce::DynamicObject;
    auto* d=c.design.getDynamicObject();d->setProperty("instrumentType",type);d->setProperty("instrumentPanel",panelId);
    d->setProperty("instrumentCompact",deviceLayout_);d->setProperty("instrumentKeys",juce::Array<juce::var>{});
    const auto color=ImGui::ColorConvertU32ToFloat4(spec->colour);d->setProperty("accent",juce::Colour::fromFloatRGBA(color.x,color.y,color.z,color.w).toString());
    if(page_.isNotEmpty())d->setProperty("page",page_);
    interface_.controls.push_back(c);selected_=c.id;selection_={c.id};
    return push(slot,true);
}

void InterfaceEditor::drawNativePalette(GraphSlot &slot)
{
    if (!ImGui::CollapsingHeader(L("Panneaux natifs CyDAW", "CyDAW native panels"), ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if(ImGui::TreeNode(L("Bibliothèque de sections","Section library"))) {
        const auto model=libraryInstrument_<15?static_cast<engine::synth::Model>(libraryInstrument_+1):engine::synth::Model::none;
        const auto type=model==engine::synth::Model::none?juce::String(libraryInstrument_==16?"kick-synth":"prism-synth"):juce::String(engine::synth::info(model).typeKey);
        const auto title=model==engine::synth::Model::none?juce::String(libraryInstrument_==16?"Kick":"Prisme"):juce::String(engine::synth::info(model).english);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##sectionModel",title.toRawUTF8())) {
            for(int i=0;i<17;++i) {
                const auto label=i<15?engine::synth::info(static_cast<engine::synth::Model>(i+1)).english:std::string(i==16?"Kick":"Prisme");
                if(ImGui::Selectable(label.c_str(),i==libraryInstrument_))libraryInstrument_=i;
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s",L("Sections libres : affectez leurs valeurs dans les liaisons du panneau.","Free sections: assign their values in the panel bindings."));
        for(const auto& panel:instrument_ui::panels(type.toStdString(),deviceLayout_)) {
            ImGui::PushID(panel.id.c_str());
            const auto text=panel.page.empty()?panel.title:panel.page+" / "+panel.title;
            if(ImGui::Selectable(text.c_str()))addInstrumentSection(slot,type,juce::String(panel.id));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    for (const auto &node : slot.graph().editorNodes())
    {
        const auto& instrument=instrument_ui::panels(node.typeKey.toStdString(),deviceLayout_);
        if(!instrument.empty()) {
            ImGui::PushID(static_cast<int>(node.id));
            if(ImGui::TreeNodeEx(node.title.toRawUTF8(),ImGuiTreeNodeFlags_DefaultOpen)) {
                if(ImGui::Button(L("Ajouter l'interface complète","Add full interface"),ImVec2(-1,0)))addInstrumentInterface(slot,node.id,true);
                for(const auto& panel:instrument) {
                    const auto text=panel.page.empty()?panel.title:panel.page+" / "+panel.title;
                    if(ImGui::Selectable(text.c_str()))addInstrumentInterface(slot,node.id,false,juce::String(panel.id));
                }
                ImGui::TreePop();
            }
            ImGui::PopID();continue;
        }
        std::vector<visual::Param> parameters;
        for (const auto &p : node.parameters)
            parameters.push_back({p.binding, p.label, p.unit, p.minimum, p.maximum, p.value, p.defaultValue});
        const auto surface = visual::inferSurface(parameters, false, false, node.typeKey);
        for (std::size_t i = 0; i < surface.panes.size(); ++i)
        {
            const auto &pane = surface.panes[i];
            const auto label = node.title + " / " + visual::paneTitle(pane);
            ImGui::PushID(static_cast<int>(node.id));
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label.toRawUTF8()))
            {
                Control c;
                c.id = interface_.nextControlId++;
                c.kind = Kind::nativePanel;
                c.label = label;
                c.x = .04f;
                c.y = .08f;
                c.width = .7f;
                c.height = .65f;
                c.design = new juce::DynamicObject;
                c.design.getDynamicObject()->setProperty("pane", static_cast<int>(pane.kind));
                c.design.getDynamicObject()->setProperty("variant", pane.variant);
                c.design.getDynamicObject()->setProperty("large",!deviceLayout_);
                if (page_.isNotEmpty())
                    c.design.getDynamicObject()->setProperty("page", page_);
                juce::Array<juce::var> indices;
                for (const auto index : pane.parameters)
                {
                    if (index < 0 || index >= static_cast<int>(parameters.size()))
                    {
                        indices.add(-1);
                        continue;
                    }
                    indices.add(static_cast<int>(c.extraParameterBindings.size()));
                    c.extraParameterBindings.push_back(parameters[static_cast<std::size_t>(index)].binding);
                }
                c.design.getDynamicObject()->setProperty("indices", indices);
                interface_.controls.push_back(c);
                selected_ = c.id;
                selection_ = {c.id};
                push(slot, true);
                ImGui::PopID();
                ImGui::PopID();
                return;
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
}

ImU32 InterfaceEditor::surfaceColour(const char* key, ImU32 fallback) const
{
    const auto value=interface_.windowDesign.getProperty(key,{}).toString();
    if(value.isEmpty())return fallback;
    const auto c=juce::Colour::fromString(value);
    return ImGui::ColorConvertFloat4ToU32({c.getFloatRed(),c.getFloatGreen(),c.getFloatBlue(),c.getFloatAlpha()});
}

void InterfaceEditor::drawWindowProperties(GraphSlot& slot)
{
    if(!ImGui::CollapsingHeader(deviceLayout_?L("Réglages de la carte module","Module card settings"):
                                             L("Réglages de la grande fenêtre","Full window settings"),ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto saveSize=[&](int w,int h){
        interface_.windowWidth=static_cast<std::uint16_t>(std::clamp(w,deviceLayout_?120:240,4096));
        interface_.windowHeight=static_cast<std::uint16_t>(std::clamp(h,deviceLayout_?80:160,2160));
        push(slot,true);
    };
    auto size=windowSize();int w=static_cast<int>(size.x),h=static_cast<int>(size.y);
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo(L("Format","Format"),L("Choisir un format…","Choose a size…"))) {
        const int presets[][2]={{720,420},{1000,700},{1280,800},{1600,900},{480,240},{720,280}};
        for(const auto& preset:presets) {
            const auto label=juce::String(preset[0])+" x "+juce::String(preset[1]);
            if(ImGui::Selectable(label.toRawUTF8(),w==preset[0]&&h==preset[1]))saveSize(preset[0],preset[1]);
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted(L("Largeur (px)","Width (px)"));ImGui::SetNextItemWidth(-1);
    if(ImGui::InputInt("##surfaceWidth",&w,10,100,ImGuiInputTextFlags_EnterReturnsTrue))saveSize(w,h);
    if(ImGui::IsItemDeactivatedAfterEdit())saveSize(w,h);
    ImGui::TextUnformatted(L("Hauteur (px)","Height (px)"));ImGui::SetNextItemWidth(-1);
    if(ImGui::InputInt("##surfaceHeight",&h,10,100,ImGuiInputTextFlags_EnterReturnsTrue))saveSize(w,h);
    if(ImGui::IsItemDeactivatedAfterEdit())saveSize(w,h);
    for(const auto* key:{"background","accent","text"}) {
        const auto fallback=juce::String(key)=="background"?theme::palette().bg1:juce::String(key)=="accent"?theme::palette().accent:theme::palette().text;
        const auto c=ImGui::ColorConvertU32ToFloat4(surfaceColour(key,fallback));float rgba[]{c.x,c.y,c.z,c.w};
        const char* label=juce::String(key)=="background"?L("Fond","Background"):juce::String(key)=="accent"?L("Accent","Accent"):L("Texte","Text");
        if(ImGui::ColorEdit4(label,rgba,ImGuiColorEditFlags_NoInputs)) {
            auto design=interface_.windowDesign.isObject()?interface_.windowDesign.clone():juce::var(new juce::DynamicObject);
            design.getDynamicObject()->setProperty(key,juce::Colour::fromFloatRGBA(rgba[0],rgba[1],rgba[2],rgba[3]).toString());
            interface_.windowDesign=design;push(slot,false);
        }
        if(ImGui::IsItemDeactivatedAfterEdit())push(slot,true);
    }
    int grid[]{interface_.gridColumns,interface_.gridRows};
    ImGui::TextUnformatted(L("Grille (colonnes / lignes)","Grid (columns / rows)"));ImGui::SetNextItemWidth(-1);
    if(ImGui::DragInt2("##surfaceGrid",grid,.1f,4,64)) {
        interface_.gridColumns=static_cast<std::uint8_t>(std::clamp(grid[0],4,64));
        interface_.gridRows=static_cast<std::uint8_t>(std::clamp(grid[1],4,64));push(slot,false);
    }
    if(ImGui::IsItemDeactivatedAfterEdit())push(slot,true);
    if(ImGui::Checkbox(L("Aimanter","Snap"),&interface_.snapToGrid))push(slot,true);
    if(ImGui::SmallButton(L("Réinitialiser les couleurs","Reset colors"))) {interface_.windowDesign=juce::var();push(slot,true);}
    ImGui::Spacing();
}

void InterfaceEditor::drawCode(GraphSlot &slot)
{
    if (!code_[0])
    {
        const auto text = juce::JSON::toString(app::serialiseModuleInterface(interface_), false);
        std::snprintf(code_.data(), code_.size(), "%s", text.toRawUTF8());
    }
    ImGui::TextWrapped("%s", L("JSON de la disposition active. Les paramètres appartiennent au graphe ; les contrôles "
                               "et leur objet design sont éditables. Appliquer valide avant de remplacer.",
                               "JSON of the active layout. Parameters belong to the graph; controls and their design "
                               "object are editable. Apply validates before replacing."));
    if (ImGui::Button(L("Appliquer le code", "Apply code")))
    {
        juce::var parsed;
        auto result = juce::JSON::parse(juce::String::fromUTF8(code_.data()), parsed);
        if (result.wasOk())
            result = app::validateModuleInterfaceDocument(parsed, interface_);
        if (result.wasOk())
        {
            auto candidate = app::restoreModuleInterface(parsed);
            candidate.parameters = interface_.parameters;
            result = app::validateModuleInterface(candidate);
            // Restoration migrates old files permissively. The code editor is
            // strict: duplicate IDs, unknown kinds and lost bindings are errors.
            if (const auto *source = parsed.getProperty("controls", {}).getArray())
            {
                if (static_cast<std::size_t>(source->size()) != candidate.controls.size())
                    result = juce::Result::fail("Contrôles ignorés : vérifiez les identifiants et les types.");
                for (const auto &c : *source)
                {
                    const auto binding = c.getProperty("binding", {}).toString();
                    if (binding.isNotEmpty() && !parameter(binding))
                        result = juce::Result::fail("Paramètre inconnu : " + binding);
                }
            }
            else
                result = juce::Result::fail("La liste controls est requise.");
            if (result.wasOk())
            {
                interface_ = std::move(candidate);
                if (push(slot, true))
                {
                    codeError_.clear();
                    codeMode_ = false;
                    selected_ = 0;
                    selection_.clear();
                }
            }
        }
        if (result.failed())
            codeError_ = result.getErrorMessage();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Recharger le modèle", "Reload model")))
    {
        code_[0] = 0;
        codeError_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Copier", "Copy")))
        ImGui::SetClipboardText(code_.data());
    if (codeError_.isNotEmpty())
        ImGui::TextWrapped("%s", codeError_.toRawUTF8());
    ImGui::InputTextMultiline("##sceneSource", code_.data(), code_.size(),
                              ImVec2(-1, std::max(dp(100), ImGui::GetContentRegionAvail().y)),
                              ImGuiInputTextFlags_AllowTabInput);
}

void InterfaceEditor::styleProperties(GraphSlot &slot, Control &control)
{
    if (!ImGui::CollapsingHeader(L("Apparence et organisation", "Appearance and organization"),
                                 ImGuiTreeNodeFlags_DefaultOpen))
        return;
    const auto colour = [&](const char *id, const char *key, ImU32 fallback) {
        const auto text = designValue(control, key).toString();
        auto rgba = ImGui::ColorConvertU32ToFloat4(fallback);
        if (text.isNotEmpty())
        {
            const auto c = juce::Colour::fromString(text);
            rgba = {c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), c.getFloatAlpha()};
        }
        if (ImGui::ColorEdit4(id, &rgba.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs))
        {
            setDesign(control, key, juce::Colour::fromFloatRGBA(rgba.x, rgba.y, rgba.z, rgba.w).toString());
            push(slot, false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            push(slot, true);
    };
    const auto &palette = theme::palette();
    colour(L("Accent", "Accent"), "accent", palette.accent);
    colour(L("Fond", "Background"), "background", palette.bg1);
    colour(L("Texte", "Text"), "text", palette.text);
    auto font = static_cast<float>(designValue(control, "fontScale", 1.0));
    if (ImGui::SliderFloat(L("Taille du texte", "Font scale"), &font, .5f, 3.0f))
    {
        setDesign(control, "fontScale", font);
        push(slot, false);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        push(slot, true);
    auto rounding = static_cast<float>(designValue(control, "rounding", 4.0));
    if (ImGui::SliderFloat(L("Arrondi", "Rounding"), &rounding, 0, 30))
    {
        setDesign(control, "rounding", rounding);
        push(slot, false);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        push(slot, true);
    for (const auto *key : {"page", "group"})
    {
        auto &storage = designText_[key];
        if (storage.empty())
            storage.resize(128);
        auto *value = storage.data();
        const auto id = juce::String("##design-") + key;
        if (ImGui::GetActiveID() != ImGui::GetID(id.toRawUTF8()))
            std::snprintf(value, storage.size(), "%s", designValue(control, key).toString().toRawUTF8());
        ImGui::TextUnformatted(key);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText(id.toRawUTF8(), value, storage.size());
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            setDesign(control, key, juce::String::fromUTF8(value));
            push(slot, true);
        }
    }
    auto locked = static_cast<bool>(designValue(control, "locked", false));
    if (ImGui::Checkbox(L("Verrouiller la position", "Lock position"), &locked))
    {
        setDesign(control, "locked", locked);
        push(slot, true);
    }
    auto showValue = static_cast<bool>(designValue(control, "showValue", false));
    if (ImGui::Checkbox(L("Afficher la valeur", "Show value"), &showValue))
    {
        setDesign(control, "showValue", showValue);
        push(slot, true);
    }
    if (control.kind == Kind::nativePanel && control.design.hasProperty("instrumentType")) {
        const auto type=designValue(control,"instrumentType").toString().toStdString();
        const auto panel=designValue(control,"instrumentPanel").toString().toStdString();
        const auto compact=static_cast<bool>(designValue(control,"instrumentCompact",false));
        if(const auto* spec=instrument_ui::panel(type,panel,compact)) {
            if(ImGui::CollapsingHeader(L("Liaisons du panneau natif","Native panel bindings"),ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto keys=designValue(control,"instrumentKeys");
                if(const auto* list=keys.getArray())for(const auto& setting:spec->settings) {
                    int index=-1;for(int i=0;i<list->size();++i)if((*list)[i].toString()==juce::String(setting.key)){index=i;break;}
                    const auto binding=index>=0 && index<static_cast<int>(control.extraParameterBindings.size())?control.extraParameterBindings[static_cast<std::size_t>(index)]:juce::String();
                    const auto* bound=parameter(binding);ImGui::PushID(setting.key.c_str());
                    ImGui::TextUnformatted(setting.label.c_str());ImGui::SetNextItemWidth(-1);
                    if(ImGui::BeginCombo("##nativeBinding",bound?bound->label.toRawUTF8():L("Sans liaison","Unbound"))) {
                        if(index>=0 && ImGui::Selectable(L("Sans liaison","Unbound"),false)) {
                            auto next=keys.clone();next.getArray()->remove(index);setDesign(control,"instrumentKeys",next);
                            control.extraParameterBindings.erase(control.extraParameterBindings.begin()+index);push(slot,true);
                        }
                        for(const auto& candidate:interface_.parameters)if(ImGui::Selectable(candidate.label.toRawUTF8(),candidate.key==binding)) {
                            if(index>=0)control.extraParameterBindings[static_cast<std::size_t>(index)]=candidate.key;
                            else {auto next=keys.clone();if(!next.isArray())next=juce::Array<juce::var>{};next.getArray()->add(juce::String(setting.key));setDesign(control,"instrumentKeys",next);control.extraParameterBindings.push_back(candidate.key);}
                            push(slot,true);
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    if (control.kind == Kind::nativePanel && !control.design.hasProperty("instrumentType"))
    {
        if(ImGui::CollapsingHeader(L("Liaisons du panneau natif","Native panel bindings")))
            for(std::size_t index=0;index<control.extraParameterBindings.size();++index) {
                auto& binding=control.extraParameterBindings[index];const auto* bound=parameter(binding);
                ImGui::PushID(static_cast<int>(index));ImGui::SetNextItemWidth(-1);
                if(ImGui::BeginCombo("##effectBinding",bound?bound->label.toRawUTF8():"—")) {
                    for(const auto& candidate:interface_.parameters)if(ImGui::Selectable(candidate.label.toRawUTF8(),candidate.key==binding)){binding=candidate.key;push(slot,true);}
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
        auto large = static_cast<bool>(designValue(control, "large", false));
        if (ImGui::Checkbox(L("Présentation grande fenêtre", "Full window presentation"), &large))
        {
            setDesign(control, "large", large);
            push(slot, true);
        }
    }
    if (control.kind == Kind::custom)
    {
        ImGui::Separator();
        ui::sectionTitle(L("Composant C++ Dear ImGui", "Dear ImGui C++ component"));
        const auto source = designValue(control, "customSource").toString();
        if (ImGui::GetActiveID() != ImGui::GetID("##cppSource"))
            std::snprintf(cpp_.data(), cpp_.size(), "%s", source.toRawUTF8());
        ImGui::InputTextMultiline("##cppSource", cpp_.data(), cpp_.size(), ImVec2(-1, dp(260)),
                                  ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            setDesign(control, "customSource", juce::String::fromUTF8(cpp_.data()));
            push(slot, true);
        }
        if (ImGui::Button(L("Compiler le composant", "Compile component")))
        {
            setDesign(control, "customSource", juce::String::fromUTF8(cpp_.data()));
            push(slot, true);
            CustomInterface::build(juce::String::fromUTF8(cpp_.data()));
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Exemple", "Example")))
        {
            setDesign(control, "customSource", CustomInterface::example());
            push(slot, true);
        }
        const auto job = CustomInterface::job(designValue(control, "customSource").toString());
        ImGui::TextUnformatted(job->status == 1   ? L("Compilation en cours…", "Compiling…")
                               : job->status == 2 ? L("Composant compilé", "Component compiled")
                               : job->status < 0  ? L("Échec de compilation", "Compilation failed")
                                                  : L("À compiler", "Not compiled"));
        if (ImGui::CollapsingHeader(L("Diagnostics du compilateur", "Compiler diagnostics")))
            ImGui::TextWrapped("%s", CustomInterface::diagnostics(job).toRawUTF8());
        ImGui::TextWrapped("%s", L("Le code utilise CustomInterfaceABI.h. Il s'exécute sur le thread interface et "
                                   "accède aux paramètres par get/set. Visual Studio C++ et CMake sont nécessaires.",
                                   "Code uses CustomInterfaceABI.h. It runs on the UI thread and accesses parameters "
                                   "through get/set. Visual Studio C++ and CMake are required."));
    }
}
} // namespace cydaw::v2
