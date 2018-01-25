#pragma once
#include <vector>
#include <thread>
#include <string>
#include <atomic>
#include <exception>

class ThreadGroup
{
public:
	typedef std::function<void()> InitDone;
	ThreadGroup(const char* name)
	:m_name(name), m_initError(false), m_initDone(0), m_initNeed(0)
	{
		m_done = std::bind([&]{
			++m_initDone;
		});
	}
	~ThreadGroup()
	{
		Join();
	}
	bool Attach(const char* name, std::function<void(InitDone)> func)
	{
		try
		{
			++m_initNeed;
			auto ctx = new Thread;
			ctx->name = name;
			ctx->thr = new std::thread(&ThreadGroup::Runner, this, ctx, func);
			m_Threads.push_back(ctx);
			return true;
		}
		catch(const std::exception&)
		{
			//m_log.notice("TG[%s] T[%s] begin failed, exception[%s].", m_name.c_str(), name, e.what());
			m_initError = true;
			return false;
		}
	}
	void Join()
	{
		for(auto it=m_Threads.begin(); it!=m_Threads.end();++it)
		{
			auto thr = (*it)->thr;
			if (thr->joinable()) {
				thr->join();
			}
			delete *it;
		}
		m_Threads.clear();
	}
	bool WaitInitDone() const
	{
		while(m_initDone < m_initNeed && !m_initError)
		{
			std::this_thread::yield();
		}
		return !m_initError;
	}
private:
	struct Thread
	{
		std::string name;
		std::thread *thr;
	};
	std::vector<Thread*> m_Threads;
	std::string m_name;
	volatile bool m_initError;
	std::atomic<int> m_initDone;
	std::atomic<int> m_initNeed;
	InitDone m_done;

	void Runner(Thread* ctx, std::function<void(InitDone)> func)
	{
		//m_log.info("TG[%s], T[%s] begined.", m_name.c_str(), ctx->name.c_str());
		try
		{
			func(m_done);
		}
		catch(const std::exception& )
		{
			//m_log.notice("TG[%s], T[%s] end with exception[%s].", m_name.c_str(), ctx->name.c_str(), e.what());
			m_initError = true;
			return;
		}
		catch(...)
		{
			m_initError = true;
			return;
		}
		//m_log.info("TG[%s], T[%s] end.", m_name.c_str(), ctx->name.c_str());
	}
};
