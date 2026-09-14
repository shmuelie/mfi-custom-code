#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace mfi_mqtt_client {
	struct system_read_error {
		std::string message;
	};

	struct cpu_warmup {};

	struct memory_stats {
		double total_mib;
		double available_mib;
		double used_mib;
		double utilization;
		bool estimated;
	};

	using cpu_sample = std::variant<double, cpu_warmup, system_read_error>;
	using memory_sample = std::variant<memory_stats, system_read_error>;

	// Reads proc files directly, without stat, logging, sleeping, or background work.
	// Input is bounded to 4096 bytes per line and 256 KiB per read.
	class system_stats_reader final {
	public:
		explicit system_stats_reader(std::string proc_root = "/proc");

		// Aggregate utilization in [0, 100]; idle and iowait are non-busy.
		// First read, explicit reset, and recovery after any CPU error need a warmup.
		cpu_sample read_cpu();

		// Kernel "kB" means KiB; utilization is a percentage in [0, 100].
		// Without MemAvailable, estimates available as
		// MemFree + Buffers + Cached + SReclaimable - Shmem, clamped to [0, total].
		// Only absent SReclaimable/Shmem default to zero. This legacy estimate
		// does not account for kernel watermarks or unreclaimable cache.
		memory_sample read_memory() const;

		void reset_cpu() noexcept;

	private:
		struct cpu_baseline {
			std::array<std::uint64_t, 8> counters{};
			std::uint64_t total = 0;
			std::uint64_t idle = 0;
		};

		std::string _proc_root;
		std::optional<cpu_baseline> _cpu_baseline;
	};
}
