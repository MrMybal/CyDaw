#include <cydaw/base/FileDialog.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shobjidl.h>
#include <windows.h>
#endif
namespace cydaw::base {
struct FileDialog::State {
  bool alive = true;
#if defined(_WIN32)
  IFileDialog *dialog{};
#endif
};
FileDialog::FileDialog(Text title, File initial, Text filter)
    : state_(std::make_shared<State>()), title_(std::move(title)),
      filter_(std::move(filter)), initial_(std::move(initial)) {}
FileDialog::~FileDialog() { cancel(); }
void FileDialog::cancel() {
  state_->alive = false;
#if defined(_WIN32)
  if (state_->dialog)
    state_->dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
#endif
}
void FileDialog::launchAsync(int flags,
                             std::function<void(const FileDialog &)> callback) {
  cancel();
  state_ = std::make_shared<State>();
  results_.clear();
  auto state = state_;
  const auto title = title_, filter = filter_;
  const auto initial = initial_;
  EventLoop::post([this, state, title, filter, initial, flags,
                   callback = std::move(callback)] {
    if (!state->alive)
      return;
    Array<File> results;
#if defined(_WIN32)
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog *dialog{};
    const bool save = (flags & FileDialogFlags::saveMode) != 0;
    if (SUCCEEDED(CoCreateInstance(
            save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
      state->dialog = dialog;
      FILEOPENDIALOGOPTIONS options{};
      dialog->GetOptions(&options);
      options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
      if (flags & FileDialogFlags::canSelectDirectories)
        options |= FOS_PICKFOLDERS;
      if (!save) {
        options |= FOS_FILEMUSTEXIST;
        if (flags & FileDialogFlags::canSelectMultipleItems)
          options |= FOS_ALLOWMULTISELECT;
      }
      if (save && (flags & FileDialogFlags::warnAboutOverwriting))
        options |= FOS_OVERWRITEPROMPT;
      dialog->SetOptions(options);
      dialog->SetTitle(title.toWideString().c_str());
      const auto folder =
          initial.isDirectory() ? initial : initial.getParentDirectory();
      if (folder.isDirectory()) {
        IShellItem *item{};
        if (SUCCEEDED(SHCreateItemFromParsingName(
                folder.getFullPathName().toWideString().c_str(), nullptr,
                IID_PPV_ARGS(&item)))) {
          dialog->SetFolder(item);
          item->Release();
        }
      }
      if (initial != File{} && !initial.isDirectory())
        dialog->SetFileName(initial.getFileName().toWideString().c_str());
      const auto wildcard =
          filter.isNotEmpty() ? filter.toWideString() : std::wstring(L"*.*");
      const COMDLG_FILTERSPEC filters[] = {{L"Files", wildcard.c_str()},
                                           {L"All files", L"*.*"}};
      if (!(flags & FileDialogFlags::canSelectDirectories)) {
        dialog->SetFileTypes(2, filters);
        if (save && filter.startsWith("*.")) {
          const auto ext =
              filter.substring(2).upToFirstOccurrenceOf(";", false, false);
          if (!ext.containsChar('*'))
            dialog->SetDefaultExtension(ext.toWideString().c_str());
        }
      }
      if (SUCCEEDED(dialog->Show(GetActiveWindow())) && state->alive) {
        const auto add = [&](IShellItem *item) {
          PWSTR path{};
          if (item &&
              SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            results.add(File(Text(path)));
            CoTaskMemFree(path);
          }
        };
        if (!save && (flags & FileDialogFlags::canSelectMultipleItems)) {
          IFileOpenDialog *open{};
          if (SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&open)))) {
            IShellItemArray *items{};
            if (SUCCEEDED(open->GetResults(&items))) {
              DWORD count{};
              items->GetCount(&count);
              for (DWORD i = 0; i < count; ++i) {
                IShellItem *item{};
                if (SUCCEEDED(items->GetItemAt(i, &item))) {
                  add(item);
                  item->Release();
                }
              }
              items->Release();
            }
            open->Release();
          }
        } else {
          IShellItem *item{};
          if (SUCCEEDED(dialog->GetResult(&item))) {
            add(item);
            item->Release();
          }
        }
      }
      state->dialog = nullptr;
      dialog->Release();
    }
    if (SUCCEEDED(initialized))
      CoUninitialize();
#endif
    if (!state->alive)
      return;
    results_ = std::move(results);
    if (callback)
      callback(*this);
  });
}
} // namespace cydaw::base
