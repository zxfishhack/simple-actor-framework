#pragma once
#include <deque>
#include <mutex>
#include "design_pattern.h"
#include <memory>
#include <string>
#include <condition_variable>
#include <atomic>

enum SEND_MESSAGE_RESULT
{
	E_SMR_OK,
	E_SMR_OVERHEAD,
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

template<typename MessageType = std::unique_ptr<detail::Message<>>>
class message_queue : public noncopyable {
public:
	message_queue(size_t overhead) : m_closed(false), m_overhead(overhead)
	{}
	~message_queue() {
	}
	SEND_MESSAGE_RESULT push(MessageType&& msg) {
		if (m_closed) {
			return E_SMR_CLOSED;
		}
		std::lock_guard<std::mutex> lck(m_mutex);
		try {
			m_msgs.push_back(std::move(msg));
		}
		catch (...) {
			return E_SMR_MEMORY;
		}
		m_cv.notify_one();
		if (m_overhead > 0 && m_msgs.size() > m_overhead)
		{
			return E_SMR_OVERHEAD;
		}
		return E_SMR_OK;
	}
	bool pop(MessageType& msg) {
		std::unique_lock<std::mutex> lck(m_mutex);
		while (m_msgs.empty() && !m_closed) {
			m_cv.wait(lck);
		}
		if (empty()) {
			return false;
		}
		msg = std::move(m_msgs.front());
		m_msgs.pop_front();
		return true;
	}
	bool try_pop(MessageType& msg) {
		std::unique_lock<std::mutex> lck(m_mutex);
		if (empty()) {
			return false;
		}
		msg = std::move(m_msgs.front());
		m_msgs.pop_front();
		return true;
	}
	bool overhead() {
		return m_overhead > 0 ? size() > m_overhead : false;
	}
	bool empty() const {
		return m_msgs.empty();
	}
	size_t size()
	{
		std::lock_guard<std::mutex> lck(m_mutex);
		return m_msgs.size();
	}
	void close() {
		std::lock_guard<std::mutex> lck(m_mutex);
		m_closed = true;
		m_cv.notify_all();
	}
private:
	std::atomic<bool> m_closed;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::deque<MessageType> m_msgs;
	size_t m_overhead;
};

