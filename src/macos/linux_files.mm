#include "linux_files.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <ctime>

#include <mach/mach.h>
#include <qbytearray.h>
#include <qhash.h>
#include <qstring.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#import <Foundation/Foundation.h>

namespace qs::macos {

namespace {

constexpr quint64 BYTES_PER_KIBIBYTE = 1024;

QByteArray sysctlString(const char* name) {
	size_t size = 0;
	if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return {};
	QByteArray value(static_cast<qsizetype>(size), '\0');
	if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) return {};
	return QByteArray(value.constData());
}

QByteArray quoted(const QByteArray& value) { return '"' + value + '"'; }

QByteArray osRelease() {
	NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
	auto versionString = QByteArray::number(version.majorVersion) + '.' + QByteArray::number(version.minorVersion);
	if (version.patchVersion > 0) versionString += '.' + QByteArray::number(version.patchVersion);

	return "NAME=\"macOS\"\n"
	       "PRETTY_NAME="
	     + quoted("macOS " + versionString)
	     + "\nID=macos\n"
	       "VERSION_ID="
	     + quoted(versionString) + "\nVERSION=" + quoted(versionString)
	     + "\nLOGO=apple.logo\n"
	       "HOME_URL=\"https://www.apple.com/macos/\"\n";
}

QByteArray hostName() {
	std::array<char, 256> name {};
	if (gethostname(name.data(), name.size()) != 0) return "localhost\n";
	auto host = QByteArray(name.data());
	if (host.endsWith(".local")) host.chop(6);
	return host + '\n';
}

QByteArray kernelRelease() {
	utsname system {};
	if (uname(&system) != 0) return {};
	return QByteArray(system.release) + '\n';
}

QByteArray kernelVersion() {
	utsname system {};
	if (uname(&system) != 0) return {};
	return QByteArray(system.sysname) + " version " + system.release + " (" + system.version + ")\n";
}

QByteArray uptime() {
	timeval bootTime {};
	size_t size = sizeof(bootTime);
	if (sysctlbyname("kern.boottime", &bootTime, &size, nullptr, 0) != 0) return {};
	timeval now {};
	gettimeofday(&now, nullptr);
	auto seconds = static_cast<double>(now.tv_sec - bootTime.tv_sec)
	             + static_cast<double>(now.tv_usec - bootTime.tv_usec) / 1e6;
	return QByteArray::number(seconds, 'f', 2) + ' ' + QByteArray::number(0.0, 'f', 2) + '\n';
}

QByteArray loadAverage() {
	std::array<double, 3> averages {};
	if (getloadavg(averages.data(), 3) != 3) return {};
	return QByteArray::number(averages[0], 'f', 2) + ' ' + QByteArray::number(averages[1], 'f', 2) + ' '
	     + QByteArray::number(averages[2], 'f', 2) + " 1/1 " + QByteArray::number(getpid()) + '\n';
}

QByteArray memoryInfo() {
	quint64 totalBytes = 0;
	size_t size = sizeof(totalBytes);
	if (sysctlbyname("hw.memsize", &totalBytes, &size, nullptr, 0) != 0) return {};

	vm_statistics64_data_t statistics {};
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &count)
	    != KERN_SUCCESS)
	{
		return {};
	}

	auto pageBytes = static_cast<quint64>(vm_kernel_page_size);
	auto appPages = statistics.internal_page_count > statistics.purgeable_count
	                  ? statistics.internal_page_count - statistics.purgeable_count
	                  : 0;
	auto usedBytes = std::min<quint64>(
	    totalBytes,
	    (appPages + statistics.wire_count + statistics.compressor_page_count) * pageBytes
	);
	auto cachedBytes = (static_cast<quint64>(statistics.external_page_count) + statistics.purgeable_count) * pageBytes;

	xsw_usage swap {};
	size = sizeof(swap);
	sysctlbyname("vm.swapusage", &swap, &size, nullptr, 0);

	auto line = [](const char* key, quint64 bytes) {
		auto label = QByteArray(key) + ':';
		return label.leftJustified(16, ' ') + QByteArray::number(bytes / BYTES_PER_KIBIBYTE).rightJustified(8, ' ')
		     + " kB\n";
	};

	return line("MemTotal", totalBytes) + line("MemFree", statistics.free_count * pageBytes)
	     + line("MemAvailable", totalBytes - usedBytes) + line("Buffers", 0) + line("Cached", cachedBytes)
	     + line("SwapCached", 0) + line("Active", statistics.active_count * pageBytes)
	     + line("Inactive", statistics.inactive_count * pageBytes) + line("SwapTotal", swap.xsu_total)
	     + line("SwapFree", swap.xsu_avail) + line("Shmem", 0);
}

QByteArray processorStatistics() {
	natural_t processorCount = 0;
	processor_info_array_t processorInfo = nullptr;
	mach_msg_type_number_t infoCount = 0;
	if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &processorCount, &processorInfo, &infoCount)
	    != KERN_SUCCESS)
	{
		return {};
	}

	auto formatLine = [](const QByteArray& label, quint64 user, quint64 nice, quint64 system, quint64 idle) {
		return label + ' ' + QByteArray::number(user) + ' ' + QByteArray::number(nice) + ' ' + QByteArray::number(system)
		     + ' ' + QByteArray::number(idle) + " 0 0 0 0 0 0\n";
	};

	std::array<quint64, CPU_STATE_MAX> totals {};
	QByteArray perProcessor;
	for (natural_t processor = 0; processor < processorCount; ++processor) {
		const auto* ticks = reinterpret_cast<const processor_cpu_load_info*>(processorInfo) + processor;
		for (int state = 0; state < CPU_STATE_MAX; ++state) totals[state] += ticks->cpu_ticks[state];
		perProcessor += formatLine(
		    "cpu" + QByteArray::number(processor),
		    ticks->cpu_ticks[CPU_STATE_USER],
		    ticks->cpu_ticks[CPU_STATE_NICE],
		    ticks->cpu_ticks[CPU_STATE_SYSTEM],
		    ticks->cpu_ticks[CPU_STATE_IDLE]
		);
	}
	vm_deallocate(
	    mach_task_self(),
	    reinterpret_cast<vm_address_t>(processorInfo),
	    static_cast<vm_size_t>(infoCount) * sizeof(integer_t)
	);

	timeval bootTime {};
	size_t size = sizeof(bootTime);
	sysctlbyname("kern.boottime", &bootTime, &size, nullptr, 0);

	return formatLine(
	           "cpu ",
	           totals[CPU_STATE_USER],
	           totals[CPU_STATE_NICE],
	           totals[CPU_STATE_SYSTEM],
	           totals[CPU_STATE_IDLE]
	       )
	     + perProcessor + "btime " + QByteArray::number(static_cast<qint64>(bootTime.tv_sec)) + '\n';
}

QByteArray processorInfo() {
	int processorCount = 0;
	size_t size = sizeof(processorCount);
	sysctlbyname("hw.logicalcpu", &processorCount, &size, nullptr, 0);
	auto brand = sysctlString("machdep.cpu.brand_string");

	QByteArray info;
	for (int processor = 0; processor < processorCount; ++processor) {
		info += "processor\t: " + QByteArray::number(processor) + "\nvendor_id\t: Apple\nmodel name\t: " + brand
		      + "\ncpu cores\t: " + QByteArray::number(processorCount) + "\n\n";
	}
	return info;
}

}

std::optional<QByteArray> linuxPseudoFile(const QString& path) {
	using Generator = QByteArray (*)();
	static const QHash<QString, Generator> generators = {
	    {QStringLiteral("/etc/os-release"), osRelease},
	    {QStringLiteral("/usr/lib/os-release"), osRelease},
	    {QStringLiteral("/proc/sys/kernel/hostname"), hostName},
	    {QStringLiteral("/etc/hostname"), hostName},
	    {QStringLiteral("/proc/sys/kernel/osrelease"), kernelRelease},
	    {QStringLiteral("/proc/version"), kernelVersion},
	    {QStringLiteral("/proc/uptime"), uptime},
	    {QStringLiteral("/proc/loadavg"), loadAverage},
	    {QStringLiteral("/proc/meminfo"), memoryInfo},
	    {QStringLiteral("/proc/stat"), processorStatistics},
	    {QStringLiteral("/proc/cpuinfo"), processorInfo},
	    {QStringLiteral("/sys/class/dmi/id/sys_vendor"), [] { return QByteArray("Apple Inc.\n"); }},
	    {QStringLiteral("/sys/class/dmi/id/product_name"), [] { return sysctlString("hw.model") + '\n'; }},
	};

	auto generator = generators.value(path);
	if (!generator) return std::nullopt;
	auto content = generator();
	if (content.isEmpty()) return std::nullopt;
	return content;
}

}
