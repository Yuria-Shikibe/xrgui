// Include cross-platform headers for CPUID and XGETBV
#if defined(_MSC_VER)
	#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
	#include <cpuid.h>
	#include <immintrin.h>
#else
	#error "Unsupported compiler."
#endif

import std;

// Wrapper for cross-platform cpuid call
void get_cpuid(std::int32_t info[4], std::int32_t eax, std::int32_t ecx = 0) {
#if defined(_MSC_VER)
    __cpuidex(info, eax, ecx);
#elif defined(__GNUC__) || defined(__clang__)
    __cpuid_count(eax, ecx, info[0], info[1], info[2], info[3]);
#endif
}

// Check if OS supports saving YMM registers (required for AVX/AVX2)
bool check_os_avx_support() {
    std::int32_t info[4];
    get_cpuid(info, 1);

    // Check CPUID.01H:ECX.OSXSAVE[bit 27]
    if ((info[2] & (1 << 27)) == 0) {
        return false;
    }

    // Check XCR0 register to see if OS saves SSE (bit 1) and AVX (bit 2) state
    std::uint64_t xcr0 = _xgetbv(0);
    return (xcr0 & 6) == 6;
}

bool check_os_avx512_support() {
	std::int32_t info[4];
	get_cpuid(info, 1);

	if ((info[2] & (1 << 27)) == 0) {
		return false;
	}

	std::uint64_t xcr0 = _xgetbv(0);
	// XCR0 must have SSE(bit 1), AVX(bit 2),
	// opmask(bit 5), ZMM_Hi256(bit 6), and Hi16_ZMM(bit 7) set.
	// 11100110 in binary is 0xE6
	return (xcr0 & 0xE6) == 0xE6;
}

// Hardware (CPU) Support Checks
bool check_cpu_sse() {
    std::int32_t info[4];
    get_cpuid(info, 1);
    return (info[3] & (1 << 25)) != 0;
}

bool check_cpu_avx() {
    std::int32_t info[4];
    get_cpuid(info, 1);
    return (info[2] & (1 << 28)) != 0;
}

bool check_cpu_avx2() {
    std::int32_t info[4];
    get_cpuid(info, 7);
    return (info[1] & (1 << 5)) != 0;
}

bool check_cpu_avx512() {
    std::int32_t info[4];
    get_cpuid(info, 7);
    return (info[1] & (1 << 16)) != 0;
}

// Helper for formatted output, merging print and println
void report_status(std::string_view name, bool is_compiled, bool is_runtime_supported = false, std::string_view error_action = "crash") {
    if (!is_compiled) {
        std::println("[Compile-Option] {} Instructions: Disabled.", name);
    } else if (is_runtime_supported) {
        std::println("[Compile-Option] {} Instructions: Enabled.  -> [Run-time] Hardware and OS support {}, safe to execute.", name, name);
    } else {
        std::println("[Compile-Option] {} Instructions: Enabled.  -> [Run-time] WARNING: Hardware or OS does not support {}! Execution will {}!", name, name, error_action);
    }
}

// The core checking logic
void run_vectorization_checks() {
	std::println();

    // ----------------- SSE Check -----------------
#if defined(__SSE__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1) || defined(_M_X64)
    report_status("SSE", true, check_cpu_sse(), "trigger SIGILL");
#else
    report_status("SSE", false);
#endif

    // ----------------- AVX Check -----------------
#if defined(__AVX__)
    report_status("AVX", true, check_cpu_avx() && check_os_avx_support());
#else
    report_status("AVX", false);
#endif

    // ----------------- AVX2 Check -----------------
#if defined(__AVX2__)
    report_status("AVX2", true, check_cpu_avx2() && check_os_avx_support());
#else
    report_status("AVX2", false);
#endif

    // ----------------- AVX-512 Check -----------------
#if defined(__AVX512F__)
    report_status("AVX-512 (Foundation)", true, check_cpu_avx512() && check_os_avx512_support());
#else
    report_status("AVX-512 (Foundation)", false);
#endif

    std::println();
}

// Global variable initialization trick to execute checks before main()
[[maybe_unused]] static bool g_vectorization_check_init = []() {
    run_vectorization_checks();
    return true;
}();