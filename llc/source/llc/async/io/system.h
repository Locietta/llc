#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "llc/async/vocab/error.h"

namespace llc::sys {

/// System memory information (all values in bytes).
struct MemoryInfo {
    /// Total physical memory installed on the system.
    std::uint64_t total = 0;

    /// Physical memory not currently in use.
    std::uint64_t free = 0;

    /// Memory available to the Process, accounting for cgroup/container
    /// limits.  Falls back to free memory when no limit is set.
    std::uint64_t available = 0;

    /// Memory limit imposed by the OS (cgroups on Linux).  Zero means
    /// unknown/no constraint; UINT64_MAX means a constraint mechanism exists
    /// but no limit is set.
    std::uint64_t constrained = 0;
};

/// Per-CPU core timing snapshot.
struct CpuTimes {
    /// Time spent running user-space processes.
    std::chrono::milliseconds user{};

    /// Time spent running niced user-space processes.
    std::chrono::milliseconds nice{};

    /// Time spent running kernel-space code.
    std::chrono::milliseconds sys{};

    /// Time spent idle.
    std::chrono::milliseconds idle{};

    /// Time spent servicing hardware interrupts.
    std::chrono::milliseconds irq{};
};

/// Information about a single logical CPU core.
struct CpuCore {
    /// CPU model name (e.g. "Intel(R) Core(TM) i7-10700K").
    std::string model;

    /// Clock speed in MHz.  May be zero on some virtualized environments.
    int speed_mhz = 0;

    /// Cumulative timing breakdown for this core.
    CpuTimes times;
};

/// Snapshot of a Process's resource usage.
struct ProcessStat {
    /// Process ID.
    int pid = -1;

    /// Resident set size in bytes (physical memory).
    std::size_t rss = 0;

    /// Virtual memory size in bytes.
    std::size_t vsize = 0;

    /// User-mode CPU time.
    std::chrono::microseconds user_time{};

    /// Kernel-mode CPU time.
    std::chrono::microseconds system_time{};

    /// Peak resident set size in bytes.
    std::size_t max_rss = 0;

    /// Page faults serviced without I/O (minor faults).
    std::uint64_t minor_faults = 0;

    /// Page faults requiring disk I/O (major faults).
    std::uint64_t major_faults = 0;

    /// Context switches initiated by the Process yielding the CPU.
    std::uint64_t voluntary_context_switches = 0;

    /// Context switches forced by the scheduler.
    std::uint64_t involuntary_context_switches = 0;
};

/// Operating system identification.
struct UnameInfo {
    /// OS name (e.g. "Linux", "Darwin", "Windows_NT").
    std::string sysname;

    /// OS release version string.
    std::string release;

    /// Detailed version/build string.
    std::string version;

    /// Hardware architecture (e.g. "x86_64", "aarch64").
    std::string machine;
};

/// Retrieve the OS pid of the calling Process.
int pid() noexcept;

/// Query system memory information.
MemoryInfo memory();

/// Query the resident set size of the current Process (in bytes).
Result<std::size_t> resident_memory();

/// Query resource usage for a Process by pid (0 = current Process).
Result<ProcessStat> process(int pid = 0);

/// Query per-core CPU information.
Result<std::vector<CpuCore>> cpu_cores();

/// Return the number of CPUs available to the Process.
unsigned int parallelism();

/// Query OS identification strings.
Result<UnameInfo> uname();

/// Query the system hostname.
Result<std::string> hostname();

/// Query the system uptime.
Result<std::chrono::duration<double>> uptime();

/// Query the current user's home directory.
Result<std::string> home_directory();

/// Query the system temporary directory.
Result<std::string> temp_directory();

/// Get the scheduling priority of a Process.
/// Returns a nice value (-20..19) on Unix, or a UV_PRIORITY_* constant on
/// Windows.  The returned value may not exactly match a prior set_priority()
/// call on Windows due to priority class mapping.
/// @param pid  Process ID (0 = current Process).
Result<int> priority(int pid = 0);

/// Set the scheduling priority of a Process.
/// @param value  Nice value on Unix (-20..19); on Windows the value is mapped
///               to the nearest priority class.
/// @param pid    Process ID (0 = current Process).
Error set_priority(int value, int pid = 0);

} // namespace llc::sys
