// Package removal helper. pidfds keep every signal tied to the process inspected,
// even when a numeric PID is recycled while removal is in progress.
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 2 || argv[1][0] != '/') return 2;
  const std::string binary(argv[1]);
  std::vector<pollfd> hosts;
  DIR *proc = opendir("/proc");
  if (!proc) return 1;
  bool failed = false;
  while (auto *entry = readdir(proc)) {
    char *end = nullptr;
    long pid = std::strtol(entry->d_name, &end, 10);
    if (!*entry->d_name || *end || pid <= 0 || pid == getpid()) continue;
    int fd = static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
    if (fd < 0) {
      if (errno == ENOSYS) { std::fprintf(stderr, "Umbra removal requires pidfd support (Linux 5.3+)\n"); failed = true; break; }
      if (errno != ESRCH) { std::perror("Umbra: pidfd_open"); failed = true; }
      continue;
    }
    char image[4096];
    auto path = std::string("/proc/") + entry->d_name + "/exe";
    auto length = readlink(path.c_str(), image, sizeof(image));
    std::string actual = length > 0 ? std::string(image, length) : std::string();
    if (actual == binary || actual == binary + " (deleted)") hosts.push_back({fd, POLLIN, 0});
    else close(fd);
  }
  closedir(proc);
  auto signal_all = [&](int signal) {
    for (auto &host : hosts) {
      if (host.revents & POLLIN) continue;
      if (syscall(SYS_pidfd_send_signal, host.fd, signal, nullptr, 0) < 0 && errno != ESRCH) {
        std::perror("Umbra: cannot signal owned host"); failed = true;
      }
    }
  };
  auto wait_all = [&](int milliseconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (true) {
      std::vector<pollfd> pending;
      for (auto &host : hosts) if (!(host.revents & POLLIN)) pending.push_back(host);
      if (pending.empty()) return true;
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (left <= 0) return false;
      if (poll(pending.data(), pending.size(), static_cast<int>(left)) < 0 && errno != EINTR) return false;
      for (auto &item : pending) for (auto &host : hosts) if (host.fd == item.fd) host.revents = item.revents;
    }
  };
  signal_all(SIGTERM);
  if (!wait_all(30000)) { signal_all(SIGKILL); if (!wait_all(5000)) failed = true; }
  for (auto &host : hosts) close(host.fd);
  return failed ? 1 : 0;
}
