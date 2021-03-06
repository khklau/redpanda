// Copyright 2020 Vectorized, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "syschecks/syschecks.h"

#include "likely.h"
#include "version.h"

#include <seastar/core/memory.hh>
#include <seastar/core/seastar.hh>

#include <systemd/sd-daemon.h>

namespace syschecks {
ss::logger checklog{"syschecks"};

ss::future<> disk(const ss::sstring& path) {
    return ss::check_direct_io_support(path).then([path] {
        return ss::file_system_at(path).then([path](auto fs) {
            if (fs != ss::fs_type::xfs) {
                checklog.error(
                  "Path: `{}' is not on XFS. This is a non-supported "
                  "setup. "
                  "Expect poor performance.",
                  path);
            }
        });
    });
}

void memory(bool ignore) {
    static const uint64_t kMinMemory = 1 << 30;
    const auto shard_mem = ss::memory::stats().total_memory();
    if (shard_mem >= kMinMemory) {
        return;
    }
    auto line = fmt::format(
      "Memory: '{}' below recommended: '{}'", shard_mem, kMinMemory);
    checklog.error(line.c_str());
    if (!ignore) {
        throw std::runtime_error(line);
    }
}

void systemd_notify_ready() {
    ss::sstring msg = fmt::format(
      "READY=1\nSTATUS=redpanda is ready! - {}", redpanda_version());
    systemd_raw_message(msg);
}

// TODO:agallego - support non-systemd installations
// by adding a macro around sd_notify to disable at compile time
void systemd_raw_message(const ss::sstring& out) {
    auto r = sd_notify(0, out.c_str());
    if (out.back() == '\n') {
        checklog.debug("{}", std::string_view(out.c_str(), out.size() - 1));
    } else {
        checklog.debug("{}", out);
    }
    if (unlikely(r < 0)) {
        checklog.trace("Could not notify systemd sd_notify ready, error:{}", r);
    }
}

} // namespace syschecks
