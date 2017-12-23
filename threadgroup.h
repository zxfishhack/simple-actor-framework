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
	//使用的函数原型为void Foo(CThreadGroup::InitDone done, ...);，调用done()以表示初始化成功，抛出异常表示初始化失败。
	template<typename Func, typename ...Arg>
	bool Attach(const char* name, Func func, Arg... args)
	{
		try
		{
			++m_initNeed;
			auto ctx = new Thread;
			ctx->name = name;
			ctx->thr = new std::thread(&ThreadGroup::Runner, this, ctx, std::bind(func, std::forward<Arg>(args)..., m_done));
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
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

	void Runner(Thread* ctx, std::function<void()> func)
	{
		//m_log.info("TG[%s], T[%s] begined.", m_name.c_str(), ctx->name.c_str());
		try
		{
			func();
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
