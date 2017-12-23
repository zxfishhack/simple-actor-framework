#pragma once
#include <string>
#include "design_pattern.h"
#include "threadgroup.h"
#include "shared_mutex.h"
#include <map>
#include "mq.h"

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class Actor;

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class ActorManager
{
	typedef message_queue<std::unique_ptr<detail::Message<ActorIdType, MessageIdType, MessageType>>> message_queue_type;
	typedef threadsafe_queue<std::shared_ptr<message_queue_type>> mq_queue_type;
public:
	static const int DEFAULT_THREAD_NUM = 4;
	~ActorManager() {}
	bool sendMessage(const ActorIdType& sourceName, const ActorIdType& targetName, const MessageIdType& messageName, MessageType* msg);
	bool registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType>* actor, size_t messageQueueOverhead = 1024) {
		return registerActor(name, actor, true, messageQueueOverhead);
	}
	bool registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType>& actor, size_t messageQueueOverhead = 1024) {
		return registerActor(name, &actor, false, messageQueueOverhead);
	}
	void releaseActor(const ActorIdType& name);
	bool start(int threadNum = DEFAULT_THREAD_NUM);
	void stop();
private:
	bool registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType>* actor, bool own, size_t messageQueueOverhead = 1024);
	bool newMq(const ActorIdType& name, size_t messageQueueOverhead = 1024) {
		message_queue_type* q = nullptr;
		try {
			q = new message_queue_type(name, messageQueueOverhead);
			std::unique_lock<shared_mutex> lck(m_mqMutex);
			m_mqs.insert(std::make_pair(name, q));
		} catch(...) {
			if (q) {
				delete q;
			}
			return false;
		}
		return true;
	}
	void freeMq(const ActorIdType& name) {
		std::unique_lock<shared_mutex> lck(m_mqMutex);
		auto it = m_mqs.find(name);
		if (it != m_mqs.end()) {
			it->second->close();
			m_mqs.erase(it);
		}
	}
	class ActorHolder : public noncopyable
	{
	public:
		ActorHolder(Actor<ActorIdType, MessageIdType>* _actor = nullptr, bool _owned = false) : actor(_actor), owned(_owned) {}
		ActorHolder(ActorHolder&& rhs) noexcept : actor(rhs.actor), owned(rhs.owned)
		{
			rhs.actor = nullptr;
		}
		~ActorHolder();
		Actor<ActorIdType, MessageIdType>* actor;
		bool owned;
	};
	ActorManager() : m_bExitFlag(false), m_actorThreads("ActorManager") {}
	void ActorThread(ThreadGroup::InitDone done);
	friend class Singleton<ActorManager>;
	volatile bool m_bExitFlag;
	ThreadGroup m_actorThreads;
	shared_mutex m_actorMutex;
	std::map<ActorIdType, ActorHolder> m_actors;
	shared_mutex m_mqMutex;
	std::map<ActorIdType, std::shared_ptr<message_queue_type>> m_mqs;
	mq_queue_type m_mqq;
};

template<typename ActorIdType, typename MessageIdType, typename MessageType>
class Actor
{
public:
	Actor()
	{}
	virtual ~Actor() {}
	bool sendMessage(const ActorIdType& targetName, const MessageIdType& messageName, MessageType* msg) const
	{
		return Singleton<ActorManager<ActorIdType, MessageIdType, MessageType>>::inst().sendMessage(m_name, targetName, messageName, msg);
	}
	virtual void onMessage(const ActorIdType& sourceName, const MessageIdType& messageName, const MessageType& msg) = 0;
protected:
	const std::string& name() const {
		return m_name;
	}
private:
	friend class ActorManager<ActorIdType, MessageIdType>;
	std::string m_name;
};

template<typename ActorIdType, typename MessageIdType, typename MessageType>
bool ActorManager<ActorIdType, MessageIdType, MessageType>::sendMessage(const ActorIdType& sourceName, const ActorIdType& targetName, const MessageIdType& messageName, MessageType* msg) {
	shared_lock<shared_mutex> lck(m_mqMutex);
	auto it = m_mqs.find(targetName);
	if (it == m_mqs.end())
	{
		return false;
	}
	auto ret = it->second->push(std::unique_ptr<detail::Message<ActorIdType, MessageIdType, MessageType>>(new detail::Message<ActorIdType, MessageIdType, MessageType>(sourceName, messageName, msg)));
	if (it->second->acquire()) {
		m_mqq.push(it->second);
	}
	return ret;
}

template<typename ActorIdType, typename MessageIdType, typename MessageType>
bool ActorManager<ActorIdType, MessageIdType, MessageType>::registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType>* actor, bool own, size_t messageQueueOverhead = 1024) {
	if (!actor) {
		return false;
	}
	if (!newMq(name, messageQueueOverhead)) {
		return false;
	}
	try {
		actor->m_name = name;
		std::unique_lock<shared_mutex> lck(m_actorMutex);
		m_actors.insert(std::make_pair(actor->m_name, ActorHolder(actor, own)));
	} catch(...) {
		freeMq(name);
		return false;
	}
	return true;
}

template<typename ActorIdType, typename MessageIdType, typename MessageType>
void ActorManager<ActorIdType, MessageIdType, MessageType>::releaseActor(const ActorIdType& name) {
	{
		std::unique_lock<shared_mutex> lck(m_actorMutex);
		m_actors.erase(name);
	}
	freeMq(name);
}

template<typename ActorIdType, typename MessageIdType, typename MessageType>
bool ActorManager<ActorIdType, MessageIdType, MessageType>::start(int threadNum) {
	char threadName[128];
	m_bExitFlag = false;
	for(auto i=0; i<threadNum; i++) {
		snprintf(threadName, sizeof(threadName), "ActorThread#%04d", i);
		m_actorThreads.Attach(threadName, &ActorManager<ActorIdType, MessageIdType>::ActorThread, this);
	}
	return m_actorThreads.WaitInitDone();
}

template<typename ActorIdType, typename MessageIdType, typename MessageType>
void ActorManager<ActorIdType, MessageIdType, MessageType>::stop() {
	{
		std::unique_lock<shared_mutex> lck(m_actorMutex);
		m_actors.clear();
	}
	m_bExitFlag = true;
	m_actorThreads.Join();
}

template<typename ActorIdType, typename MessageIdType, typename MessageType>
ActorManager<ActorIdType, MessageIdType, MessageType>::ActorHolder::~ActorHolder() {
	if (owned && actor) {
		delete actor;
	}
}

template<typename ActorIdType, typename MessageIdType, typename MessageType>
void ActorManager<ActorIdType, MessageIdType, MessageType>::ActorThread(ThreadGroup::InitDone done) {
	done();
	while(!m_bExitFlag) {
		auto maxIterator = 20;
		std::shared_ptr<message_queue_type> q;
		if (!m_mqq.pop(q)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		std::unique_ptr<detail::Message<ActorIdType, MessageIdType>> msg;
		auto && id = q->alias();
		{
			shared_lock<shared_mutex> lck(m_actorMutex);
			auto it = m_actors.find(id);
			if (it == m_actors.end()) {
				q->release();
				continue;
			}
			auto& holder = it->second;
			//停止的时候 是否等待队列中消息处理完毕？
			while (q->pop(msg) && !m_bExitFlag && maxIterator -- > 0) {
				holder.actor->onMessage(msg->src, msg->id, *(msg->msg));
			}
			if (!q->empty()) {
				m_mqq.push(q);
			} else {
				q->release();
			}
		}
	}
}
