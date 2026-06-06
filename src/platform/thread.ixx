module;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

export module mo_yanxi.platform.thread;

import std;

namespace mo_yanxi::platform {

export enum class thread_priority {
	low,
	normal,
	high,
	realtime
};

export struct thread_attributes {
	std::string name;
	thread_priority priority = thread_priority::normal;
};

export struct thread_attribute_result {
	bool name_applied = true;
	bool priority_applied = true;
	std::int64_t native_name_error = 0;
	std::int64_t native_priority_error = 0;

	[[nodiscard]] constexpr bool success() const noexcept {
		return name_applied && priority_applied;
	}
};

namespace {
#ifdef _WIN32
[[nodiscard]] std::wstring utf8_to_wide(const std::string_view value) {
	if(value.empty()) {
		return {};
	}

	const int wide_size = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		value.data(),
		static_cast<int>(value.size()),
		nullptr,
		0);
	if(wide_size <= 0) {
		return std::wstring(value.begin(), value.end());
	}

	std::wstring result(static_cast<std::size_t>(wide_size), L'\0');
	MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		value.data(),
		static_cast<int>(value.size()),
		result.data(),
		wide_size);
	return result;
}
#endif
}

export thread_attribute_result set_thread_attributes(
	std::thread::native_handle_type handle,
	const thread_attributes& attrs) noexcept {
	thread_attribute_result result{};

#ifdef _WIN32
	if(!attrs.name.empty()) {
		const std::wstring wide_name = utf8_to_wide(attrs.name);
		const HRESULT hr = SetThreadDescription(handle, wide_name.c_str());
		if(FAILED(hr)) {
			result.name_applied = false;
			result.native_name_error = static_cast<std::int64_t>(hr);
		}
	}

	int native_priority = THREAD_PRIORITY_NORMAL;
	switch(attrs.priority) {
	case thread_priority::low:
		native_priority = THREAD_PRIORITY_BELOW_NORMAL;
		break;
	case thread_priority::normal:
		native_priority = THREAD_PRIORITY_NORMAL;
		break;
	case thread_priority::high:
		native_priority = THREAD_PRIORITY_ABOVE_NORMAL;
		break;
	case thread_priority::realtime:
		native_priority = THREAD_PRIORITY_TIME_CRITICAL;
		break;
	}

	if(SetThreadPriority(handle, native_priority) == 0) {
		result.priority_applied = false;
		result.native_priority_error = static_cast<std::int64_t>(GetLastError());
	}

#elif defined(__linux__) || defined(__APPLE__)
	if(!attrs.name.empty()) {
#if defined(__APPLE__)
		result.name_applied = false;
		result.native_name_error = -1;
#else
		const std::string short_name = attrs.name.substr(0, 15);
		const int rc = pthread_setname_np(handle, short_name.c_str());
		if(rc != 0) {
			result.name_applied = false;
			result.native_name_error = rc;
		}
#endif
	}

	int policy = SCHED_OTHER;
	sched_param params{};
	int rc = pthread_getschedparam(handle, &policy, &params);
	if(rc != 0) {
		result.priority_applied = false;
		result.native_priority_error = rc;
		return result;
	}

	int min_priority = sched_get_priority_min(policy);
	int max_priority = sched_get_priority_max(policy);
	if(min_priority == -1 || max_priority == -1) {
		result.priority_applied = false;
		result.native_priority_error = -1;
		return result;
	}

	switch(attrs.priority) {
	case thread_priority::low:
		params.sched_priority = min_priority;
		break;
	case thread_priority::normal:
		params.sched_priority = (min_priority + max_priority) / 2;
		break;
	case thread_priority::high:
		params.sched_priority = max_priority;
		break;
	case thread_priority::realtime:
		policy = SCHED_FIFO;
		params.sched_priority = sched_get_priority_max(SCHED_FIFO);
		if(params.sched_priority == -1) {
			result.priority_applied = false;
			result.native_priority_error = -1;
			return result;
		}
		break;
	}

	rc = pthread_setschedparam(handle, policy, &params);
	if(rc != 0) {
		result.priority_applied = false;
		result.native_priority_error = rc;
	}
#else
	(void)handle;
	(void)attrs;
#endif

	return result;
}

export thread_attribute_result set_thread_attributes(
	std::thread& thread,
	const thread_attributes& attrs) noexcept {
	return set_thread_attributes(thread.native_handle(), attrs);
}

export thread_attribute_result set_thread_attributes(
	std::jthread& thread,
	const thread_attributes& attrs) noexcept {
	return set_thread_attributes(thread.native_handle(), attrs);
}

}
