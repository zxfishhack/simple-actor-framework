#pragma once
#ifdef _WIN32
#include <Windows.h>
#include <time.h>
#pragma comment(lib, "winmm")

class Stopwatch {
public:
	Stopwatch() {
		m_isHighResolution = QueryPerformanceFrequency(&m_frequency);
		m_elapsed = 0;
		m_running = false;
	}
	unsigned long long ElapsedMilliseconds() {
		return ElapsedTicks() * 1000 / m_frequency.QuadPart;
	}
	unsigned long long ElapsedTripMilliseconds() {
		return ElapsedTripTicks() * 1000 / m_frequency.QuadPart;
	}
	bool IsRunning() const {
		return m_running;
	}

	static time_t GetTimestamp() {
		return time(NULL);
	}

	void Reset() {
		m_elapsed = 0;
		QueryPerformanceCounter(&m_start);
		m_end = m_start;
	}

	void Restart() {
		Reset();
		m_running = true;
	}

	void Start() {
		m_running = true;
		QueryPerformanceCounter(&m_start);
	}
	void Stop() {
		if (m_running) {
			m_running = false;
			QueryPerformanceCounter(&m_end);
			m_elapsed += m_end.QuadPart - m_start.QuadPart;
		}
	}
private:
	LARGE_INTEGER m_frequency, m_start, m_end;
	unsigned long long m_elapsed;
	BOOL m_isHighResolution;
	bool m_running;

	unsigned long long ElapsedTicks() {
		if (m_running) {
			QueryPerformanceCounter(&m_end);
			return m_elapsed + m_end.QuadPart - m_start.QuadPart;
		}
		return m_elapsed;
	}
	unsigned long long ElapsedTripTicks() {
		if (m_running) {
			QueryPerformanceCounter(&m_end);
		}
		return m_end.QuadPart - m_start.QuadPart;
	}
};

#endif

#ifdef __GNUC__
#include <sys/time.h>
class Stopwatch {
public:
	Stopwatch() {
		m_elapsed = 0;
		m_running = false;
	}
	unsigned long long ElapsedMilliseconds() {
		if (m_running) {
			gettimeofday(&m_end, NULL);
			return m_elapsed + timevalDiffMilliseconds(m_end, m_start);
		}
		return m_elapsed;
	}
	unsigned long long ElapsedTripMilliseconds() {
		if (m_running) {
			gettimeofday(&m_end, NULL);
		}
		return timevalDiffMilliseconds(m_end, m_start);
	}
	bool IsRunning() const {
		return m_running;
	}

	static time_t GetTimestamp() {
		return time(NULL);
	}

	void Reset() {
		m_elapsed = 0;
		gettimeofday(&m_start, NULL);
		m_end = m_start;
	}

	void Restart() {
		Reset();
		m_running = true;
	}

	void Start() {
		m_running = true;
		gettimeofday(&m_start, NULL);
	}
	void Stop() {
		if (m_running) {
			m_running = false;
			gettimeofday(&m_end, NULL);
			m_elapsed += timevalDiffMilliseconds(m_end, m_start);
		}
	}
private:
	static unsigned long long timevalDiffMilliseconds(timeval& e, timeval& s) {
		return (e.tv_sec - s.tv_sec) * 1000 + (e.tv_usec - s.tv_usec) / 1000;
	}
	unsigned long long m_elapsed;
	timeval m_start, m_end;
	bool m_running;
};
#endif