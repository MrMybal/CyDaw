#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <optional>
#include <set>

// AAF (Advanced Authoring Format), as its public specifications describe it:
// a Microsoft Compound File ([MS-CFB]: sectors, a FAT, a directory of
// storages and streams) holding AAF objects — each object a storage whose
// class is the storage's CLSID and whose properties are its "properties"
// stream (a byte order, a count, then each property's id, stored form and
// length, then their values). A strong reference names a child storage; a
// vector or a set of them names an index stream ("<name> index") and
// children "<name>{<key in hex>}".
//
// CyDAW reads the top-level composition: each sound track (a TimelineMobSlot
// of sound) and its clips, each clip followed from the composition to its
// master mob and to the source mob that holds the file — found where its
// locator says (or by its name beside the AAF), or embedded in the AAF and
// written out. Transitions become fades; a clip made of two mono sources
// (left, right) becomes one stereo file. Pro Tools and Logic export their
// sessions this way; so do Cubase, Nuendo, Studio One and the video editors.

namespace cydaw::app::project_import {
namespace {

using namespace shared;

// ---- The compound file ---------------------------------------------------------------------

class CompoundFile {
public:
    struct Entry {
        juce::String name;
        int type {};          // 1 storage, 2 stream, 5 root
        juce::uint32 left { kNone };
        juce::uint32 right { kNone };
        juce::uint32 child { kNone };
        std::array<juce::uint8, 16> clsid {};
        juce::uint32 start {};
        juce::uint64 size {};
    };
    static constexpr juce::uint32 kNone = 0xffffffffu;

    juce::Result open(const juce::File& file)
    {
        map_ = std::make_unique<juce::MemoryMappedFile>(file, juce::MemoryMappedFile::readOnly);
        if (map_->getData() == nullptr || map_->getSize() < 512)
            return juce::Result::fail("The file could not be read: " + file.getFullPathName());
        data_ = static_cast<const juce::uint8*>(map_->getData());
        size_ = map_->getSize();
        static constexpr juce::uint8 signature[] { 0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1 };
        if (std::memcmp(data_, signature, 8) != 0)
            return juce::Result::fail("Not an AAF file (no compound file signature): " + file.getFileName());
        sectorShift_ = u16(0x1e);
        miniShift_ = u16(0x20);
        if (sectorShift_ < 7 || sectorShift_ > 16)
            return juce::Result::fail("The AAF file's sectors are of an unknown size");
        const auto fatSectors = u32(0x2c);
        const auto firstDirectory = u32(0x30);
        miniCutoff_ = u32(0x38);
        const auto firstMiniFat = u32(0x3c);
        auto difatSector = u32(0x44);
        // The FAT, from the header's 109 DIFAT entries and the DIFAT chain.
        std::vector<juce::uint32> fatList;
        for (int index = 0; index < 109 && fatList.size() < fatSectors; ++index)
            fatList.push_back(u32(0x4c + static_cast<std::size_t>(index) * 4));
        const auto perSector = sectorSize() / 4;
        for (auto guard = 0; difatSector < 0xfffffffau && fatList.size() < fatSectors && guard < 1'000'000; ++guard) {
            const auto base = offsetOf(difatSector);
            if (base + sectorSize() > size_)
                break;
            for (std::size_t index = 0; index + 1 < perSector && fatList.size() < fatSectors; ++index)
                fatList.push_back(u32(base + index * 4));
            difatSector = u32(base + (perSector - 1) * 4);
        }
        for (const auto sector : fatList) {
            const auto base = offsetOf(sector);
            if (base + sectorSize() > size_)
                return juce::Result::fail("The AAF file's FAT is cut short");
            for (std::size_t index = 0; index < perSector; ++index)
                fat_.push_back(u32(base + index * 4));
        }
        // The directory.
        const auto directory = readChain(firstDirectory, false, 0);
        for (std::size_t at = 0; at + 128 <= directory.getSize(); at += 128) {
            const auto* raw = static_cast<const juce::uint8*>(directory.getData()) + at;
            Entry entry;
            const auto nameBytes = std::min<int>(64, raw[64] | (raw[65] << 8));
            juce::String name;
            for (int index = 0; index + 1 < nameBytes; index += 2) {
                const auto unit = static_cast<juce::juce_wchar>(raw[index] | (raw[index + 1] << 8));
                if (unit == 0)
                    break;
                name += juce::String::charToString(unit);
            }
            entry.name = name;
            entry.type = raw[66];
            entry.left = read32(raw + 68);
            entry.right = read32(raw + 72);
            entry.child = read32(raw + 76);
            std::memcpy(entry.clsid.data(), raw + 80, 16);
            entry.start = read32(raw + 116);
            entry.size = static_cast<juce::uint64>(read32(raw + 120)) | (static_cast<juce::uint64>(read32(raw + 124)) << 32);
            if (sectorShift_ == 9)
                entry.size &= 0xffffffffu;
            entries_.push_back(std::move(entry));
        }
        if (entries_.empty() || entries_[0].type != 5)
            return juce::Result::fail("The AAF file holds no root storage");
        // The mini stream (the root's stream) and its FAT.
        const auto miniFat = readChain(firstMiniFat, false, 0);
        for (std::size_t at = 0; at + 4 <= miniFat.getSize(); at += 4)
            miniFat_.push_back(read32(static_cast<const juce::uint8*>(miniFat.getData()) + at));
        miniStream_ = readChain(entries_[0].start, false, entries_[0].size);
        return juce::Result::ok();
    }

    [[nodiscard]] const Entry& entry(const int index) const { return entries_[static_cast<std::size_t>(index)]; }

    // A storage's children by name.
    [[nodiscard]] std::map<juce::String, int> children(const int storage) const
    {
        std::map<juce::String, int> found;
        std::vector<juce::uint32> pending { entries_[static_cast<std::size_t>(storage)].child };
        while (!pending.empty()) {
            const auto at = pending.back();
            pending.pop_back();
            if (at >= entries_.size() || found.size() > entries_.size())
                continue;
            const auto& item = entries_[at];
            found[item.name.toLowerCase()] = static_cast<int>(at);
            pending.push_back(item.left);
            pending.push_back(item.right);
        }
        return found;
    }

    [[nodiscard]] juce::MemoryBlock read(const int index) const
    {
        const auto& item = entries_[static_cast<std::size_t>(index)];
        if (item.size < miniCutoff_ && item.type == 2)
            return readChain(item.start, true, item.size);
        return readChain(item.start, false, item.size);
    }

private:
    [[nodiscard]] std::size_t sectorSize() const { return static_cast<std::size_t>(1) << sectorShift_; }
    [[nodiscard]] std::size_t offsetOf(const juce::uint32 sector) const { return (static_cast<std::size_t>(sector) + 1) << sectorShift_; }
    [[nodiscard]] static juce::uint32 read32(const juce::uint8* at)
    {
        return static_cast<juce::uint32>(at[0]) | (static_cast<juce::uint32>(at[1]) << 8) | (static_cast<juce::uint32>(at[2]) << 16)
               | (static_cast<juce::uint32>(at[3]) << 24);
    }
    [[nodiscard]] juce::uint32 u32(const std::size_t at) const { return at + 4 <= size_ ? read32(data_ + at) : kNone; }
    [[nodiscard]] juce::uint16 u16(const std::size_t at) const
    {
        return at + 2 <= size_ ? static_cast<juce::uint16>(data_[at] | (data_[at + 1] << 8)) : 0;
    }

    // A chain of sectors (or of mini sectors, in the mini stream), up to
    // `size` bytes (0: the whole chain).
    [[nodiscard]] juce::MemoryBlock readChain(juce::uint32 sector, const bool mini, const juce::uint64 size) const
    {
        juce::MemoryOutputStream out;
        const auto& table = mini ? miniFat_ : fat_;
        const auto unit = mini ? (static_cast<std::size_t>(1) << miniShift_) : sectorSize();
        std::set<juce::uint32> seen;
        while (sector < 0xfffffffau && (size == 0 || out.getDataSize() < size)) {
            if (!seen.insert(sector).second)
                break;
            if (mini) {
                const auto at = static_cast<std::size_t>(sector) * unit;
                if (at + unit > miniStream_.getSize())
                    break;
                out.write(static_cast<const juce::uint8*>(miniStream_.getData()) + at, unit);
            } else {
                const auto at = offsetOf(sector);
                if (at >= size_)
                    break;
                out.write(data_ + at, std::min(unit, size_ - at));
            }
            if (sector >= table.size())
                break;
            sector = table[sector];
        }
        auto block = out.getMemoryBlock();
        if (size > 0 && block.getSize() > size)
            block.setSize(static_cast<std::size_t>(size));
        return block;
    }

    std::unique_ptr<juce::MemoryMappedFile> map_;
    const juce::uint8* data_ {};
    std::size_t size_ {};
    int sectorShift_ { 9 };
    int miniShift_ { 6 };
    juce::uint32 miniCutoff_ { 4'096 };
    std::vector<juce::uint32> fat_;
    std::vector<juce::uint32> miniFat_;
    std::vector<Entry> entries_;
    juce::MemoryBlock miniStream_;
};

// ---- AAF objects ---------------------------------------------------------------------------

namespace form {
constexpr int data = 0x82;
constexpr int dataStream = 0x42;
constexpr int strongReference = 0x22;
constexpr int strongVector = 0x32;
constexpr int strongSet = 0x3a;
constexpr int weakReference = 0x02;
} // namespace form

namespace pid {
constexpr int headerContent = 0x3b03;
constexpr int mobs = 0x1901;
constexpr int essenceData = 0x1902;
constexpr int mobId = 0x4401;
constexpr int mobName = 0x4402;
constexpr int slots = 0x4403;
constexpr int usage = 0x4409;
constexpr int slotId = 0x4801;
constexpr int slotName = 0x4802;
constexpr int segment = 0x4803;
constexpr int editRate = 0x4b01;
constexpr int dataDefinition = 0x0201;
constexpr int length = 0x0202;
constexpr int components = 0x1001;
constexpr int sourceId = 0x1101;
constexpr int sourceSlot = 0x1102;
constexpr int startTime = 0x1201;
constexpr int fadeIn = 0x1202;
constexpr int fadeOut = 0x1204;
constexpr int transitionGroup = 0x1801;
constexpr int operation = 0x0b01;
constexpr int inputs = 0x0b02;
constexpr int parameters = 0x0b03;
constexpr int essenceDescription = 0x4701;
constexpr int locators = 0x2f01;
constexpr int sampleRate = 0x3001;
constexpr int urlString = 0x4001;
constexpr int essenceMob = 0x2701;
constexpr int essenceStream = 0x2702;
constexpr int fileDescriptors = 0x3f01;
constexpr int audioRate = 0x3d03;
constexpr int quantization = 0x3d01;
constexpr int channels = 0x3d07;
constexpr int waveSummary = 0x3801;
constexpr int aifcSummary = 0x3101;
} // namespace pid

struct Object {
    int storage { -1 };
    juce::uint16 classId {}; // the class AUID's third field (0x3500: a CompositionMob…)
    std::map<int, std::pair<int, juce::MemoryBlock>> properties; // pid: (stored form, value)

    [[nodiscard]] bool has(const int key) const { return properties.contains(key); }
    [[nodiscard]] const juce::MemoryBlock* value(const int key) const
    {
        const auto found = properties.find(key);
        return found != properties.end() ? &found->second.second : nullptr;
    }
    [[nodiscard]] juce::int64 integer(const int key, const juce::int64 fallback = 0) const
    {
        const auto* block = value(key);
        if (block == nullptr || block->getSize() == 0)
            return fallback;
        juce::int64 result = 0;
        const auto* bytes = static_cast<const juce::uint8*>(block->getData());
        const auto count = std::min<std::size_t>(8, block->getSize());
        for (std::size_t index = 0; index < count; ++index)
            result |= static_cast<juce::int64>(bytes[index]) << (8 * index);
        // Sign-extend a shorter value.
        if (count < 8 && (bytes[count - 1] & 0x80) != 0)
            result |= ~((static_cast<juce::int64>(1) << (8 * count)) - 1);
        return result;
    }
    [[nodiscard]] double rational(const int key, const double fallback) const
    {
        const auto* block = value(key);
        if (block == nullptr || block->getSize() < 8)
            return fallback;
        const auto* bytes = static_cast<const juce::uint8*>(block->getData());
        const auto numerator = static_cast<juce::int32>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
        const auto denominator = static_cast<juce::int32>(bytes[4] | (bytes[5] << 8) | (bytes[6] << 16) | (bytes[7] << 24));
        return denominator != 0 && numerator != 0 ? static_cast<double>(numerator) / denominator : fallback;
    }
    [[nodiscard]] juce::String text(const int key) const
    {
        const auto* block = value(key);
        if (block == nullptr)
            return {};
        juce::String result;
        const auto* bytes = static_cast<const juce::uint8*>(block->getData());
        for (std::size_t index = 0; index + 1 < block->getSize(); index += 2) {
            const auto unit = static_cast<juce::juce_wchar>(bytes[index] | (bytes[index + 1] << 8));
            if (unit == 0)
                break;
            result += juce::String::charToString(unit);
        }
        return result;
    }
    // A weak reference's key: the AUID it names.
    [[nodiscard]] juce::MemoryBlock referenceKey(const int key) const
    {
        const auto* block = value(key);
        if (block == nullptr || block->getSize() < 5)
            return {};
        const auto* bytes = static_cast<const juce::uint8*>(block->getData());
        const auto size = std::min<std::size_t>(bytes[4], block->getSize() - 5);
        return juce::MemoryBlock(bytes + 5, size);
    }
};

class AafReader {
public:
    AafReader(const juce::File& file, ImportedProject& out) : file_(file), out_(out), folder_(file.getParentDirectory()) {}

    juce::Result read()
    {
        if (const auto opened = cfb_.open(file_); opened.failed())
            return opened;
        out_.format = "aaf";
        out_.application = "AAF";
        // The header: a storage of the root named after its property
        // ("Header-2").
        auto header = -1;
        for (const auto& [name, index] : cfb_.children(0))
            if (name == "header" || name.startsWith("header-"))
                header = index;
        if (header < 0)
            return juce::Result::fail("The AAF file holds no header");
        const auto headerObject = load(header);
        const auto content = strongChild(headerObject, pid::headerContent);
        if (!content)
            return juce::Result::fail("The AAF file's header holds no content");
        application(header);
        for (const auto& mob : strongChildren(*content, pid::mobs)) {
            const auto* id = mob.value(pid::mobId);
            if (id != nullptr)
                mobs_[id->toBase64Encoding()] = mob;
        }
        for (const auto& essence : strongChildren(*content, pid::essenceData))
            if (const auto* id = essence.value(pid::essenceMob))
                essences_[id->toBase64Encoding()] = essence;
        // The top-level composition (its usage says so), else the one with
        // the most slots.
        const Object* composition = nullptr;
        for (const auto& [key, mob] : mobs_) {
            if (mob.classId != 0x3500)
                continue;
            const auto usage = mob.referenceKey(pid::usage);
            const auto topLevel = usage.getSize() >= 8 && static_cast<const juce::uint8*>(usage.getData())[6] == 0x00
                                  && static_cast<const juce::uint8*>(usage.getData())[7] == 0x05;
            if (composition == nullptr || topLevel
                || (strongChildren(mob, pid::slots).size() > strongChildren(*composition, pid::slots).size()
                    && composition->referenceKey(pid::usage).getSize() == 0))
                composition = &mob;
            if (topLevel)
                break;
        }
        if (composition == nullptr)
            return juce::Result::fail("The AAF file holds no composition");
        out_.name = composition->text(pid::mobName);
        auto number = 0;
        for (const auto& slot : strongChildren(*composition, pid::slots)) {
            const auto segment = strongChild(slot, pid::segment);
            if (!segment || !isSound(*segment))
                continue;
            ImportedTrack track;
            track.name = slot.text(pid::slotName);
            if (track.name.isEmpty())
                track.name = "Audio " + juce::String(++number);
            const auto rate = slot.rational(pid::editRate, 48'000.0);
            walk(*segment, 0, rate, track);
            out_.tracks.push_back(std::move(track));
        }
        // Pro Tools (and others) start their timeline at 01:00:00:00: the
        // clips come back near the start.
        auto earliest = std::numeric_limits<double>::max();
        for (const auto& track : out_.tracks)
            for (const auto& clip : track.audioClips)
                earliest = std::min(earliest, clip.startSeconds);
        if (earliest >= 3'600.0 && earliest < std::numeric_limits<double>::max()) {
            const auto hours = std::floor(earliest / 3'600.0);
            for (auto& track : out_.tracks)
                for (auto& clip : track.audioClips)
                    clip.startSeconds -= hours * 3'600.0;
            out_.warnings.add("The timeline started at " + juce::String(static_cast<int>(hours)).paddedLeft('0', 2)
                              + ":00:00:00; it now starts at 0.");
        }
        if (gains_)
            out_.warnings.add("Clip and track gain and pan changes were left out.");
        return juce::Result::ok();
    }

private:
    // An object: its class and properties.
    Object load(const int storage) const
    {
        Object object;
        object.storage = storage;
        const auto& entry = cfb_.entry(storage);
        object.classId = static_cast<juce::uint16>(entry.clsid[6] | (entry.clsid[7] << 8));
        const auto children = cfb_.children(storage);
        const auto stream = children.find("properties");
        if (stream == children.end())
            return object;
        const auto block = cfb_.read(stream->second);
        const auto* bytes = static_cast<const juce::uint8*>(block.getData());
        if (block.getSize() < 4)
            return object;
        const auto count = static_cast<int>(bytes[2] | (bytes[3] << 8));
        std::size_t value = 4 + static_cast<std::size_t>(count) * 6;
        for (int index = 0; index < count; ++index) {
            const auto* at = bytes + 4 + index * 6;
            const auto key = at[0] | (at[1] << 8);
            const auto stored = at[2] | (at[3] << 8);
            const auto size = static_cast<std::size_t>(at[4] | (at[5] << 8));
            if (value + size > block.getSize())
                break;
            object.properties[key] = { stored, juce::MemoryBlock(bytes + value, size) };
            value += size;
        }
        return object;
    }

    // The UTF-16 name a reference holds.
    static juce::String nameIn(const juce::MemoryBlock& block, const std::size_t from = 0)
    {
        juce::String name;
        const auto* bytes = static_cast<const juce::uint8*>(block.getData());
        for (auto index = from; index + 1 < block.getSize(); index += 2) {
            const auto unit = static_cast<juce::juce_wchar>(bytes[index] | (bytes[index + 1] << 8));
            if (unit == 0)
                break;
            name += juce::String::charToString(unit);
        }
        return name;
    }

    std::optional<Object> strongChild(const Object& object, const int key) const
    {
        const auto found = object.properties.find(key);
        if (found == object.properties.end() || found->second.first != form::strongReference)
            return std::nullopt;
        const auto children = cfb_.children(object.storage);
        const auto child = children.find(nameIn(found->second.second).toLowerCase());
        if (child == children.end())
            return std::nullopt;
        return load(child->second);
    }

    std::vector<Object> strongChildren(const Object& object, const int key) const
    {
        std::vector<Object> list;
        const auto found = object.properties.find(key);
        if (found == object.properties.end())
            return list;
        const auto stored = found->second.first;
        if (stored != form::strongVector && stored != form::strongSet)
            return list;
        const auto name = nameIn(found->second.second);
        const auto children = cfb_.children(object.storage);
        const auto indexStream = children.find((name + " index").toLowerCase());
        if (indexStream == children.end())
            return list;
        const auto index = cfb_.read(indexStream->second);
        const auto* bytes = static_cast<const juce::uint8*>(index.getData());
        if (index.getSize() < 12)
            return list;
        const auto read32 = [&](const std::size_t at) {
            return at + 4 <= index.getSize() ? static_cast<juce::uint32>(bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16)
                                                                         | (bytes[at + 3] << 24))
                                             : 0u;
        };
        const auto count = read32(0);
        std::size_t at = 12;
        std::size_t stride = 4;
        if (stored == form::strongSet) {
            // The key's property and size, then (local key, count, key) each.
            if (index.getSize() < 15)
                return list;
            stride = 8 + bytes[14];
            at = 15;
        }
        for (juce::uint32 item = 0; item < count && at + 4 <= index.getSize(); ++item, at += stride) {
            const auto local = read32(at);
            const auto child = children.find((name + "{" + juce::String::toHexString(static_cast<int>(local)) + "}").toLowerCase());
            if (child != children.end())
                list.push_back(load(child->second));
        }
        return list;
    }

    // Who wrote it: the last identification's product.
    void application(const int header)
    {
        const auto headerObject = load(header);
        const auto identifications = strongChildren(headerObject, 0x3b06);
        if (identifications.empty())
            return;
        const auto& last = identifications.back();
        const auto company = last.text(0x3c01);
        const auto product = last.text(0x3c02);
        if (product.isNotEmpty())
            out_.application = (company.isNotEmpty() ? company + " " : juce::String()) + product + " (AAF)";
    }

    // A segment of sound: its data definition says so (the legacy and the
    // SMPTE identifiers of Sound), or what it holds is sound.
    bool isSound(const Object& segment) const
    {
        const auto key = segment.referenceKey(pid::dataDefinition);
        if (key.getSize() >= 8) {
            const auto* bytes = static_cast<const juce::uint8*>(key.getData());
            const auto first = static_cast<juce::uint32>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
            const auto second = bytes[4] | (bytes[5] << 8);
            const auto third = bytes[6] | (bytes[7] << 8);
            // Sound: the legacy identifier, or SMPTE's (urn:smpte:ul:060e2b34.04010101.01030202.02000000, its last
            // eight bytes first as an AUID's fields).
            if (first == 0x78e1ebe1u || (first == 0x01030202u && second == 0x0200 && third == 0x0000))
                return true;
        }
        return false;
    }

    struct Source {
        juce::File file;
        double offsetSeconds {};
        juce::String name;
    };

    // The file a mob's slot plays at `offset` seconds into it.
    std::vector<Source> resolve(const juce::MemoryBlock& mobId, const juce::int64 slotId, const double offset, const int depth = 0)
    {
        if (depth > 8)
            return {};
        const auto found = mobs_.find(mobId.toBase64Encoding());
        if (found == mobs_.end())
            return {};
        const auto& mob = found->second;
        if (mob.has(pid::essenceDescription)) {
            if (auto file = essenceFile(mob); file != juce::File())
                return { { file, offset, mob.text(pid::mobName) } };
            // Its file is not here: a source by its name still (reported missing).
            if (!hasDeeperSource(mob))
                return { { {}, offset, mob.text(pid::mobName) } };
        }
        for (const auto& slot : strongChildren(mob, pid::slots)) {
            if (slot.integer(pid::slotId) != slotId && mob.classId != 0x3700)
                continue;
            const auto rate = slot.rational(pid::editRate, 48'000.0);
            auto segment = strongChild(slot, pid::segment);
            if (!segment)
                continue;
            // A sequence: the clip at the offset.
            auto within = offset;
            if (segment->has(pid::components)) {
                auto at = 0.0;
                std::optional<Object> chosen;
                for (auto& component : strongChildren(*segment, pid::components)) {
                    const auto length = static_cast<double>(component.integer(pid::length)) / rate;
                    if (offset < at + length + 1.0e-9) {
                        chosen = component;
                        within = offset - at;
                        break;
                    }
                    at += length;
                }
                if (!chosen)
                    return {};
                segment = chosen;
            }
            if (segment->has(pid::inputs)) {
                // Channels combined: each its source.
                std::vector<Source> sources;
                for (const auto& input : strongChildren(*segment, pid::inputs))
                    if (const auto* id = input.value(pid::sourceId)) {
                        auto more = resolve(*id, input.integer(pid::sourceSlot),
                                            within + static_cast<double>(input.integer(pid::startTime)) / rate, depth + 1);
                        sources.insert(sources.end(), more.begin(), more.end());
                    }
                return sources;
            }
            if (const auto* id = segment->value(pid::sourceId); id != nullptr && segment->has(pid::startTime))
                return resolve(*id, segment->integer(pid::sourceSlot), within + static_cast<double>(segment->integer(pid::startTime)) / rate,
                               depth + 1);
            if (mob.has(pid::essenceDescription))
                break;
        }
        return {};
    }

    // A source mob that is the copy of another (a file of a recording, of a
    // tape): its slot names one more source.
    bool hasDeeperSource(const Object& mob) const
    {
        for (const auto& slot : strongChildren(mob, pid::slots))
            if (const auto segment = strongChild(slot, pid::segment))
                if (const auto* id = segment->value(pid::sourceId); id != nullptr && mobs_.contains(id->toBase64Encoding()))
                    return true;
        return false;
    }

    // A source mob's file: where its locator says, else its essence embedded
    // in the AAF (written out once).
    juce::File essenceFile(const Object& mob)
    {
        const auto* id = mob.value(pid::mobId);
        const auto key = id != nullptr ? id->toBase64Encoding() : juce::String();
        if (const auto known = files_.find(key); known != files_.end())
            return known->second;
        auto descriptor = strongChild(mob, pid::essenceDescription);
        juce::File file;
        juce::String locator;
        if (descriptor) {
            auto described = *descriptor;
            if (described.has(pid::fileDescriptors))
                if (const auto parts = strongChildren(described, pid::fileDescriptors); !parts.empty())
                    described = parts.front();
            // Its locators (a multiple descriptor's are its parts').
            auto places = strongChildren(*descriptor, pid::locators);
            if (places.empty())
                places = strongChildren(described, pid::locators);
            for (const auto& place : places) {
                locator = place.text(pid::urlString);
                if (locator.isNotEmpty())
                    if ((file = resolveProjectAudioFile(folder_, locator)) != juce::File())
                        break;
            }
            if (file == juce::File())
                file = extract(mob, described, key);
        }
        if (file == juce::File() && locator.isNotEmpty())
            out_.missingFiles.addIfNotAlreadyThere(locator);
        files_[key] = file;
        return file;
    }

    // An essence stored in the AAF, as a WAV or AIFF file beside the AAF
    // (or in CyDAW's folder for imported audio).
    juce::File extract(const Object& mob, const Object& descriptor, const juce::String& key)
    {
        const auto found = essences_.find(key);
        if (found == essences_.end())
            return {};
        const auto& essence = found->second;
        const auto stream = essence.properties.find(pid::essenceStream);
        if (stream == essence.properties.end())
            return {};
        const auto children = cfb_.children(essence.storage);
        const auto streamName = nameIn(stream->second.second, stream->second.second.getSize() % 2 == 1 ? 1 : 0).toLowerCase();
        const auto data = children.find(streamName);
        if (data == children.end())
            return {};
        const auto block = cfb_.read(data->second);
        auto where = folder_.getChildFile("CyDAW Imported Audio");
        if (where.createDirectory().failed())
            where = importedAudioFolder().getChildFile(juce::File::createLegalFileName(file_.getFileNameWithoutExtension()));
        (void) where.createDirectory();
        auto name = juce::File::createLegalFileName(mob.text(pid::mobName));
        if (name.isEmpty())
            name = "Essence";
        const auto* bytes = static_cast<const juce::uint8*>(block.getData());
        const auto riff = block.getSize() >= 12 && std::memcmp(bytes, "RIFF", 4) == 0;
        const auto form = block.getSize() >= 12 && std::memcmp(bytes, "FORM", 4) == 0;
        // Named after its mob (its name and its id): the same essence is the
        // same file, written once.
        const auto tag = juce::String::toHexString(static_cast<juce::int64>(juce::DefaultHashFunctions::generateHash(key, 0x7fffffff)));
        auto target = where.getChildFile(name + " " + tag + (form ? ".aif" : ".wav"));
        if (target.existsAsFile() && target.getSize() >= static_cast<juce::int64>(block.getSize()))
            return target;
        if (riff || form) {
            if (!target.replaceWithData(block.getData(), block.getSize()))
                return {};
            return target;
        }
        // Raw samples: their format from the descriptor (or from a WAVE
        // descriptor's summary, a RIFF header).
        auto rate = descriptor.rational(pid::audioRate, 48'000.0);
        auto channelCount = static_cast<int>(descriptor.integer(pid::channels, 1));
        auto bits = static_cast<int>(descriptor.integer(pid::quantization, 16));
        if (const auto* summary = descriptor.value(pid::waveSummary); summary != nullptr && summary->getSize() >= 36) {
            const auto* header = static_cast<const juce::uint8*>(summary->getData());
            for (std::size_t at = 12; at + 24 <= summary->getSize(); ++at)
                if (std::memcmp(header + at, "fmt ", 4) == 0) {
                    channelCount = header[at + 10] | (header[at + 11] << 8);
                    rate = static_cast<double>(header[at + 12] | (header[at + 13] << 8) | (header[at + 14] << 16) | (header[at + 15] << 24));
                    bits = header[at + 22] | (header[at + 23] << 8);
                    break;
                }
        }
        if (channelCount < 1 || channelCount > 8 || (bits != 8 && bits != 16 && bits != 24 && bits != 32) || rate < 1'000.0)
            return {};
        juce::MemoryOutputStream wav;
        const auto dataSize = static_cast<juce::uint32>(block.getSize());
        const auto blockAlign = static_cast<juce::uint16>(channelCount * bits / 8);
        wav.write("RIFF", 4);
        wav.writeInt(static_cast<int>(36 + dataSize));
        wav.write("WAVEfmt ", 8);
        wav.writeInt(16);
        wav.writeShort(1);
        wav.writeShort(static_cast<short>(channelCount));
        wav.writeInt(static_cast<int>(rate));
        wav.writeInt(static_cast<int>(rate * blockAlign));
        wav.writeShort(static_cast<short>(blockAlign));
        wav.writeShort(static_cast<short>(bits));
        wav.write("data", 4);
        wav.writeInt(static_cast<int>(dataSize));
        wav.write(block.getData(), block.getSize());
        if (!target.replaceWithData(wav.getData(), wav.getDataSize()))
            return {};
        return target;
    }

    // A clip's sources made one file: the same stereo file twice is that
    // file; two mono files (left, right) are joined.
    juce::File fileOf(const std::vector<Source>& sources, const juce::String& name)
    {
        if (sources.empty() || std::any_of(sources.begin(), sources.end(), [](const Source& source) { return source.file == juce::File(); }))
            return {};
        if (sources.size() == 1 || sources[0].file == sources[1].file)
            return sources[0].file;
        if (sources.size() > 2)
            out_.warnings.addIfNotAlreadyThere("Clips of more than two channels keep their first two.");
        auto left = sources[0];
        auto right = sources[1];
        // Named ".L" and ".R": in that order.
        if (left.name.endsWithIgnoreCase(".R") && right.name.endsWithIgnoreCase(".L"))
            std::swap(left, right);
        return joinChannels(left.file, right.file, out_.name.isNotEmpty() ? out_.name : file_.getFileNameWithoutExtension(),
                            name.isNotEmpty() ? name : left.name.upToLastOccurrenceOf(".", false, false), out_);
    }

    // A composition slot's segment, from `position` (edit units).
    juce::int64 walk(const Object& segment, juce::int64 position, const double rate, ImportedTrack& track)
    {
        if (segment.has(pid::components)) {
            // The clip before (by its place: the list grows), where it ends.
            std::optional<std::size_t> previous;
            auto previousEnd = juce::int64 {};
            juce::int64 transition = 0;
            for (const auto& component : strongChildren(segment, pid::components)) {
                const auto length = component.integer(pid::length);
                if (component.has(pid::transitionGroup)) {
                    // Overlaps the components on either side: a fade out of
                    // the one before (when it ends there), in of the next.
                    position -= length;
                    transition = length;
                    if (previous && previousEnd == position + length)
                        track.audioClips[*previous].fadeOutSeconds
                            = std::max(track.audioClips[*previous].fadeOutSeconds, static_cast<double>(length) / rate);
                    continue;
                }
                const auto before = track.audioClips.size();
                walk(component, position, rate, track);
                if (track.audioClips.size() > before) {
                    if (transition > 0)
                        track.audioClips[before].fadeInSeconds = std::max(track.audioClips[before].fadeInSeconds,
                                                                          static_cast<double>(transition) / rate);
                    previous = track.audioClips.size() - 1;
                    previousEnd = position + length;
                } else {
                    previous.reset();
                }
                transition = 0;
                position += length;
            }
            return position;
        }
        if (segment.has(pid::inputs)) {
            const auto inputs = strongChildren(segment, pid::inputs);
            if (segment.has(pid::parameters))
                gains_ = true;
            // Channels combined (each input a clip of one channel), or an
            // effect over the first input.
            const auto clips = std::count_if(inputs.begin(), inputs.end(), [](const Object& input) { return input.has(pid::sourceId); });
            if (clips >= 2 && clips == static_cast<std::ptrdiff_t>(inputs.size())) {
                std::vector<Source> sources;
                for (const auto& input : inputs) {
                    auto more = resolve(*input.value(pid::sourceId), input.integer(pid::sourceSlot),
                                        static_cast<double>(input.integer(pid::startTime)) / rate);
                    sources.insert(sources.end(), more.begin(), more.end());
                }
                // As long as the group (its inputs may say less).
                place(inputs.front(), sources, position, rate, track, segment.integer(pid::length));
            } else if (!inputs.empty()) {
                walk(inputs.front(), position, rate, track);
            }
            return position + segment.integer(pid::length);
        }
        if (const auto* id = segment.value(pid::sourceId); id != nullptr && segment.has(pid::startTime)) {
            const auto sources = resolve(*id, segment.integer(pid::sourceSlot), static_cast<double>(segment.integer(pid::startTime)) / rate);
            place(segment, sources, position, rate, track);
        }
        return position + segment.integer(pid::length);
    }

    void place(const Object& clipObject, const std::vector<Source>& sources, const juce::int64 position, const double rate,
               ImportedTrack& track, const juce::int64 groupLength = 0)
    {
        const auto length = groupLength > 0 ? groupLength : clipObject.integer(pid::length);
        if (length <= 0)
            return;
        ImportedAudioClip clip;
        clip.name = sources.empty() ? juce::String("Clip") : sources.front().name;
        clip.file = fileOf(sources, clip.name);
        clip.originalPath = clip.name;
        clip.startSeconds = static_cast<double>(position) / rate;
        clip.durationSeconds = static_cast<double>(length) / rate;
        clip.sourceOffsetSeconds = sources.empty() ? 0.0 : std::max(0.0, sources.front().offsetSeconds);
        clip.fadeInSeconds = static_cast<double>(clipObject.integer(pid::fadeIn)) / rate;
        clip.fadeOutSeconds = static_cast<double>(clipObject.integer(pid::fadeOut)) / rate;
        if (clip.file == juce::File() && sources.empty())
            out_.missingFiles.addIfNotAlreadyThere(clip.name);
        track.audioClips.push_back(std::move(clip));
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
    CompoundFile cfb_;
    std::map<juce::String, Object> mobs_;
    std::map<juce::String, Object> essences_;
    std::map<juce::String, juce::File> files_;
    bool gains_ {};
};

} // namespace

juce::Result importAafComposition(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    AafReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
