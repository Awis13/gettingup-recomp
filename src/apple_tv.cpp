// Apple TV: an app has no Documents directory, only Caches, which the
// system may empty. The game's data directory (the guest's C: drive, the
// registry) is in Caches, and what must survive -- the saves and the
// registry, a few kilobytes -- is also kept in the app's preferences: a
// dictionary from a path below the data directory to the file's bytes.
// Restored at start when Caches lost it, backed up whenever it changes
// (checked every few seconds) and when the app goes to the background.

#include <CoreFoundation/CoreFoundation.h>
#include <SDL3/SDL.h>
#include <dirent.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// What is kept, relative to the data directory: files, or directories
// taken whole.
const char* const kKept[] = {"registry.txt", "drive_c/Documents and Settings"};
constexpr size_t kMaxBytes = 512 * 1024;  // well inside the preferences' budget
const CFStringRef kKey = CFSTR("saves");

std::string g_root;
std::mutex g_mu;
std::string g_stamp;  // of what was backed up last

void Collect(const std::string& rel, std::map<std::string, std::string>* out) {
  const std::string path = g_root + "/" + rel;
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return;
  if (S_ISREG(st.st_mode)) {
    std::ifstream f(path, std::ios::binary);
    (*out)[rel].assign(std::istreambuf_iterator<char>(f), {});
    return;
  }
  if (!S_ISDIR(st.st_mode)) return;
  if (DIR* d = opendir(path.c_str())) {
    while (dirent* e = readdir(d))
      if (std::string n = e->d_name; n != "." && n != "..") Collect(rel + "/" + n, out);
    closedir(d);
  }
}

std::map<std::string, std::string> Files() {
  std::map<std::string, std::string> files;
  for (const char* k : kKept) Collect(k, &files);
  return files;
}

std::string Stamp(const std::map<std::string, std::string>& files) {
  std::string s;
  for (const auto& [rel, bytes] : files) s += rel + '\n' + bytes;
  return s;
}

void MakeParents(const std::string& path) {
  for (size_t i = 1; i < path.size(); i++)
    if (path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

void Backup() {
  std::lock_guard<std::mutex> l(g_mu);
  const auto files = Files();
  std::string stamp = Stamp(files);
  if (stamp == g_stamp) return;
  if (stamp.size() > kMaxBytes) {
    std::fprintf(stderr, "apple tv: saves are %zu bytes, too many to back up\n", stamp.size());
    return;
  }
  CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
      nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  for (const auto& [rel, bytes] : files) {
    CFStringRef k = CFStringCreateWithCString(nullptr, rel.c_str(), kCFStringEncodingUTF8);
    CFDataRef v = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
                               CFIndex(bytes.size()));
    CFDictionarySetValue(dict, k, v);
    CFRelease(k);
    CFRelease(v);
  }
  CFPreferencesSetAppValue(kKey, dict, kCFPreferencesCurrentApplication);
  CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
  CFRelease(dict);
  g_stamp = std::move(stamp);
  std::fprintf(stderr, "apple tv: saves backed up (%zu files)\n", files.size());
}

void Restore() {
  CFPropertyListRef v = CFPreferencesCopyAppValue(kKey, kCFPreferencesCurrentApplication);
  if (!v) return;
  int restored = 0;
  if (CFGetTypeID(v) == CFDictionaryGetTypeID()) {
    const auto dict = static_cast<CFDictionaryRef>(v);
    const CFIndex n = CFDictionaryGetCount(dict);
    std::vector<const void*> keys(n), values(n);
    CFDictionaryGetKeysAndValues(dict, keys.data(), values.data());
    for (CFIndex i = 0; i < n; i++) {
      if (CFGetTypeID(keys[i]) != CFStringGetTypeID() ||
          CFGetTypeID(values[i]) != CFDataGetTypeID())
        continue;
      char rel[1024];
      if (!CFStringGetCString(static_cast<CFStringRef>(keys[i]), rel, sizeof rel,
                              kCFStringEncodingUTF8))
        continue;
      const std::string path = g_root + "/" + rel;
      struct stat st;
      if (stat(path.c_str(), &st) == 0) continue;  // Caches kept it
      const auto data = static_cast<CFDataRef>(values[i]);
      MakeParents(path);
      std::ofstream(path, std::ios::binary)
          .write(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), CFDataGetLength(data));
      restored++;
    }
  }
  CFRelease(v);
  std::fprintf(stderr, "apple tv: saves restored (%d files)\n", restored);
}

bool OnEvent(void*, SDL_Event* e) {
  if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND || e->type == SDL_EVENT_TERMINATING) Backup();
  return true;
}

}  // namespace

// Called before the runtime starts, with the game's data directory.
void AppleTvSaves(const std::string& data_dir) {
  g_root = data_dir;
  Restore();
  g_stamp = Stamp(Files());
  std::thread([] {
    bool watching = false;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      if (!watching && SDL_WasInit(SDL_INIT_EVENTS)) watching = SDL_AddEventWatch(OnEvent, nullptr);
      Backup();
    }
  }).detach();
}
