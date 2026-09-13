#include <borealis/file_select.hpp>

namespace borealis::file_select {

Capabilities capabilities() noexcept {
  return Capabilities{};
}

bool busy() noexcept {
  return false;
}

void open_file(FileOptions, Callback callback) {
  if (callback)
    callback(Result{Status::Canceled, {}, "no system picker on Switch"});
}

void open_folder(FolderOptions, Callback callback) {
  if (callback)
    callback(Result{Status::Canceled, {}, "no system picker on Switch"});
}

void export_file(ExportOptions, Callback callback) {
  if (callback)
    callback(Result{Status::Canceled, {}, "no system picker on Switch"});
}

}  // namespace borealis::file_select
