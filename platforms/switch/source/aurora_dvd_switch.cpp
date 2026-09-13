#include <cstddef>
#include <cstdint>
#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <dolphin/types.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <thread>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <string_view>
#include <utility>
#include <vector>

#include "switch/runtime.hpp"

namespace dusk::sw::dvd {


void DVDLog(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  dusk::sw::log_line(buf);
}


using FstIndex = s32;
constexpr s32 k_invalidFstEntry = -1;

struct FSTEntry {
  std::string name;
  bool isDir = false;
  FstIndex parent = 0;
  u32 nextOrLength = 0;
  void* overlayData = nullptr;
  bool isOverlay = false;
  s32 origEntryNum = 0;
  u32 fileOffset = 0;
};

struct IterateNode {
  std::string name;
  bool isDir;
  s32 originalEntryNum;
  u32 size;
  void* overlayData;
  bool isOverlay;
  std::vector<std::shared_ptr<IterateNode>> children;

  IterateNode(std::string name, bool isDir, u32 size, s32 originalEntryNum, void* overlayData)
  : name(std::move(name)), isDir(isDir), size(size), originalEntryNum(originalEntryNum), overlayData(overlayData), isOverlay(true) {}

  IterateNode(std::string name, bool isDir, u32 size, s32 originalEntryNum)
  : name(std::move(name)), isDir(isDir), size(size), originalEntryNum(originalEntryNum), overlayData(nullptr), isOverlay(false) {}
};

struct IterateContext {
  std::shared_ptr<IterateNode> root;
  std::vector<std::pair<std::shared_ptr<IterateNode>, u32>> dirStack;
};

struct OverlayFileEntry {
  std::string fileName;
  void* userData;
  u32 size;
  s32 entryNum = k_invalidFstEntry;
  size_t sourceIndex = 0;
};

// ---- shared state (mirrors aurora dvd.cpp) ----
std::string s_isoPath;
std::vector<FSTEntry> s_fstEntries;
std::vector<FstIndex> s_entryNumToFstIndex;
std::vector<u8> s_dolData;
u32 s_fstOffset = 0;
u32 s_fstSize = 0;
u64 s_isoSize = 0;
s32 s_baseEntryCount = 0;
FstIndex s_currentDir = 0;
std::string s_currentPath = "/";
BOOL s_autoInvalidation = FALSE;
BOOL s_autoFatalMessaging = FALSE;
DVDDiskID s_diskID = {};
DVDLowCallback s_resetCoverCallback = nullptr;
bool s_initialized = false;
bool s_isoOpen = false;
bool s_overlayCallbacksSet = false;
AuroraOverlayCallbacks s_overlayCallbacks;
std::mutex s_fstLock;
std::vector<OverlayFileEntry> s_overlayFiles;
std::unordered_map<std::string, s32> s_overlayEntryNums;
s32 s_nextOverlayEntryNum = 0;
s32 s_overlayEntryNumBase = 0;
std::mutex s_ioMutex;
std::vector<u32> s_walkOffsets;

namespace {

std::string normalizeOverlayPath(std::string_view path) {
  std::string normalized;
  normalized.reserve(path.size());
  bool lastWasSlash = false;
  for (char ch : path) {
    if (ch == '\\') {
      ch = '/';
    }
    if (ch == '/') {
      if (lastWasSlash) {
        continue;
      }
      lastWasSlash = true;
      normalized.push_back('/');
      continue;
    }
    lastWasSlash = false;
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
    normalized.push_back(ch);
  }
  if (normalized.size() > 1 && normalized.back() == '/') {
    normalized.pop_back();
  }
  return normalized;
}

void syncOverlayEntryAllocator() {
  if (s_overlayEntryNumBase == s_baseEntryCount) {
    return;
  }

  s_overlayEntryNums.clear();
  s_overlayEntryNumBase = s_baseEntryCount;
  s_nextOverlayEntryNum = s_baseEntryCount;
}

s32 allocateOverlayEntryNum(std::string_view path) {
  syncOverlayEntryAllocator();

  std::string normalized = normalizeOverlayPath(path);
  auto it = s_overlayEntryNums.find(normalized);
  if (it != s_overlayEntryNums.end()) {
    return it->second;
  }

  const s32 entryNum = s_nextOverlayEntryNum++;
  s_overlayEntryNums.emplace(std::move(normalized), entryNum);
  return entryNum;
}

bool nameEqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < rhs.size(); ++i) {
    char lc = lhs[i];
    char rc = rhs[i];
    if (lc >= 'a' && lc <= 'z') {
      lc = static_cast<char>(lc - 'a' + 'A');
    }
    if (rc >= 'a' && rc <= 'z') {
      rc = static_cast<char>(rc - 'a' + 'A');
    }
    if (lc != rc) {
      return false;
    }
  }
  return true;
}

// Feeds one raw FST entry (in disc order) into the context tree.
// Replaces nod_partition_iterate_fst(); directory range end = next index.
u32 fstFeedEntry(u32 index, bool isDir, const char* name, u32 size, void* userData) {
  auto* ctx = static_cast<IterateContext*>(userData);

  while (index >= ctx->dirStack.back().second) {
    ctx->dirStack.pop_back();
  }

  const auto newEntry = std::make_shared<IterateNode>(
    name, isDir, size, static_cast<s32>(index));

  const auto& curDir = ctx->dirStack.back().first;
  curDir->children.push_back(newEntry);

  if (newEntry->isDir) {
    ctx->dirStack.emplace_back(newEntry, size);
  }

  return index + 1;
}

IterateNode* findNode(const IterateNode& node, const std::string_view name) {
  for (const auto& child : node.children) {
    if (nameEqualsIgnoreCase(child->name, name)) {
      return child.get();
    }
  }

  return nullptr;
}

void mergeOverlayFileIntoContext(const IterateContext& context, OverlayFileEntry& overlayFile) {
  IterateNode* node = context.root.get();
  std::string_view filePath = overlayFile.fileName;
  std::string currentPath;

  assert(!filePath.empty() && filePath.front() == '/');
  filePath = filePath.substr(1);
  while (true) {
    const auto nextDelim = filePath.find('/');
    if (nextDelim == std::string_view::npos) {
      break;
    }

    const auto segment = filePath.substr(0, nextDelim);
    filePath = filePath.substr(nextDelim + 1);
    currentPath += '/';
    currentPath.append(segment);

    const auto existingNode = findNode(*node, segment);
    if (existingNode) {
      if (!existingNode->isDir) {
        DVDLog("Overlay file needs directory that is already a file!");
        return;
      }

      node = existingNode;
    } else {
      const s32 entryNum = allocateOverlayEntryNum(currentPath);
      const auto newNode = std::make_shared<IterateNode>(std::string(segment), true, 0, entryNum);
      node->children.push_back(newNode);
      node = newNode.get();
    }
  }

  std::string fullFilePath = currentPath;
  fullFilePath += '/';
  fullFilePath.append(filePath);

  auto newNode = IterateNode(std::string(filePath), false, overlayFile.size, k_invalidFstEntry, overlayFile.userData);
  const auto existingNode = findNode(*node, filePath);
  if (existingNode) {
    if (existingNode->isDir) {
      DVDLog("Overlay file overlaps directory with same name!");
      return;
    }

    newNode.originalEntryNum = existingNode->originalEntryNum;
    overlayFile.entryNum = newNode.originalEntryNum;

    *existingNode = std::move(newNode);
  } else {
    newNode.originalEntryNum = allocateOverlayEntryNum(fullFilePath);
    overlayFile.entryNum = newNode.originalEntryNum;

    node->children.emplace_back(std::make_shared<IterateNode>(std::move(newNode)));
  }
}

void mergeOverlayFilesIntoContext(const IterateContext& context) {
  for (auto& overlayFile : s_overlayFiles) {
    mergeOverlayFileIntoContext(context, overlayFile);
  }
}

void makeFstRecursive(IterateNode& node, FstIndex parent) {
  if (node.originalEntryNum != k_invalidFstEntry) {
    if (s_entryNumToFstIndex.size() <= static_cast<size_t>(node.originalEntryNum)) {
      s_entryNumToFstIndex.resize(node.originalEntryNum + 1, k_invalidFstEntry);
    }

    auto& map = s_entryNumToFstIndex[node.originalEntryNum];
    if (map != k_invalidFstEntry) {
      DVDLog("Duplicate FST entry num in map!");
      return;
    }

    map = static_cast<FstIndex>(s_fstEntries.size());
  }

  if (!node.isDir) {
    assert(node.children.empty());
    assert(node.originalEntryNum != k_invalidFstEntry);

    s_fstEntries.emplace_back(node.name, false, parent, node.size, node.overlayData, node.isOverlay, node.originalEntryNum);
    return;
  }

  std::ranges::sort(node.children, [](const auto& a, const auto& b) { return a->name < b->name; });

  const FstIndex ourIndex = static_cast<FstIndex>(s_fstEntries.size());
  s_fstEntries.emplace_back(node.name, true, parent, 0, node.overlayData, node.isOverlay, node.originalEntryNum);

  for (const auto& child : node.children) {
    makeFstRecursive(*child, ourIndex);
  }

  s_fstEntries[ourIndex].nextOrLength = static_cast<u32>(s_fstEntries.size());
}

void makeFstFromContext(const IterateContext& context) {
  makeFstRecursive(*context.root, 0);
}

s32 calcEntryCount(const IterateNode& node) {
  s32 counter = 1;

  for (const auto& child : node.children) {
    counter += calcEntryCount(*child);
  }

  return counter;
}

bool validateOverlayFile(const AuroraOverlayFile& file) {
  const std::string_view name(file.fileName);

  if (name.empty() || name.front() != '/') {
    DVDLog("Overlay path does not start with /");
    return false;
  }

  if (file.size > std::numeric_limits<u32>::max()) {
    DVDLog("Overlay file sizes above 4 GiB are not supported");
    return false;
  }

  return true;
}

bool isValidEntryNum(s32 entry) {
  return entry >= 0 && static_cast<size_t>(entry) < s_entryNumToFstIndex.size() &&
         s_entryNumToFstIndex[entry] != k_invalidFstEntry;
}

bool isValidFstIndex(FstIndex entry) {
  return entry >= 0 && static_cast<size_t>(entry) < s_fstEntries.size();
}

bool isAligned(const void* addr, uintptr_t align) {
  return (reinterpret_cast<uintptr_t>(addr) & (align - 1)) == 0;
}

FstIndex findInDir(FstIndex dirEntry, const char* name, size_t nameLen) {
  if (!isValidFstIndex(dirEntry) || !s_fstEntries[dirEntry].isDir) {
    return -1;
  }

  u32 childEnd = s_fstEntries[dirEntry].nextOrLength;
  u32 i = static_cast<u32>(dirEntry) + 1;
  while (i < childEnd && i < s_fstEntries.size()) {
    if (nameEqualsIgnoreCase(s_fstEntries[i].name, std::string_view(name, nameLen))) {
      return static_cast<FstIndex>(i);
    }

    if (s_fstEntries[i].isDir) {
      u32 next = s_fstEntries[i].nextOrLength;
      i = (next > i) ? next : i + 1;
    } else {
      ++i;
    }
  }
  return -1;
}

std::string build_path(FstIndex fstIndex) {
  if (fstIndex <= 0 || !isValidFstIndex(fstIndex)) {
    return "/";
  }

  const bool isDir = s_fstEntries[fstIndex].isDir;
  std::vector<std::string> parts;
  FstIndex cur = fstIndex;
  while (cur > 0 && isValidFstIndex(cur)) {
    parts.push_back(s_fstEntries[cur].name);
    auto parent = s_fstEntries[cur].parent;
    if (parent == cur) {
      break;
    }
    cur = parent;
  }

  std::string out = "/";
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    if (it != parts.rbegin()) {
      out += '/';
    }
    out += *it;
  }
  if (isDir) {
    out += '/';
  }
  return out;
}

// aurora_dvd_switch.cpp (part 2/3): raw ISO transport, worker, open/FST/DOL.

constexpr u32 k_gcFstOffsetOff = 0x424;
constexpr u32 k_gcFstSizeOff = 0x428;
constexpr u32 k_gcDolOffsetOff = 0x420;
constexpr u32 k_gcHeaderSize = 0x440;

u32 be32(const u8* p) {
  return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}

bool isoPRead(u64 offset, void* out, size_t len) {
  if (!s_isoOpen || out == nullptr) {
    return false;
  }
  if (offset >= s_isoSize) {
    return len == 0;
  }
  std::lock_guard lk(s_ioMutex);
  FILE* f = fopen(s_isoPath.c_str(), "rb");
  if (f == nullptr) {
    return false;
  }
#if defined(_WIN32)
  _fseeki64(f, static_cast<__int64>(offset), SEEK_SET);
#else
  fseeko(f, static_cast<off_t>(offset), SEEK_SET);
#endif
  const size_t got = fread(out, 1, len, f);
  fclose(f);
  return got == len;
}

class CommandDataBase {
public:
  virtual ~CommandDataBase() = default;
  virtual int64_t read(uint8_t* buf, size_t len) = 0;
  virtual int64_t seek(int64_t offset, int32_t whence) = 0;
};

// Raw-file handle: independent cursor per open (own position, shared image).
class CommandDataRawFile final : public CommandDataBase {
public:
  u64 base = 0;
  u64 size = 0;
  u64 pos = 0;

  CommandDataRawFile(u64 baseOffset, u64 length) : base(baseOffset), size(length), pos(0) {}

  int64_t read(uint8_t* buf, size_t len) override {
    if (buf == nullptr) {
      return -1;
    }
    if (pos >= size) {
      return 0;
    }
    size_t want = len;
    if (pos + want > size) {
      want = static_cast<size_t>(size - pos);
    }
    std::vector<u8> tmp(want);
    if (!isoPRead(base + pos, tmp.data(), want)) {
      return -1;
    }
    std::memcpy(buf, tmp.data(), want);
    pos += want;
    return static_cast<int64_t>(want);
  }

  int64_t seek(int64_t offset, int32_t whence) override {
    int64_t target = 0;
    if (whence == SEEK_SET) {
      target = offset;
    } else if (whence == SEEK_CUR) {
      target = static_cast<int64_t>(pos) + offset;
    } else if (whence == SEEK_END) {
      target = static_cast<int64_t>(size) + offset;
    } else {
      return -1;
    }
    if (target < 0) {
      return -1;
    }
    pos = static_cast<u64>(target);
    return target;
  }
};

class CommandDataOverlay final : public CommandDataBase {
public:
  void* handle;
  explicit CommandDataOverlay(void* handle) : handle(handle) {}
  ~CommandDataOverlay() override {
    s_overlayCallbacks.close(handle);
  }

  int64_t read(uint8_t* buf, size_t len) override {
    return s_overlayCallbacks.read(handle, buf, len);
  }

  int64_t seek(int64_t offset, int32_t whence) override {
    return s_overlayCallbacks.seek(handle, offset, whence);
  }
};

CommandDataRawFile* s_discRaw = nullptr;

void clearState() {
  if (s_discRaw != nullptr) {
    delete s_discRaw;
    s_discRaw = nullptr;
  }
  s_fstEntries.clear();
  s_entryNumToFstIndex.clear();
  s_dolData.clear();
  s_fstOffset = 0;
  s_fstSize = 0;
  s_isoSize = 0;
  s_baseEntryCount = 0;
  s_currentDir = 0;
  s_currentPath = "/";
  s_diskID = {};
  s_initialized = false;
  s_isoOpen = false;
}

s32 readFromHandle(CommandDataBase* handle, void* out, s32 length, s32 offset, u32* transferredOut) {
  if (transferredOut != nullptr) {
    *transferredOut = 0;
  }
  if (handle == nullptr || out == nullptr || length < 0 || offset < 0) {
    return DVD_RESULT_FATAL_ERROR;
  }
  if (length == 0) {
    return 0;
  }
  if (handle->seek(offset, 0) < 0) {
    return DVD_RESULT_FATAL_ERROR;
  }

  u8* writePtr = static_cast<u8*>(out);
  s32 totalRead = 0;
  s32 remaining = length;
  while (remaining > 0) {
    const int64_t read = handle->read(writePtr + totalRead, static_cast<size_t>(remaining));
    if (read < 0) {
      return DVD_RESULT_FATAL_ERROR;
    }
    if (read == 0) {
      break;
    }
    totalRead += static_cast<s32>(read);
    remaining -= static_cast<s32>(read);
  }

  if (transferredOut != nullptr) {
    *transferredOut = static_cast<u32>(totalRead);
  }
  return totalRead;
}

template <typename T>
void atomic_store_relaxed(T& ref, T val) {
#if defined(__cpp_lib_atomic_ref)
  std::atomic_ref<T>{ref}.store(val, std::memory_order_relaxed);
#else
  __atomic_store_n(&ref, val, __ATOMIC_RELAXED);
#endif
}

template <typename T>
void atomic_store_release(T& ref, T val) {
#if defined(__cpp_lib_atomic_ref)
  std::atomic_ref<T>{ref}.store(val, std::memory_order_release);
#else
  __atomic_store_n(&ref, val, __ATOMIC_RELEASE);
#endif
}

template <typename T>
T atomic_load_relaxed(const T& ref) {
#if defined(__cpp_lib_atomic_ref)
  return std::atomic_ref<T>{const_cast<T&>(ref)}.load(std::memory_order_relaxed);
#else
  return __atomic_load_n(const_cast<T*>(&ref), __ATOMIC_RELAXED);
#endif
}

template <typename T>
T atomic_load_acquire(const T& ref) {
#if defined(__cpp_lib_atomic_ref)
  return std::atomic_ref<T>{const_cast<T&>(ref)}.load(std::memory_order_acquire);
#else
  return __atomic_load_n(const_cast<T*>(&ref), __ATOMIC_ACQUIRE);
#endif
}

void setCommandResult(DVDCommandBlock* block, s32 state, u32 transferred) {
  if (block == nullptr) {
    return;
  }
  atomic_store_relaxed(block->transferredSize, transferred);
  atomic_store_release(block->state, state);
}

s32 stateForResult(s32 result) {
  if (result == DVD_RESULT_CANCELED) {
    return DVD_STATE_CANCELED;
  }
  if (result == DVD_RESULT_IGNORED) {
    return DVD_STATE_IGNORED;
  }
  return result >= 0 ? DVD_STATE_END : DVD_STATE_FATAL_ERROR;
}

bool isCommandBlockIdle(const DVDCommandBlock* block) {
  if (block == nullptr) {
    return false;
  }
  const s32 state = atomic_load_acquire(block->state);
  return state != DVD_STATE_BUSY && state != DVD_STATE_WAITING;
}

CommandDataBase* getCommandHandle(DVDCommandBlock* block) {
  if (block != nullptr && block->userData != nullptr) {
    return static_cast<CommandDataBase*>(block->userData);
  }
  return s_discRaw;
}

void beginCommand(DVDCommandBlock* block, u32 command, void* addr, u32 length, u32 offset, DVDCBCallback callback) {
  if (block == nullptr) {
    return;
  }
  block->command = command;
  block->addr = addr;
  block->length = length;
  block->offset = offset;
  atomic_store_relaxed(block->transferredSize, 0u);
  block->callback = callback;
  atomic_store_release(block->state, DVD_STATE_BUSY);
}

void finishCommand(DVDCommandBlock* block, s32 result, u32 transferred) {
  setCommandResult(block, stateForResult(result), transferred);
}

class DvdWorker {
public:
  ~DvdWorker() { stop(); }

  void start() {
    std::lock_guard lk(m_mutex);
    if (m_running) {
      return;
    }
    m_shutdown = false;
    m_thread = std::thread([this] { run(); });
    m_running = true;
  }

  void stop() {
    std::vector<DVDCommandBlock*> canceledBlocks;
    bool stoppedFromWorker = false;
    {
      std::lock_guard lk(m_mutex);
      if (!m_running) {
        return;
      }
      m_shutdown = true;
      canceledBlocks = discard_pending_commands_locked();
      if (std::this_thread::get_id() == m_thread.get_id()) {
        m_running = false;
        m_thread.detach();
        stoppedFromWorker = true;
      } else if (m_activeBlock != nullptr) {
        m_cancelActiveBlock = m_activeBlock;
      }
    }
    complete_canceled_commands(canceledBlocks);
    m_doneCv.notify_all();
    if (stoppedFromWorker) {
      return;
    }
    m_cv.notify_all();
    m_thread.join();
    {
      std::lock_guard lk(m_mutex);
      m_running = false;
    }
    m_doneCv.notify_all();
  }

  void enqueue(DVDCommandBlock* block) {
    bool executeNow = false;
    {
      std::lock_guard lk(m_mutex);
      if (!m_running || m_shutdown) {
        executeNow = true;
      } else {
        atomic_store_release(block->state, DVD_STATE_WAITING);
        m_queue.push_back(block);
      }
    }
    if (executeNow) {
      execute(block);
      return;
    }
    m_cv.notify_one();
  }

  void retire_command(DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }

    DVDCommandBlock* canceledBlock = nullptr;
    std::unique_lock lk(m_mutex);
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
      if (*it == block) {
        m_queue.erase(it);
        canceledBlock = block;
        lk.unlock();
        complete_canceled_command(canceledBlock);
        m_doneCv.notify_all();
        return;
      }
    }

    if (m_activeBlock == block && std::this_thread::get_id() != m_thread.get_id()) {
      m_cancelActiveBlock = block;
      m_doneCv.wait(lk, [&] { return !m_running || m_activeBlock != block; });
    } else {
      atomic_store_release(block->state, DVD_STATE_CANCELED);
      lk.unlock();
      m_doneCv.notify_all();
      return;
    }
  }

  void cancel_all() {
    std::vector<DVDCommandBlock*> canceledBlocks;
    bool waitForActive = false;
    {
      std::lock_guard lk(m_mutex);
      canceledBlocks = discard_pending_commands_locked();
      if (m_activeBlock != nullptr && std::this_thread::get_id() != m_thread.get_id()) {
        m_cancelActiveBlock = m_activeBlock;
        waitForActive = true;
      }
    }
    complete_canceled_commands(canceledBlocks);
    m_doneCv.notify_all();

    if (waitForActive) {
      std::unique_lock lk(m_mutex);
      m_doneCv.wait(lk, [&] { return !m_running || m_activeBlock == nullptr; });
    }
  }

  void drain_command(DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }

    DVDCommandBlock* canceledBlock = nullptr;
    std::unique_lock lk(m_mutex);
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
      if (*it == block) {
        m_queue.erase(it);
        canceledBlock = block;
        lk.unlock();
        complete_canceled_command(canceledBlock);
        m_doneCv.notify_all();
        return;
      }
    }

    if (m_activeBlock == block && std::this_thread::get_id() != m_thread.get_id()) {
      m_cancelActiveBlock = block;
      m_doneCv.wait(lk, [&] { return !m_running || m_activeBlock != block; });
    }
  }

  void wait(const DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }
    std::unique_lock lk(m_mutex);
    m_doneCv.wait(lk, [&] {
      const s32 state = atomic_load_acquire(block->state);
      return !m_running || (m_activeBlock != block && state != DVD_STATE_BUSY && state != DVD_STATE_WAITING);
    });
  }

private:
  void run() {
    std::unique_lock lk(m_mutex);
    while (true) {
      m_cv.wait(lk, [&] { return m_shutdown || !m_queue.empty(); });
      if (m_shutdown) {
        return;
      }
      DVDCommandBlock* block = m_queue.front();
      m_queue.pop_front();
      m_activeBlock = block;
      atomic_store_release(block->state, DVD_STATE_BUSY);
      lk.unlock();
      process_command(block);
      lk.lock();
      m_activeBlock = nullptr;
      if (m_cancelActiveBlock == block) {
        m_cancelActiveBlock = nullptr;
      }
      lk.unlock();
      m_doneCv.notify_all();
      lk.lock();
    }
  }

  static std::pair<s32, u32> perform_command(DVDCommandBlock* block) {
    s32 result;
    u32 transferred = 0;
    if (block->command == DVD_COMMAND_SEEK) {
      auto* handle = getCommandHandle(block);
      const int64_t seek = handle != nullptr ? handle->seek(block->offset, 0) : -1;
      result = seek < 0 ? DVD_RESULT_FATAL_ERROR : DVD_RESULT_GOOD;
    } else {
      result = readFromHandle(getCommandHandle(block), block->addr, static_cast<s32>(block->length),
                              static_cast<s32>(block->offset), &transferred);
    }
    return {result, transferred};
  }

  void process_command(DVDCommandBlock* block) {
    auto [result, transferred] = perform_command(block);
    if (consume_active_cancel(block)) {
      result = DVD_RESULT_CANCELED;
      transferred = 0;
    }
    finishCommand(block, result, transferred);
    if (block->callback != nullptr) {
      block->callback(result, block);
    }
  }

  void execute(DVDCommandBlock* block) {
    {
      std::lock_guard lk(m_mutex);
      m_activeBlock = block;
      atomic_store_release(block->state, DVD_STATE_BUSY);
    }
    process_command(block);
    {
      std::lock_guard lk(m_mutex);
      m_activeBlock = nullptr;
      if (m_cancelActiveBlock == block) {
        m_cancelActiveBlock = nullptr;
      }
    }
    m_doneCv.notify_all();
  }

  bool consume_active_cancel(DVDCommandBlock* block) {
    std::lock_guard lk(m_mutex);
    if (m_cancelActiveBlock != block) {
      return false;
    }
    m_cancelActiveBlock = nullptr;
    return true;
  }

  static void complete_canceled_command(DVDCommandBlock* block) {
    if (block == nullptr) {
      return;
    }
    finishCommand(block, DVD_RESULT_CANCELED, 0);
    if (block->callback != nullptr) {
      block->callback(DVD_RESULT_CANCELED, block);
    }
  }

  static void complete_canceled_commands(const std::vector<DVDCommandBlock*>& blocks) {
    for (auto* block : blocks) {
      complete_canceled_command(block);
    }
  }

  std::vector<DVDCommandBlock*> discard_pending_commands_locked() {
    std::vector<DVDCommandBlock*> blocks;
    blocks.reserve(m_queue.size());
    for (auto* block : m_queue) {
      blocks.push_back(block);
    }
    m_queue.clear();
    return blocks;
  }

  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::condition_variable m_doneCv;
  std::deque<DVDCommandBlock*> m_queue;
  DVDCommandBlock* m_activeBlock = nullptr;
  DVDCommandBlock* m_cancelActiveBlock = nullptr;
  bool m_running = false;
  bool m_shutdown = false;
};

DvdWorker s_worker;

int completeImmediateCommand(DVDCommandBlock* block, u32 command, s32 result, u32 transferred, DVDCBCallback callback) {
  beginCommand(block, command, nullptr, 0, 0, callback);
  finishCommand(block, result, transferred);
  if (callback != nullptr) {
    callback(result, block);
  }
  return TRUE;
}

void cbForReadAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  assert(&fileInfo->cb == block);
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

void cbForSeekAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  assert(&fileInfo->cb == block);
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

void cbForPrepareStreamAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  assert(&fileInfo->cb == block);
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

// ---- raw FST + open/close ----

bool rebuildFST() {
  std::lock_guard lock(s_fstLock);

  s32 currentDirEntryNum = k_invalidFstEntry;
  const std::string currentPath = s_currentPath;
  if (s_currentDir >= 0 && static_cast<size_t>(s_currentDir) < s_fstEntries.size() && s_fstEntries[s_currentDir].isDir) {
    currentDirEntryNum = s_fstEntries[s_currentDir].origEntryNum;
  }

  s_fstEntries.clear();
  s_entryNumToFstIndex.clear();
  IterateContext ctx;
  ctx.root = std::make_shared<IterateNode>("", true, 0, 0);
  ctx.dirStack.emplace_back(ctx.root, std::numeric_limits<u32>::max());

  // Raw GameCube FST walk (replaces nod_partition_iterate_fst).
  std::vector<u8> fst(s_fstSize);
  if (s_fstSize < 12 || !isoPRead(s_fstOffset, fst.data(), s_fstSize)) {
    return false;
  }
  const u32 numEntries = be32(fst.data() + 8);
  if (numEntries < 1 || numEntries > 100000 || 12u * numEntries > s_fstSize) {
    return false;
  }
  const u8* strTab = fst.data() + 12u * numEntries;
  const u32 strSize = s_fstSize - 12u * numEntries;
  s_walkOffsets.assign(numEntries, 0);
  for (u32 i = 1; i < numEntries; ++i) {
    const u8* e = fst.data() + 12u * i;
    const u32 b0 = be32(e);
    const u32 b1 = be32(e + 4);
    const u32 b2 = be32(e + 8);
    const bool isDir = (b0 >> 24) != 0;
    const u32 nameOff = b0 & 0xFFFFFFu;
    if (nameOff >= strSize) {
      return false;
    }
    const char* name = reinterpret_cast<const char*>(strTab + nameOff);
    size_t nameLen = 0;
    while (nameOff + nameLen < strSize && name[nameLen] != '\0') {
      ++nameLen;
    }
    if (nameOff + nameLen >= strSize) {
      return false;
    }
    s_walkOffsets[i] = b1;
    fstFeedEntry(i, isDir, name, b2, &ctx);
  }

  s_baseEntryCount = calcEntryCount(*ctx.root);
  syncOverlayEntryAllocator();
  mergeOverlayFilesIntoContext(ctx);
  makeFstFromContext(ctx);
  for (auto& e : s_fstEntries) {
    if (!e.isDir && !e.isOverlay && e.origEntryNum >= 0 &&
        static_cast<u32>(e.origEntryNum) < s_walkOffsets.size()) {
      e.fileOffset = s_walkOffsets[e.origEntryNum];
    }
  }

  if (currentDirEntryNum >= 0 && static_cast<size_t>(currentDirEntryNum) < s_entryNumToFstIndex.size()) {
    const FstIndex currentDir = s_entryNumToFstIndex[currentDirEntryNum];
    if (currentDir >= 0 && static_cast<size_t>(currentDir) < s_fstEntries.size() && s_fstEntries[currentDir].isDir) {
      s_currentDir = currentDir;
      s_currentPath = currentPath;
      return true;
    }
  }

  if (currentDirEntryNum != k_invalidFstEntry) {
    DVDLog("DVD directory lost during FST rebuild; resetting to root");
  }

  s_currentDir = 0;
  s_currentPath = "/";
  return true;
}

// aurora_dvd_switch.cpp (part 3/3): C API (dolphin DVD + aurora_dvd_*).

constexpr u32 k_gcDolHeaderSize = 0x100;

bool readDol() {
  u8 hdr[k_gcDolHeaderSize];
  u32 dolOffset = 0;
  {
    u8 off4[4];
    if (!isoPRead(k_gcDolOffsetOff, off4, sizeof(off4))) {
      return false;
    }
    dolOffset = be32(off4);
  }
  if (dolOffset == 0 || !isoPRead(dolOffset, hdr, sizeof(hdr))) {
    return false;
  }
  u32 dolSize = 0;
  for (int i = 0; i < 7; ++i) {
    const u32 off = be32(hdr + 4 * i);
    const u32 size = be32(hdr + 0x90 + 4 * i);
    if (size > 0 && off != 0) {
      dolSize = std::max(dolSize, off + size);
    }
  }
  for (int i = 0; i < 11; ++i) {
    const u32 off = be32(hdr + 0x1C + 4 * i);
    const u32 size = be32(hdr + 0xAC + 4 * i);
    if (size > 0 && off != 0) {
      dolSize = std::max(dolSize, off + size);
    }
  }
  if (dolSize == 0 || static_cast<u64>(dolSize) > s_isoSize) {
    return false;
  }
  s_dolData.resize(dolSize);
  return isoPRead(dolOffset, s_dolData.data(), dolSize);
}


}  // namespace
extern "C" {

bool aurora_dvd_open(const char* disc_path) {
  if (disc_path == nullptr) {
    return false;
  }

  s_worker.stop();
  clearState();

  s_isoPath = disc_path;
  {
    FILE* probe = fopen(s_isoPath.c_str(), "rb");
    if (probe == nullptr) {
      clearState();
      return false;
    }
#if defined(_WIN32)
    _fseeki64(probe, 0, SEEK_END);
    const long long size = _ftelli64(probe);
#else
    fseeko(probe, 0, SEEK_END);
    const long long size = ftello(probe);
#endif
    fclose(probe);
    if (size <= static_cast<long long>(k_gcHeaderSize)) {
      clearState();
      return false;
    }
    s_isoSize = static_cast<u64>(size);
  }
  s_isoOpen = true;

  u8 header[k_gcHeaderSize];
  if (!isoPRead(0, header, sizeof(header))) {
    clearState();
    return false;
  }
  std::memcpy(&s_diskID, header, sizeof(s_diskID));
  s_fstOffset = be32(header + k_gcFstOffsetOff);
  s_fstSize = be32(header + k_gcFstSizeOff);
  if (s_fstOffset < k_gcHeaderSize || s_fstSize < 12 ||
      static_cast<u64>(s_fstOffset) + s_fstSize > s_isoSize) {
    clearState();
    return false;
  }

  s_discRaw = new CommandDataRawFile(0, s_isoSize);

  if (!readDol()) {
    DVDLog("DVD: could not read DOL, continuing without it");
    s_dolData.clear();
  }

  if (!rebuildFST()) {
    clearState();
    return false;
  }

  s_currentDir = 0;
  s_currentPath = "/";
  s_initialized = true;
  s_worker.start();
  DVDLog("DVD: opened image");
  return true;
}

void aurora_dvd_close(void) {
  s_worker.stop();
  clearState();
}

s32 aurora_dvd_base_entry_count() {
  return s_baseEntryCount;
}

void aurora_dvd_overlay_files(const AuroraOverlayFile* files, size_t nFiles, s32* outEntryNums) {
  if (!s_overlayCallbacksSet) {
    DVDLog("aurora_dvd_overlay_callbacks must be called first!");
    assert(false);
    return;
  }

  s_overlayFiles.clear();
  if (outEntryNums != nullptr) {
    for (size_t i = 0; i < nFiles; i++) {
      outEntryNums[i] = k_invalidFstEntry;
    }
  }

  for (size_t i = 0; i < nFiles; i++) {
    const auto& file = files[i];

    if (!validateOverlayFile(file)) {
      continue;
    }

    s_overlayFiles.emplace_back(file.fileName, file.userData, static_cast<u32>(file.size), k_invalidFstEntry, i);
  }

  rebuildFST();

  if (outEntryNums != nullptr) {
    for (const auto& file : s_overlayFiles) {
      if (file.entryNum != k_invalidFstEntry) {
        outEntryNums[file.sourceIndex] = file.entryNum;
      }
    }
  }
}

void aurora_dvd_overlay_callbacks(const AuroraOverlayCallbacks* callbacks) {
  s_overlayCallbacks = *callbacks;
  s_overlayCallbacksSet = true;
}

void DVDInit(void) {}

const u8* DVDGetDOLLocation(s32* out_size) {
  if (s_dolData.empty()) {
    *out_size = 0;
    return nullptr;
  }
  *out_size = static_cast<s32>(s_dolData.size());
  return s_dolData.data();
}

static int DVDReadAbsAsyncPrioInternal(DVDCommandBlock* block, u32 command, void* addr, s32 length, s32 offset,
                                       DVDCBCallback callback, s32 prio) {
  (void)prio;
  assert(block);
  assert(addr);
  assert(isAligned(addr, 32));
  assert(!(length & (32 - 1)));
  assert(!(offset & (4 - 1)));
  assert(length >= 0);
  assert(isCommandBlockIdle(block));

  beginCommand(block, command, addr, static_cast<u32>(length), static_cast<u32>(offset), callback);
  s_worker.enqueue(block);
  return TRUE;
}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback, s32 prio) {
  return DVDReadAbsAsyncPrioInternal(block, DVD_COMMAND_READ, addr, length, offset, callback, prio);
}

int DVDSeekAbsAsyncPrio(DVDCommandBlock* block, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  assert(block);
  assert(!(offset & (4 - 1)));
  assert(isCommandBlockIdle(block));

  beginCommand(block, DVD_COMMAND_SEEK, nullptr, 0, static_cast<u32>(offset), callback);
  s_worker.enqueue(block);
  return TRUE;
}

int DVDReadAbsAsyncForBS(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback) {
  return DVDReadAbsAsyncPrioInternal(block, DVD_COMMAND_BSREAD, addr, length, offset, callback, 2);
}

int DVDReadDiskID(DVDCommandBlock* block, DVDDiskID* diskID, DVDCBCallback callback) {
  if (diskID != nullptr) {
    *diskID = s_diskID;
  }
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

int DVDPrepareStreamAbsAsync(DVDCommandBlock* block, u32 length, u32 offset, DVDCBCallback callback) {
  const bool idle = isCommandBlockIdle(block);
  if (block == nullptr || !idle) {
    return FALSE;
  }
  beginCommand(block, DVD_COMMAND_INITSTREAM, nullptr, length, offset, callback);
  finishCommand(block, DVD_RESULT_IGNORED, 0);
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED, block);
  }
  return TRUE;
}

int DVDCancelStreamAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    atomic_store_release(block->state, DVD_STATE_CANCELED);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, block);
  }
  return TRUE;
}

s32 DVDCancelStream(DVDCommandBlock* block) {
  if (block != nullptr) {
    atomic_store_release(block->state, DVD_STATE_CANCELED);
  }
  return DVD_RESULT_GOOD;
}

int DVDStopStreamAtEndAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDStopStreamAtEnd(DVDCommandBlock* block) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  return DVD_RESULT_GOOD;
}

int DVDGetStreamErrorStatusAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_AUDIO_ERROR, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamErrorStatus(DVDCommandBlock* block) {
  (void)block;
  return DVD_RESULT_IGNORED;
}

int DVDGetStreamPlayAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_PLAY_ADDR, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamPlayAddr(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDGetStreamStartAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_START_ADDR, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamStartAddr(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDGetStreamLengthAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block == nullptr || !isCommandBlockIdle(block)) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_LENGTH, DVD_RESULT_IGNORED, 0, callback);
}

s32 DVDGetStreamLength(DVDCommandBlock* block) {
  (void)block;
  return 0;
}

int DVDChangeDiskAsyncForBS(DVDCommandBlock* block, DVDCBCallback callback) {
  assert(block);
  const bool idle = isCommandBlockIdle(block);
  assert(idle);
  if (block == nullptr || !idle) {
    return FALSE;
  }
  return completeImmediateCommand(block, DVD_COMMAND_BS_CHANGE_DISK, DVD_RESULT_IGNORED, 0, callback);
}

int DVDChangeDiskAsync(DVDCommandBlock* block, DVDDiskID* id, DVDCBCallback callback) {
  (void)id;
  return DVDChangeDiskAsyncForBS(block, callback);
}

s32 DVDChangeDisk(DVDCommandBlock* block, DVDDiskID* id) {
  (void)block;
  (void)id;
  return DVD_RESULT_IGNORED;
}

int DVDStopMotorAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDStopMotor(DVDCommandBlock* block) {
  (void)block;
  return DVD_RESULT_GOOD;
}

int DVDInquiryAsync(DVDCommandBlock* block, DVDDriveInfo* info, DVDCBCallback callback) {
  if (info != nullptr) {
    std::memset(info, 0, sizeof(*info));
  }
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDInquiry(DVDCommandBlock* block, DVDDriveInfo* info) {
  DVDInquiryAsync(block, info, nullptr);
  return DVD_RESULT_GOOD;
}

void DVDReset(void) {}

int DVDResetRequired(void) { return FALSE; }

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
  if (block == nullptr) {
    return DVD_STATE_END;
  }
  return atomic_load_acquire(block->state);
}

s32 DVDGetDriveStatus(void) { return s_initialized ? DVD_STATE_END : DVD_STATE_NO_DISK; }

BOOL DVDSetAutoInvalidation(BOOL autoInval) {
  BOOL prev = s_autoInvalidation;
  s_autoInvalidation = autoInval;
  return prev;
}

void DVDPause(void) {}

void DVDResume(void) {}

int DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  s_worker.retire_command(block);
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, block);
  }
  return TRUE;
}

s32 DVDCancel(volatile DVDCommandBlock* block) {
  auto* mutableBlock = const_cast<DVDCommandBlock*>(block);
  s_worker.retire_command(mutableBlock);
  return DVD_RESULT_GOOD;
}

int DVDCancelAllAsync(DVDCBCallback callback) {
  s_worker.cancel_all();
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, nullptr);
  }
  return TRUE;
}

s32 DVDCancelAll(void) {
  s_worker.cancel_all();
  return DVD_RESULT_GOOD;
}

DVDDiskID* DVDGetCurrentDiskID(void) { return &s_diskID; }

BOOL DVDCheckDisk(void) { return s_initialized ? TRUE : FALSE; }

int DVDSetAutoFatalMessaging(BOOL enable) {
  const BOOL prev = s_autoFatalMessaging;
  s_autoFatalMessaging = enable;
  return prev;
}

s32 DVDConvertPathToEntrynum(const char* pathPtr) {
  std::lock_guard lock(s_fstLock);

  if (!s_initialized || pathPtr == nullptr || s_fstEntries.empty()) {
    return -1;
  }

  FstIndex current = 0;
  const char* p = pathPtr;
  if (*p == '/') {
    ++p;
  } else {
    current = s_currentDir;
  }

  while (*p != '\0') {
    while (*p == '/') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    if (!isValidFstIndex(current) || !s_fstEntries[current].isDir) {
      return -1;
    }

    const char* compEnd = p;
    while (*compEnd != '\0' && *compEnd != '/') {
      ++compEnd;
    }
    size_t compLen = static_cast<size_t>(compEnd - p);

    if (compLen == 1 && p[0] == '.') {
      // no-op
    } else if (compLen == 2 && p[0] == '.' && p[1] == '.') {
      current = static_cast<s32>(s_fstEntries[current].parent);
    } else {
      const FstIndex found = findInDir(current, p, compLen);
      if (found < 0) {
        return -1;
      }
      current = found;
    }
    p = compEnd;
  }

  assert(isValidFstIndex(current));
  return s_fstEntries[current].origEntryNum;
}

BOOL DVDConvertEntrynumToPath(s32 entrynum, char* path, u32 maxlen) {
  std::lock_guard lock(s_fstLock);

  if (path == nullptr || maxlen == 0) {
    return FALSE;
  }
  path[0] = '\0';

  if (!s_initialized || !isValidEntryNum(entrynum)) {
    return FALSE;
  }

  const std::string entryPath = build_path(s_entryNumToFstIndex[entrynum]);
  const size_t copyLen = std::min(entryPath.size(), static_cast<size_t>(maxlen - 1));
  std::memcpy(path, entryPath.data(), copyLen);
  path[copyLen] = '\0';
  return entryPath.size() < maxlen ? TRUE : FALSE;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
  std::lock_guard lock(s_fstLock);

  if (!s_initialized || fileInfo == nullptr || !isValidEntryNum(entrynum) || !s_isoOpen) {
    return FALSE;
  }

  const auto fstIndex = s_entryNumToFstIndex[entrynum];
  assert(fstIndex >= 0);

  const auto& entry = s_fstEntries[fstIndex];
  if (entry.isDir) {
    return FALSE;
  }

  std::memset(fileInfo, 0, sizeof(*fileInfo));
  fileInfo->startAddr = 0;
  fileInfo->length = entry.nextOrLength;

  if (entry.isOverlay) {
    const auto handle = s_overlayCallbacks.open(entry.overlayData);
    if (!handle) {
      return FALSE;
    }

    fileInfo->cb.userData = new CommandDataOverlay(handle);
  } else {
    fileInfo->cb.userData = new CommandDataRawFile(entry.fileOffset, entry.nextOrLength);
  }

  atomic_store_release(fileInfo->cb.state, DVD_STATE_END);
  return TRUE;
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
  s32 entrynum = DVDConvertPathToEntrynum(fileName);
  if (entrynum < 0) {
    return FALSE;
  }
  return DVDFastOpen(entrynum, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
  if (fileInfo == nullptr) {
    return FALSE;
  }
  s_worker.drain_command(&fileInfo->cb);
  if (fileInfo->cb.userData != nullptr) {
    delete static_cast<CommandDataBase*>(fileInfo->cb.userData);
    fileInfo->cb.userData = nullptr;
  }
  atomic_store_release(fileInfo->cb.state, DVD_STATE_END);
  return TRUE;
}

BOOL DVDGetCurrentDir(char* path, u32 maxlen) {
  if (path == nullptr || maxlen == 0) {
    return FALSE;
  }
  const size_t len = s_currentPath.size();
  const size_t copyLen = (len >= maxlen) ? (maxlen - 1) : len;
  std::memcpy(path, s_currentPath.c_str(), copyLen);
  path[copyLen] = '\0';
  return TRUE;
}

BOOL DVDChangeDir(const char* dirName) {
  s32 entry = DVDConvertPathToEntrynum(dirName);

  std::lock_guard lock(s_fstLock);

  if (!isValidEntryNum(entry)) {
    return FALSE;
  }

  const auto fstIndex = s_entryNumToFstIndex[entry];
  if (!s_fstEntries[fstIndex].isDir) {
    return FALSE;
  }

  s_currentDir = fstIndex;
  s_currentPath = build_path(fstIndex);
  return TRUE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
  assert(fileInfo);
  assert(addr);
  assert((0 <= offset) && (offset <= static_cast<s32>(fileInfo->length)));
  assert((0 <= offset + length) && (offset + length < static_cast<s32>(fileInfo->length) + DVD_MIN_TRANSFER_SIZE));

  fileInfo->callback = callback;
  DVDReadAbsAsyncPrio(&fileInfo->cb, addr, length, offset, cbForReadAsync, prio);
  return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
  if (!DVDReadAsyncPrio(fileInfo, addr, length, offset, nullptr, prio)) {
    return DVD_RESULT_FATAL_ERROR;
  }
  s_worker.wait(&fileInfo->cb);
  const s32 state = atomic_load_acquire(fileInfo->cb.state);
  if (state == DVD_STATE_END) {
    return static_cast<s32>(atomic_load_relaxed(fileInfo->cb.transferredSize));
  }
  if (state == DVD_STATE_CANCELED) {
    return DVD_RESULT_CANCELED;
  }
  return DVD_RESULT_FATAL_ERROR;
}

int DVDSeekAsyncPrio(DVDFileInfo* fileInfo, s32 offset, void (*callback)(s32, DVDFileInfo*), s32 prio) {
  assert(fileInfo);
  assert(!(offset & 3));
  assert((0 <= offset) && (offset <= static_cast<s32>(fileInfo->length)));

  fileInfo->callback = callback;
  DVDSeekAbsAsyncPrio(&fileInfo->cb, offset, cbForSeekAsync, prio);
  return 1;
}

s32 DVDSeekPrio(DVDFileInfo* fileInfo, s32 offset, s32 prio) {
  if (!DVDSeekAsyncPrio(fileInfo, offset, nullptr, prio)) {
    return DVD_RESULT_FATAL_ERROR;
  }
  s_worker.wait(&fileInfo->cb);
  const s32 state = atomic_load_acquire(fileInfo->cb.state);
  if (state == DVD_STATE_END) {
    return DVD_RESULT_GOOD;
  }
  if (state == DVD_STATE_CANCELED) {
    return DVD_RESULT_CANCELED;
  }
  return DVD_RESULT_FATAL_ERROR;
}

s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo) {
  if (fileInfo == nullptr) {
    return DVD_STATE_END;
  }
  return atomic_load_acquire(fileInfo->cb.state);
}

BOOL DVDFastOpenDir(s32 entrynum, DVDDir* dir) {
  std::lock_guard lock(s_fstLock);

  if (!isValidEntryNum(entrynum) || dir == nullptr) {
    return FALSE;
  }

  const auto fstIndex = s_entryNumToFstIndex[entrynum];
  if (!s_fstEntries[fstIndex].isDir) {
    return FALSE;
  }

  dir->entryNum = static_cast<u32>(entrynum);
  dir->location = static_cast<u32>(fstIndex) + 1;
  dir->next = s_fstEntries[fstIndex].nextOrLength;
  return TRUE;
}

int DVDOpenDir(const char* dirName, DVDDir* dir) {
  s32 entrynum = DVDConvertPathToEntrynum(dirName);
  if (entrynum < 0) {
    return FALSE;
  }
  return DVDFastOpenDir(entrynum, dir);
}

int DVDReadDir(DVDDir* dir, DVDDirEntry* dirent) {
  if (dir == nullptr || dirent == nullptr) {
    return FALSE;
  }

  std::lock_guard lock(s_fstLock);

  if (dir->location >= dir->next || dir->location >= s_fstEntries.size()) {
    return FALSE;
  }

  const u32 index = dir->location;
  FSTEntry& entry = s_fstEntries[index];
  dirent->entryNum = static_cast<u32>(entry.origEntryNum);
  dirent->isDir = entry.isDir ? TRUE : FALSE;
  dirent->name = entry.name.empty() ? nullptr : entry.name.data();

  if (entry.isDir) {
    const u32 next = entry.nextOrLength;
    dir->location = (next > index) ? next : index + 1;
  } else {
    dir->location = index + 1;
  }
  return TRUE;
}

int DVDCloseDir(DVDDir* dir) {
  (void)dir;
  return TRUE;
}

void DVDRewindDir(DVDDir* dir) {
  if (dir == nullptr) {
    return;
  }

  std::lock_guard lock(s_fstLock);

  if (!isValidEntryNum(static_cast<s32>(dir->entryNum))) {
    return;
  }

  dir->location = static_cast<u32>(s_entryNumToFstIndex[dir->entryNum]) + 1;
}

BOOL DVDLowReadDiskID(DVDDiskID* diskID, DVDLowCallback callback) {
  if (diskID != nullptr) {
    *diskID = s_diskID;
  }
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowStopMotor(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowRequestError(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowInquiry(DVDDriveInfo* info, DVDLowCallback callback) {
  if (info != nullptr) {
    std::memset(info, 0, sizeof(*info));
  }
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowAudioStream(u32 subcmd, u32 length, u32 offset, DVDLowCallback callback) {
  (void)subcmd;
  (void)length;
  (void)offset;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowRequestAudioStatus(u32 subcmd, DVDLowCallback callback) {
  (void)subcmd;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

BOOL DVDLowAudioBufferConfig(BOOL enable, u32 size, DVDLowCallback callback) {
  (void)enable;
  (void)size;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}

void DVDLowReset(void) {
  if (s_resetCoverCallback != nullptr) {
    s_resetCoverCallback(0);
  }
}

DVDLowCallback DVDLowSetResetCoverCallback(DVDLowCallback callback) {
  DVDLowCallback previous = s_resetCoverCallback;
  s_resetCoverCallback = callback;
  return previous;
}

BOOL DVDLowBreak(void) { return TRUE; }

DVDLowCallback DVDLowClearCallback(void) {
  DVDLowCallback previous = s_resetCoverCallback;
  s_resetCoverCallback = nullptr;
  return previous;
}

u32 DVDLowGetCoverStatus(void) { return s_initialized ? 0 : 1; }

void DVDDumpWaitingQueue(void) {}

}  // extern "C"
}  // namespace dusk::sw::dvd
