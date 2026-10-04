#pragma once

// pqrs::filesystem v2.1.0

// (C) Copyright Takayama Fumihiko 2018.
// Distributed under the Boost Software License, Version 1.0.
// (See https://www.boost.org/LICENSE_1_0.txt)

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace pqrs::filesystem {

[[nodiscard]] inline std::optional<uid_t> uid(const std::string& path) noexcept {
  struct stat s;
  if (stat(path.c_str(), &s) == 0) {
    return s.st_uid;
  }
  return std::nullopt;
}

[[nodiscard]] inline std::optional<uid_t> symlink_uid(const std::string& path) noexcept {
  struct stat s;
  if (lstat(path.c_str(), &s) == 0) {
    return s.st_uid;
  }
  return std::nullopt;
}

[[nodiscard]] inline std::optional<gid_t> gid(const std::string& path) noexcept {
  struct stat s;
  if (stat(path.c_str(), &s) == 0) {
    return s.st_gid;
  }
  return std::nullopt;
}

[[nodiscard]] inline std::optional<gid_t> symlink_gid(const std::string& path) noexcept {
  struct stat s;
  if (lstat(path.c_str(), &s) == 0) {
    return s.st_gid;
  }
  return std::nullopt;
}

[[nodiscard]] inline bool is_owned(const std::string& path, uid_t uid) noexcept {
  return pqrs::filesystem::uid(path) == uid;
}

[[nodiscard]] inline bool is_symlink_owned(const std::string& path, uid_t uid) noexcept {
  return pqrs::filesystem::symlink_uid(path) == uid;
}

//
// read_file
//

struct read_file_options final {
  std::optional<size_t> max_size;
  // Unset permits any owner; an empty list permits none.
  std::optional<std::vector<uid_t>> allowed_owners;
};

struct read_file_error final {
  enum class reason {
    open_failed,
    stat_failed,
    read_failed,
    not_regular_file,
    invalid_owner,
    size_limit_exceeded,
  };

  reason type;
  // Preserves errno for OS failures, including EACCES and EPERM.
  std::error_code code;

  [[nodiscard]] std::string message() const {
    switch (type) {
      case reason::open_failed:
        return "open failed: " + code.message();
      case reason::stat_failed:
        return "stat failed: " + code.message();
      case reason::read_failed:
        return "read failed: " + code.message();
      case reason::not_regular_file:
        return "not a regular file";
      case reason::invalid_owner:
        return "not owned by an allowed user";
      case reason::size_limit_exceeded:
        return "file size limit exceeded";
    }
    return "unknown read error";
  }
};

// Follows symlinks, but validates and reads the same opened regular file.
// O_NONBLOCK avoids waiting for a FIFO writer; it does not make disk I/O asynchronous.
// A size limit is checked both before allocation and while reading to EOF.
[[nodiscard]] inline std::expected<std::vector<uint8_t>, read_file_error> read_file(
    const std::string& path,
    const read_file_options& options = {}) {
  struct file_descriptor final {
    int value;
    ~file_descriptor() {
      if (value >= 0) {
        ::close(value);
      }
    }
  };
  file_descriptor fd{::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)};
  if (fd.value < 0) {
    return std::unexpected(read_file_error{
        read_file_error::reason::open_failed,
        {errno, std::generic_category()},
    });
  }

  struct stat status{};
  if (::fstat(fd.value, &status) != 0) {
    return std::unexpected(read_file_error{
        read_file_error::reason::stat_failed,
        {errno, std::generic_category()},
    });
  }

  if (!S_ISREG(status.st_mode)) {
    return std::unexpected(read_file_error{
        read_file_error::reason::not_regular_file,
        {},
    });
  }

  if (options.allowed_owners &&
      !std::ranges::contains(*options.allowed_owners, status.st_uid)) {
    return std::unexpected(read_file_error{
        read_file_error::reason::invalid_owner,
        {},
    });
  }

  std::vector<uint8_t> contents;

  // Check the file size before reserve() to avoid allocating beyond the limit.
  auto limit = std::min(options.max_size.value_or(contents.max_size()),
                        contents.max_size());
  if (!std::in_range<size_t>(status.st_size) ||
      static_cast<size_t>(status.st_size) > limit) {
    return std::unexpected(read_file_error{
        read_file_error::reason::size_limit_exceeded,
        {},
    });
  }

  contents.reserve(static_cast<size_t>(status.st_size));
  std::array<uint8_t, 8192> buffer;
  for (;;) {
    auto size = ::read(fd.value,
                       buffer.data(),
                       buffer.size());
    if (size < 0) {
      auto error = errno;
      if (error == EINTR) {
        // Interrupted by a signal before reading any data; retry the read.
        continue;
      }
      return std::unexpected(read_file_error{
          read_file_error::reason::read_failed,
          {error, std::generic_category()},
      });
    }

    if (size == 0) {
      return contents;
    }

    if (static_cast<size_t>(size) > limit - contents.size()) {
      return std::unexpected(read_file_error{
          read_file_error::reason::size_limit_exceeded,
          {},
      });
    }

    contents.insert(contents.end(),
                    buffer.begin(),
                    buffer.begin() + size);
  }
}

} // namespace pqrs::filesystem
