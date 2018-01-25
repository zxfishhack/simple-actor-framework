#pragma once
#include "design_pattern.h"
#include <memory>
#include <map>
#include <mutex>
#include "shared_mutex.h"
#include "mq.h"
#include <thread>

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class ActorManager;

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class ActorImpl;

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class Actor
{
public:
	virtual ~Actor() {}
	SEND_MESSAGE_RESULT sendMessage(const ActorIdType& targetName, const MessageIdType& messageName, MessageType* msg) const
	{
		if (!m_impl) {
			delete msg;
			return E_SMR_NOTREGISTER;
		}
		return m_impl->sendMessage(targetName, messageName, msg);
	}
	virtual void onEnter() {}
	virtual void onExit() {}
	virtual void onMessage(const ActorIdType& sourceName, const MessageIdType& messageName, const MessageType& msg) = 0;
	const ActorIdType& id() const {
		return m_id;
	}
	ActorManager<ActorIdType, MessageIdType, MessageType>* manager();
private:
	friend class ActorImpl<ActorIdType, MessageIdType, MessageType>;
	ActorImpl<ActorIdType, MessageIdType, MessageType>* m_impl;
	ActorIdType m_id;
};

template<typename ActorIdType, typename MessageIdType, typename MessageType>
class ActorImpl {
public:
	typedef typename detail::Message<ActorIdType, MessageIdType, MessageType> messageType;
	ActorImpl(const ActorIdType& id, ActorManager<ActorIdType, MessageIdType, MessageType>& mgr, Actor<ActorIdType, MessageIdType, MessageType>* actor, bool own, size_t messageQueueOverhead)
		: m_own(own), m_exitFlag(false), m_actor(actor), m_mgr(mgr), m_messageQueue(messageQueueOverhead) {
		actor->m_impl = this;
		actor->m_id = id;
		m_thread = std::thread([this]()
		{
			std::unique_ptr<messageType> msg;
			m_actor->onEnter();
			while(!m_exitFlag) {
				if (!m_messageQueue.pop(msg)) {
					continue;
				}
				m_actor->onMessage(msg->src, msg->id, *(msg->msg));
			}
		});
	}
	~ActorImpl() {
		m_exitFlag = true;
		m_messageQueue.close();
		if (m_thread.joinable()) {
			m_thread.join();
		}
		std::unique_ptr<messageType> msg;
		while(m_messageQueue.try_pop(msg)) {
			m_actor->onMessage(msg->src, msg->id, *(msg->msg));
		}
		m_actor->onExit();
		if (m_own) {
			delete m_actor;
		}
	}
	SEND_MESSAGE_RESULT sendMessage(const ActorIdType& targetName, const MessageIdType& messageName, MessageType* msg) {
		if (targetName == m_actor->id())
		{
			return enqueue(std::unique_ptr<messageType>(new messageType(targetName, messageName, msg)));
		}
		else
		{
			return m_mgr.sendMessage(m_actor->id(), targetName, messageName, msg);
		}
	}
	SEND_MESSAGE_RESULT enqueue(std::unique_ptr<messageType> msg) {
		return m_messageQueue.push(std::move(msg));
	}
	ActorManager<ActorIdType, MessageIdType, MessageType>* manager()
	{
		return &m_mgr;
	}
private:
	bool m_own;
	bool m_exitFlag;
	Actor<ActorIdType, MessageIdType, MessageType>* m_actor;
	ActorManager<ActorIdType, MessageIdType, MessageType>& m_mgr;
	message_queue<std::unique_ptr<messageType>> m_messageQueue;
	std::thread m_thread;
};

template<typename ActorIdType, typename MessageIdType, typename MessageType>
ActorManager<ActorIdType, MessageIdType, MessageType>* Actor<ActorIdType, MessageIdType, MessageType>::manager()
{
	if (m_impl)
	{
		return m_impl->manager();
	}
	return nullptr;
}

template<typename ActorIdType, typename MessageIdType, typename MessageType>
class ActorManager
{
	typedef ActorImpl<ActorIdType, MessageIdType, MessageType> ActorHolder;
	typedef detail::Message<ActorIdType, MessageIdType, MessageType> messageType;
public:
	~ActorManager() {
		std::map<ActorIdType, std::shared_ptr<ActorHolder>> actors;
		{
			std::lock_guard<shared_mutex> lck(m_actorMutex);
			actors.swap(m_actors);
		}
	}
	SEND_MESSAGE_RESULT sendMessage(const ActorIdType& sourceName, const ActorIdType& targetName, const MessageIdType& messageName, MessageType* msg) {
		std::shared_ptr<ActorHolder> holder;
		{
			shared_lock<shared_mutex> lck(m_actorMutex);
			auto it = m_actors.find(targetName);
			if (it != m_actors.end()) {
				holder = it->second;
			}
		}
		if (!holder)
		{
			delete msg;
			return E_SMR_NOTFOUND;
		}
		return holder->enqueue(std::unique_ptr<messageType>(new messageType(sourceName, messageName, msg)));
	}
	bool registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType, MessageType>* actor, size_t messageQueueOverhead = 1024) {
		return registerActor(name, actor, true, messageQueueOverhead);
	}
	bool registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType, MessageType>& actor, size_t messageQueueOverhead = 1024) {
		return registerActor(name, &actor, false, messageQueueOverhead);
	}
	void releaseActor(const ActorIdType& name) {
		std::shared_ptr<ActorHolder> holder;
		{
			std::lock_guard<shared_mutex> lck(m_actorMutex);
			auto it = m_actors.find(name);
			if (it != m_actors.end()) {
				holder = it->second;
				m_actors.erase(it);
			}
		}
	}
private:
	bool registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType, MessageType>* actor, bool own, size_t messageQueueOverhead) {
		try {
			std::shared_ptr<ActorHolder> holder(new ActorHolder(name, *this, actor, own, messageQueueOverhead));
			std::lock_guard<shared_mutex> lck(m_actorMutex);
			m_actors.insert(std::make_pair(name, holder));
		}
		catch(...) {
			return false;
		}
		return true;
	}
	shared_mutex m_actorMutex;
	std::map<ActorIdType, std::shared_ptr<ActorHolder>> m_actors;
};
