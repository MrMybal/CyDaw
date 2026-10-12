#include "SampleCatalog.h"

#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace cydaw::v2 {

namespace {

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

juce::String categoryLabel(const juce::String& key, const bool french)
{
    if (key.isEmpty())
        return french ? juce::String::fromUTF8("Sans catégorie") : juce::String("Uncategorised");
    if (const auto* info = app::SampleLibrary::categoryInfo(key))
        return juce::String::fromUTF8(french ? info->french : info->english);
    return key;
}

// "Grosses caisses (kicks)": Grosses caisses.
juce::String categoryShort(const juce::String& key, const bool french)
{
    if (key.isEmpty())
        return {};
    const auto label = categoryLabel(key, french);
    if (const auto open = label.indexOfChar('('); open > 0)
        return label.substring(0, open).trim();
    return label;
}

int familyIndex(const juce::String& category)
{
    const auto family = category.isEmpty() ? juce::String("other") : app::SampleLibrary::familyOf(category);
    const auto& families = app::SampleLibrary::categoryFamilies();
    for (std::size_t index = 0; index < families.size(); ++index)
        if (family == families[index].key)
            return static_cast<int>(index);
    return static_cast<int>(families.size()) - 1;
}

int categoryOrder(const juce::String& category)
{
    if (category.isEmpty())
        return 10'000;
    const auto& taxonomy = app::SampleLibrary::categoryTaxonomy();
    for (std::size_t index = 0; index < taxonomy.size(); ++index)
        if (category == taxonomy[index].key)
            return familyIndex(category) * 100 + static_cast<int>(index);
    return 9'000;
}

// A key's place: its pitch, major before minor; none last.
int keyOrder(const juce::String& key)
{
    if (key.isEmpty())
        return 1'000;
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const auto minor = key.endsWithChar('m');
    const auto root = minor ? key.dropLastCharacters(1) : key;
    for (auto index = 0; index < 12; ++index)
        if (root == names[index] || root == flats[index])
            return index * 2 + (minor ? 1 : 0);
    return 900;
}

int tempoBucket(const float bpm)
{
    if (bpm <= 0.0f)
        return 0;
    if (bpm < 90.0f)
        return 1;
    if (bpm < 110.0f)
        return 2;
    if (bpm < 130.0f)
        return 3;
    if (bpm < 150.0f)
        return 4;
    return 5;
}

int lengthBucket(const float seconds)
{
    if (seconds < 0.5f)
        return 0;
    if (seconds < 2.0f)
        return 1;
    if (seconds < 10.0f)
        return 2;
    if (seconds < 60.0f)
        return 3;
    return 4;
}

juce::String durationText(const double seconds)
{
    char text[32];
    if (seconds < 10.0)
        std::snprintf(text, sizeof(text), "%.2f s", seconds);
    else
        std::snprintf(text, sizeof(text), "%d:%02d", static_cast<int>(seconds) / 60, static_cast<int>(seconds) % 60);
    return text;
}

// Digits compared as numbers: "kick2" before "kick10".
bool naturalLess(const std::string& a, const std::string& b)
{
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            auto ei = i;
            auto ej = j;
            while (ei < a.size() && a[ei] == '0')
                ++ei;
            while (ej < b.size() && b[ej] == '0')
                ++ej;
            auto ni = ei;
            auto nj = ej;
            while (ni < a.size() && std::isdigit(static_cast<unsigned char>(a[ni])))
                ++ni;
            while (nj < b.size() && std::isdigit(static_cast<unsigned char>(b[nj])))
                ++nj;
            if (ni - ei != nj - ej)
                return ni - ei < nj - ej;
            for (auto k = std::size_t {}; k < ni - ei; ++k)
                if (a[ei + k] != b[ej + k])
                    return a[ei + k] < b[ej + k];
            i = ni;
            j = nj;
            continue;
        }
        if (ca != cb)
            return ca < cb;
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

} // namespace

// ---- Folding -----------------------------------------------------------------------------------

std::string SampleCatalog::fold(const juce::String& text)
{
    std::string out;
    out.reserve(static_cast<std::size_t>(text.getNumBytesAsUTF8()));
    for (auto pointer = text.getCharPointer(); !pointer.isEmpty();) {
        auto c = pointer.getAndAdvance();
        if (c < 128) {
            out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
            continue;
        }
        c = juce::CharacterFunctions::toLowerCase(c);
        switch (c) {
        case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: out.push_back('a'); continue;
        case 0xe7: out.push_back('c'); continue;
        case 0xe8: case 0xe9: case 0xea: case 0xeb: out.push_back('e'); continue;
        case 0xec: case 0xed: case 0xee: case 0xef: out.push_back('i'); continue;
        case 0xf1: out.push_back('n'); continue;
        case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: out.push_back('o'); continue;
        case 0xf9: case 0xfa: case 0xfb: case 0xfc: out.push_back('u'); continue;
        case 0x153: out += "oe"; continue;
        case 0xe6: out += "ae"; continue;
        default: break;
        }
        // Anything else as its UTF-8 bytes.
        char bytes[8] {};
        juce::CharPointer_UTF8 writer(bytes);
        writer.write(c);
        writer.writeNull();
        out += bytes;
    }
    return out;
}

// ---- What is copied from the library -------------------------------------------------------------

struct SampleCatalog::Raw {
    juce::String path;
    juce::String name;
    juce::String category;
    juce::String instrument;
    juce::String source;
    juce::String root;
    juce::String type;
    juce::String key;
    juce::String classifier;
    // Made by build(), off the message thread.
    juce::String sourceKey;
    juce::String sourceTitle;
    float length {};
    float bpm {};
    int channels {};
    float rate {};
    bool favourite {};
    bool bank {};
};

struct SampleCatalog::Extract {
    std::vector<Raw> raws;
    std::map<juce::String, juce::String> titles; // bank id -> title
};

SampleCatalog::Extract SampleCatalog::extract() const
{
    Extract extract;
    const auto& samples = library_.samples();
    extract.raws.resize(samples.size());
    for (const auto& bank : library_.banks())
        extract.titles[bank.id] = bank.title.isNotEmpty() ? bank.title : bank.id;
    // Texts shared, not copied: a reference each.
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& record = samples[index];
        auto& raw = extract.raws[index];
        raw.path = record.path;
        raw.name = record.name;
        raw.category = record.category;
        raw.instrument = record.instrument;
        raw.source = record.source;
        raw.root = record.root;
        raw.type = record.type;
        raw.key = record.key;
        raw.classifier = record.classifier;
        raw.length = static_cast<float>(record.length);
        raw.bpm = static_cast<float>(record.bpm);
        raw.channels = record.channels;
        raw.rate = static_cast<float>(record.sampleRate);
        raw.favourite = record.favourite;
    }
    return extract;
}

// ---- Building --------------------------------------------------------------------------------------

std::shared_ptr<SampleCatalog::Data> SampleCatalog::build(Extract extract, const int generation)
{
    const auto started = nowMs();
    const auto french = ui::french();
    auto& raws = extract.raws;
    // Where each comes from, named (the records of one bank follow each other).
    {
        std::map<juce::String, juce::String> folders;
        juce::String lastSource;
        juce::String lastTitle;
        for (auto& raw : raws) {
            raw.bank = raw.source.isNotEmpty();
            if (raw.bank) {
                raw.sourceKey = "bank:" + raw.source;
                if (raw.source != lastSource) {
                    lastSource = raw.source;
                    const auto found = extract.titles.find(raw.source);
                    lastTitle = found != extract.titles.end() ? found->second : raw.source;
                }
                raw.sourceTitle = lastTitle;
            } else {
                raw.sourceKey = "folder:" + raw.root;
                auto& title = folders[raw.root];
                if (title.isEmpty())
                    title = juce::File(raw.root).getFileName();
                raw.sourceTitle = title;
            }
        }
    }
    auto data = std::make_shared<Data>();
    data->generation = generation;
    const auto count = raws.size();
    data->items.resize(count);
    data->haystacks.resize(count);
    data->byPath.reserve(count);
    std::array<std::unordered_map<juce::String, int>, facetCount> interned;
    // Each value with what orders it in its column.
    std::array<std::vector<double>, facetCount> orders;
    const auto intern = [&](const Facet facet, const juce::String& key, const juce::String& label, const double order,
                            const int family = -1) -> std::uint16_t {
        auto& map = interned[static_cast<std::size_t>(facet)];
        if (const auto found = map.find(key); found != map.end())
            return static_cast<std::uint16_t>(found->second);
        auto& values = data->values[static_cast<std::size_t>(facet)];
        const auto id = static_cast<int>(values.size());
        values.push_back({ key, label, family });
        orders[static_cast<std::size_t>(facet)].push_back(order);
        map.emplace(key, id);
        return static_cast<std::uint16_t>(id);
    };
    static const char* tempoLabels[] = { "", "< 90", "90 \xe2\x80\x93 109", "110 \xe2\x80\x93 129", "130 \xe2\x80\x93 149",
                                         "150 +" };
    static const char* lengthLabels[] = { "< 0,5 s", "0,5 \xe2\x80\x93 2 s", "2 \xe2\x80\x93 10 s", "10 \xe2\x80\x93 60 s",
                                          "> 1 min" };
    std::vector<std::string> foldedNames(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& raw = raws[index];
        auto& item = data->items[index];
        item.path = raw.path;
        const auto hasLetters = raw.name.containsAnyOf("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        const auto folder = juce::File(raw.path).getParentDirectory().getFileName();
        // The folders under its root, words to find it by ("Aerophones/Recorder").
        const auto folders = raw.path.length() > raw.root.length() + 1
                                 ? raw.path.substring(raw.root.length() + 1).upToLastOccurrenceOf(juce::File::getSeparatorString(), false, false)
                                       .replaceCharacters("\\/_-", "    ")
                                 : juce::String();
        item.label = hasLetters ? raw.name : raw.name + "  \xc2\xb7  " + folder;
        item.family = familyIndex(raw.category);
        item.categoryText = categoryShort(raw.category, french);
        item.groupText = raw.instrument.isNotEmpty() ? raw.instrument : raw.sourceTitle;
        item.sourceText = raw.sourceTitle;
        item.typeText = raw.type == "one-shot" ? (french ? "Coup" : "One-shot")
                        : raw.type == "loop"   ? (french ? "Boucle" : "Loop")
                        : raw.type == "long"   ? juce::String("Long")
                                               : juce::String();
        item.keyText = raw.key;
        item.length = raw.length;
        item.bpm = raw.bpm;
        item.channels = raw.channels;
        item.rate = raw.rate;
        item.favourite = raw.favourite;
        item.aiCategory = raw.classifier == "clap";
        item.bank = raw.bank;
        item.lengthText = durationText(raw.length);
        if (raw.bpm > 0.0f)
            item.bpmText = juce::String(juce::roundToInt(raw.bpm));
        if (raw.rate > 0.0f)
            item.rateText = juce::String(raw.rate / 1000.0f, raw.rate == std::floor(raw.rate / 1000.0f) * 1000.0f ? 0 : 1);

        item.facets[category] = intern(category, raw.category.isEmpty() ? juce::String("-") : raw.category,
                                       categoryLabel(raw.category, french), categoryOrder(raw.category), item.family);
        item.facets[group] = intern(group, item.groupText, item.groupText, 0.0);
        item.facets[source] = intern(source, raw.sourceKey, raw.sourceTitle, raw.bank ? 0.0 : 1.0);
        item.facets[type] = intern(type, raw.type.isEmpty() ? juce::String("-") : raw.type,
                                   item.typeText.isNotEmpty() ? item.typeText : (french ? "Sans type" : "No type"),
                                   raw.type == "one-shot" ? 0.0 : raw.type == "loop" ? 1.0 : raw.type == "long" ? 2.0 : 3.0);
        item.facets[key] = intern(key, raw.key.isEmpty() ? juce::String("-") : raw.key,
                                  raw.key.isNotEmpty() ? raw.key : juce::String::fromUTF8(french ? "Sans tonalité" : "No key"),
                                  keyOrder(raw.key));
        const auto tempoIndex = tempoBucket(raw.bpm);
        item.facets[tempo] = intern(tempo, juce::String(tempoIndex),
                                    tempoIndex == 0 ? juce::String(french ? "Sans tempo" : "No tempo")
                                                    : juce::String::fromUTF8(tempoLabels[tempoIndex]) + " BPM",
                                    tempoIndex == 0 ? 99.0 : tempoIndex);
        const auto lengthIndex = lengthBucket(raw.length);
        item.facets[length] = intern(length, juce::String(lengthIndex), juce::String::fromUTF8(lengthLabels[lengthIndex]), lengthIndex);

        data->haystacks[index] = fold(raw.name + " " + raw.category + " " + categoryLabel(raw.category, true) + " "
                                      + categoryLabel(raw.category, false) + " " + raw.type + " " + raw.key + " "
                                      + raw.instrument + " " + raw.sourceTitle + " " + folders);
        foldedNames[index] = fold(item.label);
        data->byPath.emplace(raw.path, static_cast<int>(index));
    }
    // Each column's order, and the group's by its label.
    for (auto facet = 0; facet < facetCount; ++facet) {
        auto& values = data->values[static_cast<std::size_t>(facet)];
        std::vector<int> ids(values.size());
        for (std::size_t id = 0; id < ids.size(); ++id)
            ids[id] = static_cast<int>(id);
        std::vector<std::string> labels;
        if (facet == group || facet == source) {
            labels.resize(values.size());
            for (std::size_t id = 0; id < values.size(); ++id)
                labels[id] = fold(values[id].label);
        }
        const auto& order = orders[static_cast<std::size_t>(facet)];
        std::stable_sort(ids.begin(), ids.end(), [&](const int a, const int b) {
            const auto oa = order[static_cast<std::size_t>(a)];
            const auto ob = order[static_cast<std::size_t>(b)];
            if (oa != ob)
                return oa < ob;
            if (!labels.empty())
                return naturalLess(labels[static_cast<std::size_t>(a)], labels[static_cast<std::size_t>(b)]);
            return false;
        });
        auto& ranks = data->ranks[static_cast<std::size_t>(facet)];
        ranks.assign(values.size(), 0);
        for (std::size_t place = 0; place < ids.size(); ++place)
            ranks[static_cast<std::size_t>(ids[place])] = static_cast<int>(place);
    }
    // The library in name order.
    data->byName.resize(count);
    for (std::size_t index = 0; index < count; ++index)
        data->byName[index] = static_cast<int>(index);
    std::sort(data->byName.begin(), data->byName.end(), [&foldedNames](const int a, const int b) {
        return naturalLess(foldedNames[static_cast<std::size_t>(a)], foldedNames[static_cast<std::size_t>(b)]);
    });
    data->nameRank.resize(count);
    for (std::size_t place = 0; place < count; ++place)
        data->nameRank[static_cast<std::size_t>(data->byName[place])] = static_cast<int>(place);

    // The quick list's tree: families › categories › groups, then the sources.
    {
        struct Counts {
            int count {};
            std::map<int, int> groups; // group id -> count
        };
        std::map<int, std::map<int, Counts>> families; // family -> category id -> counts
        std::map<int, int> familyCounts;
        std::map<int, int> sourceCounts;
        for (const auto& item : data->items) {
            ++familyCounts[item.family];
            auto& counts = families[item.family][item.facets[category]];
            ++counts.count;
            ++counts.groups[item.facets[group]];
            ++sourceCounts[item.facets[source]];
        }
        const auto& familyList = app::SampleLibrary::categoryFamilies();
        for (const auto& [family, categories] : families) {
            const auto& info = familyList[static_cast<std::size_t>(family)];
            TreeNode node { info.key, juce::String::fromUTF8(french ? info.french : info.english), familyCounts[family], {} };
            std::vector<int> ids;
            for (const auto& [id, counts] : categories)
                ids.push_back(id);
            std::sort(ids.begin(), ids.end(), [&data](const int a, const int b) {
                return data->ranks[category][static_cast<std::size_t>(a)] < data->ranks[category][static_cast<std::size_t>(b)];
            });
            for (const auto id : ids) {
                const auto& counts = categories.at(id);
                const auto& value = data->values[category][static_cast<std::size_t>(id)];
                TreeNode child { value.key, value.label, counts.count, {} };
                std::vector<int> groups;
                for (const auto& [groupId, groupCount] : counts.groups)
                    groups.push_back(groupId);
                std::sort(groups.begin(), groups.end(), [&data](const int a, const int b) {
                    return data->ranks[group][static_cast<std::size_t>(a)] < data->ranks[group][static_cast<std::size_t>(b)];
                });
                for (const auto groupId : groups)
                    child.children.push_back({ data->values[group][static_cast<std::size_t>(groupId)].key,
                                               data->values[group][static_cast<std::size_t>(groupId)].label, counts.groups.at(groupId), {} });
                node.children.push_back(std::move(child));
            }
            data->categoryTree.push_back(std::move(node));
        }
        TreeNode banks { "banks", french ? juce::String("Banques de CyDAW") : juce::String("CyDAW's banks"), 0, {} };
        TreeNode folders { "folders", french ? juce::String("Mes dossiers") : juce::String("My folders"), 0, {} };
        std::vector<int> ids;
        for (const auto& [id, sourceCount] : sourceCounts)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end(), [&data](const int a, const int b) {
            return data->ranks[source][static_cast<std::size_t>(a)] < data->ranks[source][static_cast<std::size_t>(b)];
        });
        for (const auto id : ids) {
            const auto& value = data->values[source][static_cast<std::size_t>(id)];
            auto& kind = value.key.startsWith("bank:") ? banks : folders;
            kind.children.push_back({ value.key, value.label, sourceCounts[id], {} });
            kind.count += sourceCounts[id];
        }
        if (!banks.children.empty())
            data->sourceTree.push_back(std::move(banks));
        if (!folders.children.empty())
            data->sourceTree.push_back(std::move(folders));
    }
    data->buildMs = nowMs() - started;
    return data;
}

int SampleCatalog::Data::find(const juce::String& path) const
{
    const auto found = byPath.find(path);
    return found != byPath.end() ? found->second : -1;
}

int SampleCatalog::Data::valueOf(const Facet facet, const juce::String& key) const
{
    const auto& list = values[static_cast<std::size_t>(facet)];
    for (std::size_t id = 0; id < list.size(); ++id)
        if (list[id].key == key)
            return static_cast<int>(id);
    return -1;
}

// ---- Querying --------------------------------------------------------------------------------------

SampleCatalog::Result SampleCatalog::run(const Data& data, const Query& query)
{
    const auto started = nowMs();
    Result result;
    for (auto facet = 0; facet < facetCount; ++facet)
        result.counts[static_cast<std::size_t>(facet)].assign(data.values[static_cast<std::size_t>(facet)].size(), 0);
    std::array<const std::vector<char>*, facetCount> selected {};
    for (auto facet = 0; facet < facetCount; ++facet) {
        const auto& chosen = query.selected[static_cast<std::size_t>(facet)];
        if (!chosen.empty() && std::find(chosen.begin(), chosen.end(), 1) != chosen.end())
            selected[static_cast<std::size_t>(facet)] = &chosen;
    }
    std::vector<std::string> words;
    {
        const auto folded = fold(query.text.trim());
        std::size_t start = 0;
        while (start < folded.size()) {
            const auto end = folded.find(' ', start);
            const auto word = folded.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!word.empty())
                words.push_back(word);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    const auto consider = [&](const int index, const float score) {
        const auto& item = data.items[static_cast<std::size_t>(index)];
        if (query.favouritesOnly && !item.favourite)
            return;
        if (!words.empty()) {
            const auto& haystack = data.haystacks[static_cast<std::size_t>(index)];
            for (const auto& word : words)
                if (haystack.find(word) == std::string::npos)
                    return;
        }
        // Which columns leave it out: none, it is shown and counted in all;
        // one, it is counted in that one (what choosing there would bring).
        auto failed = -1;
        auto failures = 0;
        for (auto facet = 0; facet < facetCount; ++facet) {
            const auto* chosen = selected[static_cast<std::size_t>(facet)];
            if (chosen == nullptr)
                continue;
            const auto value = item.facets[static_cast<std::size_t>(facet)];
            if (value >= chosen->size() || (*chosen)[value] == 0) {
                failed = facet;
                if (++failures > 1)
                    return;
            }
        }
        if (failures == 1) {
            ++result.counts[static_cast<std::size_t>(failed)][item.facets[static_cast<std::size_t>(failed)]];
            return;
        }
        for (auto facet = 0; facet < facetCount; ++facet)
            ++result.counts[static_cast<std::size_t>(facet)][item.facets[static_cast<std::size_t>(facet)]];
        result.rows.push_back(index);
        if (query.ranked != nullptr)
            result.scores.push_back(score);
    };
    if (query.ranked != nullptr) {
        for (const auto& [path, score] : *query.ranked)
            if (const auto index = data.find(path); index >= 0)
                consider(index, score);
    } else {
        result.rows.reserve(data.items.size());
        for (const auto index : data.byName)
            consider(index, 0.0f);
    }
    result.matched = static_cast<int>(result.rows.size());

    // The order asked for (the rows are in name order, or the model's).
    const auto column = query.sortColumn;
    const auto byScore = query.ranked != nullptr && (column == colScore || column == colShape);
    if (byScore || (query.ranked == nullptr && (column == colName || column == colShape))) {
        if (!query.ascending == !byScore) {
            std::reverse(result.rows.begin(), result.rows.end());
            std::reverse(result.scores.begin(), result.scores.end());
        }
    } else {
        std::vector<std::size_t> order(result.rows.size());
        for (std::size_t index = 0; index < order.size(); ++index)
            order[index] = index;
        const auto& items = data.items;
        const auto rank = [&data](const Facet facet, const Item& item) {
            return data.ranks[static_cast<std::size_t>(facet)][item.facets[static_cast<std::size_t>(facet)]];
        };
        const auto keyOf = [&](const int row) -> double {
            const auto& item = items[static_cast<std::size_t>(row)];
            switch (column) {
            case colFavourite: return item.favourite ? 0.0 : 1.0;
            case colName: return data.nameRank[static_cast<std::size_t>(row)];
            case colCategory: return rank(category, item);
            case colGroup: return rank(group, item);
            case colSource: return rank(source, item);
            case colType: return rank(type, item);
            case colLength: return item.length;
            case colBpm: return item.bpm > 0.0f ? item.bpm : 1.0e9;
            case colKey: return rank(key, item);
            case colChannels: return item.channels;
            case colRate: return item.rate;
            default: return data.nameRank[static_cast<std::size_t>(row)];
            }
        };
        std::vector<double> keys(result.rows.size());
        for (std::size_t index = 0; index < keys.size(); ++index)
            keys[index] = keyOf(result.rows[index]);
        const auto ascending = query.ascending;
        std::stable_sort(order.begin(), order.end(), [&keys, ascending](const std::size_t a, const std::size_t b) {
            return ascending ? keys[a] < keys[b] : keys[a] > keys[b];
        });
        std::vector<int> rows(order.size());
        std::vector<float> scores(result.scores.empty() ? 0 : order.size());
        for (std::size_t index = 0; index < order.size(); ++index) {
            rows[index] = result.rows[order[index]];
            if (!scores.empty())
                scores[index] = result.scores[order[index]];
        }
        result.rows = std::move(rows);
        result.scores = std::move(scores);
    }
    result.ms = nowMs() - started;
    return result;
}

// ---- Keeping it current -----------------------------------------------------------------------------

SampleCatalog::SampleCatalog(app::SampleLibrary& library) : library_(library)
{
    data_ = std::make_shared<Data>();
    library_.addListener(this);
    startTimer(100);
}

SampleCatalog::~SampleCatalog()
{
    stopTimer();
    library_.removeListener(this);
    pool_.removeAllJobs(true, 10'000);
}

void SampleCatalog::addOnChange(const void* owner, std::function<void()> callback)
{
    removeOnChange(owner);
    callbacks_.emplace_back(owner, std::move(callback));
}

void SampleCatalog::removeOnChange(const void* owner)
{
    std::erase_if(callbacks_, [owner](const auto& entry) { return entry.first == owner; });
}

std::shared_ptr<const SampleCatalog::Data> SampleCatalog::data() const
{
    const std::scoped_lock lock(mutex_);
    return data_;
}

void SampleCatalog::sampleLibraryChanged(bool)
{
    if (!dirty_)
        changedAt_ = nowMs();
    dirty_ = true;
}

void SampleCatalog::refresh()
{
    dirty_ = true;
    changedAt_ = 0.0;
    builtAt_ = 0.0;
}

void SampleCatalog::timerCallback()
{
    if (!dirty_ || building_.load() || !library_.loaded())
        return;
    const auto now = nowMs();
    // A moment after the last change; once a second while a scan brings
    // sounds in.
    if (now - changedAt_ < 120.0)
        return;
    if (library_.isScanning() && now - builtAt_ < 1'000.0)
        return;
    startBuild();
}

void SampleCatalog::startBuild()
{
    dirty_ = false;
    building_ = true;
    const auto started = nowMs();
    auto copied = std::make_shared<Extract>(extract());
    const auto extractMs = nowMs() - started;
    const auto generation = ++generation_;
    pool_.addJob([catalog = juce::WeakReference<SampleCatalog>(this), copied, generation, extractMs]() mutable {
        auto built = build(std::move(*copied), generation);
        built->extractMs = extractMs;
        std::shared_ptr<const Data> data = std::move(built);
        juce::MessageManager::callAsync([catalog, data] {
            auto* owner = catalog.get();
            if (owner == nullptr)
                return;
            {
                const std::scoped_lock lock(owner->mutex_);
                owner->data_ = data;
            }
            owner->building_ = false;
            owner->builtAt_ = nowMs();
            const auto callbacks = owner->callbacks_;
            for (const auto& [who, callback] : callbacks)
                if (callback)
                    callback();
        });
    });
}

std::shared_ptr<const SampleCatalog::Data> SampleCatalog::synthetic(const int count) const
{
    auto source = extract();
    auto& raws = source.raws;
    if (raws.empty()) {
        const auto& taxonomy = app::SampleLibrary::categoryTaxonomy();
        for (auto index = 0; index < 64; ++index) {
            Raw raw;
            raw.path = "C:\\synthetic\\sound" + juce::String(index) + ".wav";
            raw.name = "sound " + juce::String(index);
            raw.category = taxonomy[static_cast<std::size_t>(index) % taxonomy.size()].key;
            raw.root = "C:\\synthetic";
            raw.type = index % 3 == 0 ? "loop" : "one-shot";
            raw.length = 0.2f + static_cast<float>(index % 17);
            raw.bpm = index % 3 == 0 ? 90.0f + static_cast<float>(index) : 0.0f;
            raws.push_back(std::move(raw));
        }
    }
    Extract many;
    many.titles = source.titles;
    many.raws.reserve(static_cast<std::size_t>(count));
    for (auto index = 0; index < count; ++index) {
        auto raw = raws[static_cast<std::size_t>(index) % raws.size()];
        if (index >= static_cast<int>(raws.size())) {
            raw.path << "#" << index;
            raw.name << " " << index;
        }
        many.raws.push_back(std::move(raw));
    }
    return build(std::move(many), -1);
}

} // namespace cydaw::v2
