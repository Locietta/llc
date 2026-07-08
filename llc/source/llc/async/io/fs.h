#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "llc/async/runtime/task.h"
#include "llc/async/vocab/error.h"
#include "llc/async/vocab/owned.h"

namespace llc {

class EventLoop;

namespace fs {

using FileTime = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

struct FileStats {
    /// Device ID containing the file.
    std::uint64_t dev = 0;

    /// File type and mode (permissions).
    std::uint64_t mode = 0;

    /// Number of hard links.
    std::uint64_t nlink = 0;

    /// User ID of owner.
    std::uint64_t uid = 0;

    /// Group ID of owner.
    std::uint64_t gid = 0;

    /// Device ID (if special file).
    std::uint64_t rdev = 0;

    /// Inode number.
    std::uint64_t ino = 0;

    /// Total size in bytes.
    std::uint64_t size = 0;

    /// Preferred I/O block size.
    std::uint64_t blksize = 0;

    /// Number of 512-byte blocks allocated.
    std::uint64_t blocks = 0;

    /// File flags (BSD-specific).
    std::uint64_t flags = 0;

    /// File generation number (BSD-specific).
    std::uint64_t gen = 0;

    /// Last access time.
    FileTime atime;

    /// Last modification time.
    FileTime mtime;

    /// Last status change time.
    FileTime ctime;

    /// Creation (birth) time.
    FileTime birthtime;
};

struct MkstempResult {
    int fd = -1;
    std::string path;
};

struct Dirent {
    enum class Type {
        UNKNOWN,     // type not known
        FILE,        // regular file
        DIR,         // directory
        LINK,        // symlink
        FIFO,        // FIFO/Pipe
        SOCKET,      // socket
        CHAR_DEVICE, // character device
        BLOCK_DEVICE // block device
    };
    std::string name;
    Type kind = Type::UNKNOWN;
};

struct CopyfileOptions {
    /// Fail if destination exists.
    bool excl = false;

    /// Try to clone via copy-on-write if supported.
    bool clone = false;

    /// Force clone (may fall back to copy on failure).
    bool clone_force = false;
};

class DirHandle {
public:
    DirHandle() = default;
    DirHandle(DirHandle &&other) noexcept;
    DirHandle &operator=(DirHandle &&other) noexcept;

    DirHandle(const DirHandle &) = delete;
    DirHandle &operator=(const DirHandle &) = delete;

    bool valid() const noexcept;
    void *native_handle() const noexcept;
    void reset() noexcept;

    static DirHandle from_native(void *ptr);

private:
    explicit DirHandle(void *ptr);

    void *dir = nullptr;
};

/// Remove a file.
Task<void, Error> unlink(std::string_view path, EventLoop &loop = EventLoop::current());

/// Create a directory with the given permissions.
Task<void, Error> mkdir(std::string_view path, int mode, EventLoop &loop = EventLoop::current());

/// Get file status (metadata) by path.
Task<FileStats, Error> stat(std::string_view path, EventLoop &loop = EventLoop::current());

/// Copy a file from path to new_path.
Task<void, Error> copyfile(std::string_view path,
                           std::string_view new_path,
                           CopyfileOptions options = CopyfileOptions{},
                           EventLoop &loop = EventLoop::current());

/// Create a unique temporary directory from a template (must end with "XXXXXX").
Task<std::string, Error> mkdtemp(std::string_view tpl, EventLoop &loop = EventLoop::current());

/// Create a unique temporary file from a template (must end with "XXXXXX").
Task<MkstempResult, Error> mkstemp(std::string_view tpl, EventLoop &loop = EventLoop::current());

/// Remove an empty directory.
Task<void, Error> rmdir(std::string_view path, EventLoop &loop = EventLoop::current());

/// Scan a directory, returning all entries at once.
Task<std::vector<Dirent>, Error> scandir(std::string_view path,
                                         EventLoop &loop = EventLoop::current());

/// Open a directory for iterative reading.
Task<DirHandle, Error> opendir(std::string_view path, EventLoop &loop = EventLoop::current());

/// Read a batch of entries from an opened directory.
Task<std::vector<Dirent>, Error> readdir(DirHandle &dir, EventLoop &loop = EventLoop::current());

/// Close an opened directory handle.
Task<void, Error> closedir(DirHandle &dir, EventLoop &loop = EventLoop::current());

/// Get file status by file descriptor.
Task<FileStats, Error> fstat(int fd, EventLoop &loop = EventLoop::current());

/// Get file status by path, without following symlinks.
Task<FileStats, Error> lstat(std::string_view path, EventLoop &loop = EventLoop::current());

/// Rename (move) a file or directory.
Task<void, Error> rename(std::string_view path,
                         std::string_view new_path,
                         EventLoop &loop = EventLoop::current());

/// Flush file data and metadata to disk.
Task<void, Error> fsync(int fd, EventLoop &loop = EventLoop::current());

/// Flush file data to disk (metadata may not be flushed).
Task<void, Error> fdatasync(int fd, EventLoop &loop = EventLoop::current());

/// Truncate a file to the specified length.
Task<void, Error> ftruncate(int fd, std::int64_t offset, EventLoop &loop = EventLoop::current());

/// Zero-copy transfer data between file descriptors.
Task<std::int64_t, Error> sendfile(int out_fd,
                                   int in_fd,
                                   std::int64_t in_offset,
                                   std::size_t length,
                                   EventLoop &loop = EventLoop::current());

/// Check user permissions for a file (mode: F_OK, R_OK, W_OK, X_OK).
Task<void, Error> access(std::string_view path, int mode, EventLoop &loop = EventLoop::current());

/// Change file permissions by path.
Task<void, Error> chmod(std::string_view path, int mode, EventLoop &loop = EventLoop::current());

/// Change file access and modification times by path.
Task<void, Error> utime(std::string_view path,
                        double atime,
                        double mtime,
                        EventLoop &loop = EventLoop::current());

/// Change file access and modification times by file descriptor.
Task<void, Error>
futime(int fd, double atime, double mtime, EventLoop &loop = EventLoop::current());

/// Change file access and modification times by path, without following symlinks.
Task<void, Error> lutime(std::string_view path,
                         double atime,
                         double mtime,
                         EventLoop &loop = EventLoop::current());

/// Create a hard link.
Task<void, Error> link(std::string_view path,
                       std::string_view new_path,
                       EventLoop &loop = EventLoop::current());

/// Create a symbolic link.
Task<void, Error> symlink(std::string_view path,
                          std::string_view new_path,
                          int flags = 0,
                          EventLoop &loop = EventLoop::current());

/// Read the target of a symbolic link.
Task<std::string, Error> readlink(std::string_view path, EventLoop &loop = EventLoop::current());

/// Resolve a path to its canonical absolute pathname.
Task<std::string, Error> realpath(std::string_view path, EventLoop &loop = EventLoop::current());

/// Change file permissions by file descriptor.
Task<void, Error> fchmod(int fd, int mode, EventLoop &loop = EventLoop::current());

/// Change file owner and group by path.
Task<void, Error> chown(std::string_view path,
                        std::uint32_t uid,
                        std::uint32_t gid,
                        EventLoop &loop = EventLoop::current());

/// Change file owner and group by file descriptor.
Task<void, Error>
fchown(int fd, std::uint32_t uid, std::uint32_t gid, EventLoop &loop = EventLoop::current());

/// Change file owner and group by path, without following symlinks.
Task<void, Error> lchown(std::string_view path,
                         std::uint32_t uid,
                         std::uint32_t gid,
                         EventLoop &loop = EventLoop::current());

struct FsStats {
    /// Filesystem type identifier.
    std::uint64_t type = 0;

    /// Fundamental block size in bytes.
    std::uint64_t bsize = 0;

    /// Total number of blocks on the filesystem.
    std::uint64_t blocks = 0;

    /// Number of free blocks.
    std::uint64_t bfree = 0;

    /// Number of free blocks available to unprivileged users.
    std::uint64_t bavail = 0;

    /// Total number of file nodes (inodes).
    std::uint64_t files = 0;

    /// Number of free file nodes.
    std::uint64_t ffree = 0;

    /// Fragment size in bytes.
    std::uint64_t frsize = 0;
};

/// Get filesystem statistics (total/free space, inode counts, etc.).
Task<FsStats, Error> statfs(std::string_view path, EventLoop &loop = EventLoop::current());

/// Open a file asynchronously. Returns the file descriptor on success.
Task<int, Error>
open(std::string_view path, int flags, int mode = 0, EventLoop &loop = EventLoop::current());

/// Read from a file descriptor into a buffer. offset = -1 uses current position.
Task<std::size_t, Error> read(int fd,
                              std::span<char> buf,
                              std::int64_t offset = -1,
                              EventLoop &loop = EventLoop::current());

/// Write a buffer to a file descriptor. offset = -1 uses current position.
Task<std::size_t, Error> write(int fd,
                               std::span<const char> buf,
                               std::int64_t offset = -1,
                               EventLoop &loop = EventLoop::current());

/// Close a file descriptor asynchronously.
Task<void, Error> close(int fd, EventLoop &loop = EventLoop::current());

namespace sync {

/// Open a file. Returns the fd on success.
/// flags: UV_FS_O_RDONLY, UV_FS_O_WRONLY, UV_FS_O_RDWR, etc.
Result<int> open(std::string_view path, int flags, int mode = 0);

/// Read up to buf.size() bytes from fd at offset (-1 = current position).
Result<std::size_t> read(int fd, std::span<char> buf, std::int64_t offset = -1);

/// Write buf to fd at offset (-1 = current position).
Result<std::size_t> write(int fd, std::span<const char> buf, std::int64_t offset = -1);

/// Close a file descriptor.
Error close(int fd);

/// Convenience: read entire file into a string.
Result<std::string> read_to_string(std::string_view path);

} // namespace sync

} // namespace fs

} // namespace llc
