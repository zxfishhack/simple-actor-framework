#pragma once
#include <deque>
#include "spin_lock.h"
#include "design_pattern.h"
#include <memory>

namespace detail {

	template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
	class Message : public noncopyable {
	public:
		typedef ActorIdType actorIdType;
		Message() : msg(nullptr) {}
		Message(const ActorIdType& src_, const MessageIdType& id_, MessageType* msg_)
			: src(src_), id(id_), msg(msg_)
		{}
		Message(Message<ActorIdType, MessageIdType>&& rhs) noexcept 
		: src(rhs.src), id(rhs.id), msg(rhs.msg) {}
		ActorIdType src;
		MessageIdType id;
		std::unique_ptr<MessageType> msg;
	};

}

template<typename MessageQueueType, typename MutexType = std::mutex>
class threadsafe_queue;

template<typename MessageType = std::unique_ptr<detail::Message<>>, typename ActorIdType = typename MessageType::element_type::actorIdType, typename MutexType = std::mutex>
class message_queue : public noncopyable {
public:
	message_queue(const ActorIdType& id, size_t overhead) : m_closed(false), m_overhead(overhead), m_id(id) {}
	~message_queue() {
		while (!acquire()) {}
		std::lock_guard<MutexType> lck(m_mutex);
	}
	bool push(MessageType msg) noexcept {
		if (m_closed) {
			return false;
		}
		std::lock_guard<MutexType> lck(m_mutex);
		try {
			m_msgs.push_back(std::move(msg));
		}
		catch (...) {
			return false;
		}
		return true;
	}
	bool pop(MessageType& msg) noexcept {
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
		std::lock_guard<MutexType> lck(m_mutex);
		return m_msgs.empty();
	}
	void close() {
		m_closed = true;
	}
private:
	std::atomic_flag m_acquired;
	std::atomic_bool m_closed;
	MutexType m_mutex;
	std::deque<MessageType> m_msgs;
	size_t m_overhead;
	ActorIdType m_id;
};

template<typename MessageQueueType, typename MutexType>
class threadsafe_queue : public noncopyable {
public:
	typedef MutexType mutexType;
	threadsafe_queue() {}
	void push(MessageQueueType queue) {
		std::lock_guard<MutexType> lck(m_mutex);
		m_mqs.push_back(queue);
	}
	bool pop(MessageQueueType& queue) {
		std::lock_guard<MutexType> lck(m_mutex);
		if (m_mqs.empty()) {
			return false;
		}
		queue = m_mqs.front();
		m_mqs.pop_front();
		return true;
	}
private:
	MutexType m_mutex;
	std::deque<MessageQueueType> m_mqs;
};

