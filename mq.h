#pragma once
#include <deque>
#include "spin_lock.h"
#include "design_pattern.h"
#include <memory>
#include <condition_variable>

enum SEND_MESSAGE_RESULT
{
	E_SMR_OK,
	E_SMR_CLOSED,
	E_SMR_MEMORY,
	E_SMR_NOTFOUND,
	E_SMR_NOTREGISTER
};

namespace detail {

	template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
	class Message : public noncopyable {
	public:
		typedef ActorIdType actorIdType;
		Message() : msg(nullptr) {}
		Message(const ActorIdType& src_, const MessageIdType& id_, MessageType* msg_)
			: src(src_), id(id_), msg(msg_)
		{}
		Message(Message<ActorIdType, MessageIdType>&& rhs) 
		: src(rhs.src), id(rhs.id), msg(rhs.msg) {}
		ActorIdType src;
		MessageIdType id;
		std::unique_ptr<MessageType> msg;
	};
}

template<typename MessageQueueType>
class threadsafe_queue;

template<typename MessageType = std::unique_ptr<detail::Message<>>, typename ActorIdType = typename MessageType::element_type::actorIdType, typename MutexType = spin_lock>
class message_queue : public noncopyable {
public:
	message_queue(const ActorIdType& id, size_t overhead) : m_closed(false), m_overhead(overhead), m_id(id)
	{
		m_acquired.clear();
	}
	~message_queue() {
		while (!acquire()) {}
		std::lock_guard<MutexType> lck(m_mutex);
	}
	SEND_MESSAGE_RESULT push(MessageType msg) {
		if (m_closed) {
			return E_SMR_CLOSED;
		}
		std::lock_guard<MutexType> lck(m_mutex);
		try {
			m_msgs.push_back(std::move(msg));
		}
		catch (...) {
			return E_SMR_MEMORY;
		}
		return E_SMR_OK;
	}
	bool pop(MessageType& msg) {
		msg = nullptr;
		std::lock_guard<MutexType> lck(m_mutex);
		if (m_msgs.empty()) {
			return false;
		}
		msg = std::move(m_msgs.front());
		m_msgs.pop_front();
		return true;
	}
	bool overhead() {
		std::lock_guard<MutexType> lck(m_mutex);
		return m_overhead > 0 ? m_msgs.size() > m_overhead : false;
	}
	bool acquire() {
		return !m_acquired.test_and_set(std::memory_order_acquire);
	}
	void release() {
		m_acquired.clear(std::memory_order_release);
	}
	ActorIdType alias() const {
		return m_id;
	}
	bool empty() {
		return m_msgs.empty();
	}
	void close() {
		m_closed = true;
	}
	void lock() {
		m_mutex.lock();
	}
	void unlock() {
		m_mutex.unlock();
	}
private:
	std::atomic_flag m_acquired;
	std::atomic<bool> m_closed;
	MutexType m_mutex;
	std::deque<MessageType> m_msgs;
	size_t m_overhead;
	ActorIdType m_id;
};

template<typename MessageQueueType>
class threadsafe_queue : public noncopyable {
public:
	threadsafe_queue() : m_closed(false) {}
	bool push(MessageQueueType queue) {
		std::lock_guard<std::mutex> lck(m_mutex);
		if (m_closed)
		{
			return false;
		}
		try {
			m_mqs.push_back(queue);
		} catch(...) {
			return false;
		}
		m_cv.notify_one();
		return true;
	}
	bool pop(MessageQueueType& queue) {
		std::unique_lock<std::mutex> lck(m_mutex);
		while (m_mqs.empty() && !m_closed)
		{
			m_cv.wait(lck);
		}
		if (m_mqs.empty())
		{
			return false;
		}
		queue = m_mqs.front();
		m_mqs.pop_front();
		return true;
	}
	void close()
	{
		m_closed = true;
		m_cv.notify_all();
	}
	void clear()
	{
		m_closed = false;
		for(auto it = m_mqs.begin(); it != m_mqs.end(); ++it)
		{
			(*it)->release();
		}
		m_mqs.clear();
	}
private:
	std::atomic<bool> m_closed;
	std::condition_variable m_cv;
	std::mutex m_mutex;
	std::deque<MessageQueueType> m_mqs;
};

