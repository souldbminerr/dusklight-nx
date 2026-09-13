#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stddef.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>


extern "C" {

int flock(int fd, int operation) {
  (void)fd;
  (void)operation;
  return 0;
}

uid_t getuid(void) {
  return 0;
}

uid_t geteuid(void) {
  return 0;
}

uid_t getgid(void) {
  return 0;
}

gid_t getegid(void) {
  return 0;
}

int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
  (void)how;
  (void)set;
  (void)oldset;
  return 0;
}

int fchown(int fd, uid_t owner, gid_t group) {
  (void)fd;
  (void)owner;
  (void)group;
  return 0;
}

int dirfd(DIR* dirp) {
  (void)dirp;
  return -1;
}

int fstatat(int dirfd_in, const char* pathname, struct stat* buf, int flags) {
  (void)dirfd_in;
  (void)pathname;
  (void)buf;
  (void)flags;
  errno = ENOSYS;
  return -1;
}

int getpwuid_r(uid_t uid, struct passwd* pwd, char* buf, size_t buflen, struct passwd** result) {
  (void)uid;
  (void)pwd;
  (void)buf;
  (void)buflen;
  if (result != nullptr) {
    *result = nullptr;
  }
  return ENOSYS;
}

int execv(const char* path, char* const argv[]) {
  (void)path;
  (void)argv;
  errno = ENOSYS;
  return -1;
}

int execvp(const char* file, char* const argv[]) {
  (void)file;
  (void)argv;
  errno = ENOSYS;
  return -1;
}

pid_t waitpid(pid_t pid, int* status, int options) {
  (void)pid;
  (void)status;
  (void)options;
  errno = ECHILD;
  return -1;
}

long sysconf(int name) {
  // Numbers match newlib sys/unistd.h (_SC_*).
  switch (name) {
    case 8:  // _SC_PAGESIZE
      return 4096;
    case 9:  // _SC_NPROCESSORS_CONF
    case 10:  // _SC_NPROCESSORS_ONLN
      return 4;  // Tegra X1 (A57 x4)
    case 11:  // _SC_PHYS_PAGES
    case 12:  // _SC_AVPHYS_PAGES
      return (4L * 1024 * 1024 * 1024) / 4096;  // ~4 GiB addressable
    case 4:  // _SC_OPEN_MAX
      return 128;
    case 2:  // _SC_CLK_TCK
      return 100;
    default:
      errno = EINVAL;
      return -1;
  }
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
  if (memptr == nullptr || alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) {
    return EINVAL;
  }
  if (size == 0) {
    size = 1;
  }
  // newlib memalign() is free()-compatible, unlike manual over-allocation.
  void* p = memalign(alignment, size);
  if (p == nullptr) {
    return ENOMEM;
  }
  *memptr = p;
  return 0;
}

// Minimal POSIX regex
int regcomp(regex_t* preg, const char* regex, int cflags) {
  (void)preg;
  (void)regex;
  (void)cflags;
  return 0;
}

int regexec(const regex_t* preg, const char* string, size_t nmatch, regmatch_t* pmatch, int eflags) {
  (void)preg;
  (void)string;
  (void)nmatch;
  (void)pmatch;
  (void)eflags;
  return REG_NOMATCH;
}

void regfree(regex_t* preg) {
  (void)preg;
}

size_t regerror(int errcode, const regex_t* preg, char* errbuf, size_t errbuf_size) {
  (void)errcode;
  (void)preg;
  static const char msg[] = "regex not supported on Switch";
  if (errbuf != nullptr && errbuf_size > 0) {
    size_t n = sizeof(msg) < errbuf_size ? sizeof(msg) : errbuf_size;
    for (size_t i = 0; i < n; i++) {
      errbuf[i] = msg[i];
    }
    errbuf[errbuf_size - 1] = '\0';
  }
  return sizeof(msg);
}

}  // extern "C"
