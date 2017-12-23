#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#endif

class shared_mutex
{
public:
	shared_mutex();
	~shared_mutex();
	//为兼容C++ Concepts，以下接口采用标准库命名方式
	void lock();
	bool try_lock();
	void unlock();
	void lock_shared();
	bool try_lock_shared();
	void unlock_shared();
private:
	shared_mutex(const shared_mutex&);
	void operator=(const shared_mutex&);

#ifdef _WIN32
	SRWLOCK m_lock;
#else
	pthread_rwlock_t m_lock;
#endif
};

template<class SharedMutex>
class shared_lock
{
public:
	shared_lock(SharedMutex& mtx) : _mtx(mtx)
	{
		_mtx.lock_shared();
	}
	~shared_lock()
	{
		_mtx.unlock_shared();
	}
private:
	SharedMutex& _mtx;
	void operator=(const shared_lock& rhs);
	shared_lock(const shared_lock& rhs);
};

#ifdef _WIN32
inline shared_mutex::shared_mutex()
{
	InitializeSRWLock(&m_lock);
}

inline shared_mutex::~shared_mutex()
{
	//无需清理
}

inline void shared_mutex::lock()
{
	AcquireSRWLockExclusive(&m_lock);
}

inline bool shared_mutex::try_lock()
{
	return TryAcquireSRWLockExclusive(&m_lock) == TRUE;
}


inline void shared_mutex::unlock()
{
	ReleaseSRWLockExclusive(&m_lock);
}

inline void shared_mutex::lock_shared()
{
	AcquireSRWLockShared(&m_lock);
}

inline bool shared_mutex::try_lock_shared()
{
	return TryAcquireSRWLockShared(&m_lock) == TRUE;
}

inline void shared_mutex::unlock_shared()
{
	ReleaseSRWLockShared(&m_lock);
}

#else

inline shared_mutex::shared_mutex()
{
	pthread_rwlock_init(&m_lock, NULL);
}

inline shared_mutex::~shared_mutex()
{
	pthread_rwlock_destroy(&m_lock);
}

inline void shared_mutex::lock()
{
	pthread_rwlock_wrlock(&m_lock);
}

inline bool shared_mutex::try_lock()
{
	return pthread_rwlock_trywrlock(&m_lock) == 0;
}


inline void shared_mutex::unlock()
{
	pthread_rwlock_unlock(&m_lock);
}

inline void shared_mutex::lock_shared()
{
	pthread_rwlock_rdlock(&m_lock);
}

inline bool shared_mutex::try_lock_shared()
{
	return pthread_rwlock_tryrdlock(&m_lock);
}

inline void shared_mutex::unlock_shared()
{
	pthread_rwlock_unlock(&m_lock);
}

#endif
