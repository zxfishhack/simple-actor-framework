#pragma once
#include <atomic>
#include "design_pattern.h"

class spin_lock : public noncopyable {
public:
	spin_lock() {}
	void lock() {
		while (flag.test_and_set(std::memory_order_acquire));
	}
	void unlock() {
		flag.clear(std::memory_order_release);
	}
private:
	std::atomic_flag flag;
};
