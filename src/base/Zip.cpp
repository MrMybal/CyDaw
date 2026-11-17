#include <cydaw/base/Zip.h>
#include <miniz/miniz.h>
namespace cydaw::base {
namespace {
struct Archive {
  FileInputStream input;
  mz_zip_archive zip{};
  bool valid = false;
  explicit Archive(const File &file) : input(file) {
    if (!input.openedOk())
      return;
    zip.m_pIO_opaque = &input;
    zip.m_pRead = [](void *opaque, mz_uint64 offset, void *data,
                     size_t size) -> size_t {
      auto &in = *static_cast<FileInputStream *>(opaque);
      if (offset > mz_uint64(INT64_MAX) || !in.setPosition(int64(offset)))
        return 0;
      auto n = in.read(data, int(std::min<size_t>(size, INT_MAX)));
      return size_t(std::max(0, n));
    };
    valid = mz_zip_reader_init(&zip, mz_uint64(input.getTotalLength()), 0) != 0;
  }
  ~Archive() {
    if (valid)
      mz_zip_reader_end(&zip);
  }
};
class EntryStream final : public InputStream {
  Archive archive_;
  mz_zip_reader_extract_iter_state *iterator_{};
  int64 length_, position_ = 0;
  bool failed_ = false;

public:
  EntryStream(const File &file, int index, int64 length)
      : archive_(file), length_(length) {
    if (archive_.valid)
      iterator_ =
          mz_zip_reader_extract_iter_new(&archive_.zip, mz_uint(index), 0);
  }
  bool valid() const { return iterator_ != nullptr; }
  ~EntryStream() {
    if (iterator_)
      mz_zip_reader_extract_iter_free(iterator_);
  }
  int read(void *data, int count) override {
    if (count <= 0)
      return 0;
    if (failed_)
      return -1;
    if (!iterator_)
      return 0;
    auto n = mz_zip_reader_extract_iter_read(iterator_, data, size_t(count));
    position_ += int64(n);
    if (position_ == length_ || n == 0) {
      auto valid = mz_zip_reader_extract_iter_free(iterator_);
      iterator_ = nullptr;
      failed_ = !valid || position_ != length_;
      if (failed_)
        return -1;
    }
    return int(n);
  }
  int64 getPosition() override { return position_; }
  int64 getTotalLength() override { return length_; }
  bool setPosition(int64 n) override { return n == position_; }
  bool isExhausted() override { return failed_ || !iterator_; }
};
} // namespace
struct ZipFile::Impl {
  File file;
  std::vector<Entry> entries;
};
ZipFile::ZipFile(const File &file) : impl_(std::make_unique<Impl>()) {
  impl_->file = file;
  Archive archive(file);
  if (!archive.valid)
    return;
  auto count = mz_zip_reader_get_num_files(&archive.zip);
  if (count > 100000)
    return;
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&archive.zip, i, &stat) ||
        stat.m_uncomp_size > mz_uint64(INT64_MAX)) {
      impl_->entries.clear();
      return;
    }
    auto size = mz_zip_reader_get_filename(&archive.zip, i, nullptr, 0);
    if (size > 1024 * 1024) {
      impl_->entries.clear();
      return;
    }
    std::vector<char> name(size + 1);
    mz_zip_reader_get_filename(&archive.zip, i, name.data(), size);
    impl_->entries.push_back({Text(name.data()), int64(stat.m_uncomp_size),
                              Time(int64(stat.m_time) * 1000)});
  }
}
ZipFile::~ZipFile() = default;
int ZipFile::getNumEntries() const { return int(impl_->entries.size()); }
int ZipFile::getIndexOfFileName(const Text &name, bool ignoreCase) const {
  for (int i = 0; i < getNumEntries(); ++i) {
    auto &e = impl_->entries[size_t(i)];
    if (ignoreCase ? e.filename.equalsIgnoreCase(name) : e.filename == name)
      return i;
  }
  return -1;
}
const ZipFile::Entry *ZipFile::getEntry(int i) const {
  return i >= 0 && i < getNumEntries() ? &impl_->entries[size_t(i)] : nullptr;
}
InputStream *ZipFile::createStreamForEntry(int i) const {
  auto *e = getEntry(i);
  if (!e)
    return nullptr;
  auto stream =
      std::make_unique<EntryStream>(impl_->file, i, e->uncompressedSize);
  return stream->valid() ? stream.release() : nullptr;
}
} // namespace cydaw::base
