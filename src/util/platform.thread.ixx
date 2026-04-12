module;

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

export module mo_yanxi.platform.thread;

import std;

namespace mo_yanxi::platform{
export
enum class thread_priority {
	low,
	normal,
	high,
	realtime
};

export
struct thread_attributes {
	std::string name;
	thread_priority priority = thread_priority::normal;

};

export
void set_thread_attributes(void* handle, const thread_attributes& attrs) {


#ifdef _WIN32
	// --- Windows 平台实现 ---

	// 1. 设置线程名称 (需要 Windows 10 版本 1607 或更高)
	if (!attrs.name.empty()) {
		std::wstring w_name(attrs.name.begin(), attrs.name.end());
		HRESULT hr = SetThreadDescription(handle, w_name.c_str());
		if (FAILED(hr)) {
			std::cerr << "Failed to set thread name on Windows.\n";
		}
	}

	// 2. 设置线程优先级
	int win_priority = THREAD_PRIORITY_NORMAL;
	switch (attrs.priority) {
	case thread_priority::low:      win_priority = THREAD_PRIORITY_BELOW_NORMAL; break;
	case thread_priority::normal:   win_priority = THREAD_PRIORITY_NORMAL; break;
	case thread_priority::high:     win_priority = THREAD_PRIORITY_ABOVE_NORMAL; break;
	case thread_priority::realtime: win_priority = THREAD_PRIORITY_TIME_CRITICAL; break;
	}
	SetThreadPriority(handle, win_priority);

#elif defined(__linux__) || defined(__APPLE__)
	// --- POSIX (Linux / macOS) 平台实现 ---

	// 1. 设置线程名称
	if (!attrs.name.empty()) {
#if defined(__APPLE__)
		std::cerr << "Warning: Setting thread name from outside is not supported on macOS.\n";
#else
		// Linux 限制线程名称长度最大为 16 字节（包含终止符 '\0'）
		std::string short_name = attrs.name.substr(0, 15);
		int rc = pthread_setname_np(handle, short_name.c_str());
		if (rc != 0) {
			std::cerr << "Failed to set thread name on Linux. Error code: " << rc << "\n";
		}
#endif
	}

	// 2. 设置线程优先级
	int policy = SCHED_OTHER;
	sched_param sch;
	pthread_getschedparam(handle, &policy, &sch);

	int min_prio = sched_get_priority_min(policy);
	int max_prio = sched_get_priority_max(policy);

	switch (attrs.priority) {
	case thread_priority::low:
		sch.sched_priority = min_prio;
		break;
	case thread_priority::normal:
		sch.sched_priority = (min_prio + max_prio) / 2;
		break;
	case thread_priority::high:
		sch.sched_priority = max_prio;
		break;
	case thread_priority::realtime:
		policy = SCHED_FIFO;
		sch.sched_priority = sched_get_priority_max(SCHED_FIFO);
		break;
	}

	int rc = pthread_setschedparam(handle, policy, &sch);
	if (rc != 0) {
		std::cerr << "Failed to set thread priority on POSIX system. Note: Realtime requires elevated privileges.\n";
	}
#endif
}

export
void set_thread_attributes(std::thread& thread, const thread_attributes& attrs){
	set_thread_attributes(thread.native_handle(), attrs);
}

export
void set_thread_attributes(std::jthread& thread, const thread_attributes& attrs){
	set_thread_attributes(thread.native_handle(), attrs);
}
}
// 使用示例
/*
void worker_function() {
    while(true) {
        // do work
    }
}

int main() {
    std::thread worker_thread(worker_function);

    thread_attributes attrs;
    attrs.name = "audio_processor";
    attrs.priority = thread_priority::high;
    attrs.cpu_affinity_mask = 0x01;

    set_thread_attributes(worker_thread, attrs);

    worker_thread.join();
    return 0;
}
*/