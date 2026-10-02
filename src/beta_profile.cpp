/**
 * @file src/beta_profile.cpp
 * @brief Per-client config overrides for Eclipse Beta. BETA-ONLY — see beta_profile.h.
 */
#include "beta_profile.h"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

#include "config.h"
#include "logging.h"
#include "platform/common.h"

using namespace std::literals;

namespace beta_profile {
  namespace {
    using target_t = std::variant<int *, bool *>;
    using setting_t = std::variant<std::pair<int *, int>, std::pair<bool *, bool>>;

    struct knob_t {
      const char *name;
      target_t target;
    };

    std::vector<knob_t> knobs() {
      // These are read AFTER the HTTP hook: video.cpp encoder creation,
      // process.cpp display creation, and display_vram.cpp -> nvenc_base.cpp.
      return {
        {"gop_length", &config::video.gop_length},
        {"coop_pad_rows", &config::video.coop_pad_rows},
        {"nvenc_h264_main", &config::video.nv.h264_main_profile},
      };
    }

    std::mutex mtx;
    std::weak_ptr<scope_t> active;
    std::uint64_t generation = 0;
    std::vector<setting_t> saved;
    std::vector<setting_t> current;

    void restore_locked() {
      for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
        std::visit([](const auto &s) { *s.first = s.second; }, *it);
      }
      if (!saved.empty()) {
        BOOST_LOG(info) << "beta_profile: "sv << saved.size() << " override(s) restored after lease release"sv;
      }
      saved.clear();
      current.clear();
    }

    std::string trim(std::string s) {
      const auto b = s.find_first_not_of(" \t\r\n");
      if (b == std::string::npos) {
        return {};
      }
      return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    }

    struct profile_t {
      std::vector<std::string> clients;
      std::vector<std::string> uuids;
      std::vector<std::pair<std::string, std::string>> values;
    };

    profile_t load() {
      profile_t p;
      std::ifstream in(platf::appdata().string() + "/beta_profile.conf");
      std::string line;
      while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
          continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
          BOOST_LOG(warning) << "beta_profile: ignoring line without '=': "sv << line;
          continue;
        }
        const auto key = trim(line.substr(0, eq));
        const auto val = trim(line.substr(eq + 1));
        if (key == "client" || key == "client_uuid") {
          if (!val.empty()) {
            (key == "client" ? p.clients : p.uuids).push_back(val);
          }
        } else {
          p.values.emplace_back(key, val);
        }
      }
      return p;
    }

    setting_t parse(const knob_t &knob, const std::string &value) {
      return std::visit([&](auto *target) -> setting_t {
        using value_t = std::remove_pointer_t<decltype(target)>;
        if constexpr (std::is_same_v<value_t, bool>) {
          if (value == "true" || value == "1") {
            return std::make_pair(target, true);
          }
          if (value == "false" || value == "0") {
            return std::make_pair(target, false);
          }
          throw std::runtime_error("beta_profile: '"s + knob.name + "' needs true/false or 1/0");
        } else {
          int number = 0;
          const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
          if (result.ec != std::errc {} || result.ptr != value.data() + value.size() || number < 0) {
            throw std::runtime_error("beta_profile: '"s + knob.name + "' needs a nonnegative integer");
          }
          return std::make_pair(target, number);
        }
      }, knob.target);
    }
  }  // namespace

  struct scope_t {
    explicit scope_t(std::uint64_t id): id(id) {}
    ~scope_t() {
      std::lock_guard lock(mtx);
      // If the last owner expired while waiting for this lock, the next acquire
      // may already have restored it. A stale destructor must not undo that session.
      if (id == generation) {
        restore_locked();
      }
    }
    const std::uint64_t id;
  };

  lease_t acquire_for_client(const std::string &client_name, const std::string &client_uuid) {
    // Declare before the lock: its last reference must be released AFTER unlocking.
    lease_t lease;
    lease_t existing;
    std::lock_guard lock(mtx);
    const auto p = load();
    bool match = false;
    for (const auto &uuid : p.uuids) {
      match = match || uuid == client_uuid;
    }
    for (const auto &name : p.clients) {
      match = match || name == client_name;
    }

    std::vector<setting_t> desired;
    if (match) {
      const auto ks = knobs();
      // Validate the entire profile before mutating even one setting.
      for (const auto &[key, value] : p.values) {
        const knob_t *found = nullptr;
        for (const auto &k : ks) {
          if (key == k.name) {
            found = &k;
            break;
          }
        }
        if (!found) {
          throw std::runtime_error("beta_profile: '"s + key + "' is not an overridable setting");
        }
        desired.push_back(parse(*found, value));
      }
    }

    existing = active.lock();
    if (existing) {
      if (desired != current) {
        throw std::runtime_error("beta_profile: an incompatible streaming profile is still active");
      }
      return existing;
    }

    restore_locked();
    ++generation;
    lease = std::make_shared<scope_t>(generation);
    // Allocate before applying; later assignments cannot allocate or throw.
    saved.reserve(desired.size());
    current = desired;
    for (std::size_t i = 0; i < desired.size(); ++i) {
      std::visit([&](const auto &s) {
        BOOST_LOG(info) << "beta_profile: "sv << p.values[i].first << " "sv << *s.first
                        << " -> "sv << s.second << " for this session"sv;
        saved.emplace_back(std::make_pair(s.first, *s.first));
        *s.first = s.second;
      }, desired[i]);
    }
    active = lease;
    if (match) {
      BOOST_LOG(info) << "beta_profile: BETA client uuid "sv << client_uuid << " -- "sv
                      << saved.size() << " override(s) applied before display/encoder setup"sv;
    }
    return lease;
  }
}  // namespace beta_profile
