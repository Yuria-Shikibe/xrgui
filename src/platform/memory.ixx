module;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

export module mo_yanxi.platform.memory;

import std;

namespace mo_yanxi::platform {

export [[nodiscard]] void* reserve_commit_memory(const std::size_t size) noexcept {
	if(size == 0) {
		return nullptr;
	}

#ifdef _WIN32
	return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return ptr == MAP_FAILED ? nullptr : ptr;
#endif
}

export void release_memory(void* ptr, const std::size_t size) noexcept {
	if(ptr == nullptr) {
		return;
	}

#ifdef _WIN32
	(void)size;
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	munmap(ptr, size);
#endif
}

}
