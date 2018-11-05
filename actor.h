#pragma once
#include "design_pattern.h"
#include <memory>
#include <map>
#include <mutex>
#include "shared_mutex.h"
#include "mq.h"
#include <sstream>
#include <thread>
#include <threadgroup.h>

#ifdef LOG4CPP_CATEGORY_NAME
#include <log4cpp/Category.hh>
#endif

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class ActorManager;

template<typename ActorIdType = std::string>
class ActorImplBase
{
public:
	virtual ~ActorImplBase() {}
	virtual bool WaitInitDone() = 0;
	virtual bool InThreadPool() {
		return false;
	}
	virtual void Poll() {}
	virtual const ActorIdType& id() const = 0;
};

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class ActorImpl;

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class ActorImplNoThread;

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
	//implement one of below
	//onEnter will never failed.
	virtual void onEnter() {}
	//onEnter will failed.
	virtual bool onEnterMayFailed() {
		onEnter();
		return true;
	}
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

template<typename ActorIdType = std::string, typename MessageIdType = std::string, typename MessageType = std::string>
class ActorNoThread
{
public:
	virtual ~ActorNoThread() {}
	SEND_MESSAGE_RESULT sendMessage(const ActorIdType& targetName, const MessageIdType& messageName, MessageType* msg) const
	{
		if (!m_impl) {
			delete msg;
			return E_SMR_NOTREGISTER;
		}
		return m_impl->sendMessage(targetName, messageName, msg);
	}
	//implement one of below
	//onEnter will never failed.
	virtual void onEnter() {}
	//onEnter will failed.
	virtual bool onEnterMayFailed() {
		onEnter();
		return true;
	}
	virtual void onExit() {}
	virtual void onMessage(const ActorIdType& sourceName, const MessageIdType& messageName, const MessageType& msg) = 0;
	const ActorIdType& id() const {
		return m_id;
	}
	ActorManager<ActorIdType, MessageIdType, MessageType>* manager();
private:
	friend class ActorImplNoThread<ActorIdType, MessageIdType, MessageType>;
	ActorImplNoThread<ActorIdType, MessageIdType, MessageType>* m_impl;
	ActorIdType m_id;
};

template<typename ActorIdType, typename MessageIdType, typename MessageType>
class ActorImpl : public ActorImplBase<ActorIdType> {
public:
	typedef typename detail::Message<ActorIdType, MessageIdType, MessageType> messageType;
	ActorImpl(const ActorIdType& id, ActorManager<ActorIdType, MessageIdType, MessageType>& mgr, Actor<ActorIdType, MessageIdType, MessageType>* actor, bool own, size_t messageQueueOverhead)
		: m_own(own), m_exitFlag(false), m_initDone(false), m_initSucc(false), m_actor(actor), m_mgr(mgr), m_messageQueue(messageQueueOverhead) {
		actor->m_impl = this;
		actor->m_id = id;
	}
	virtual ~ActorImpl() {
		m_exitFlag = true;
		m_messageQueue.close();
		if (m_thread.joinable()) {
			m_thread.join();
		}
		std::unique_ptr<messageType> msg;
		while (m_messageQueue.try_pop(msg)) {
			m_actor->onMessage(msg->src, msg->id, *(msg->msg));
		}
#ifdef LOG4CPP_CATEGORY_NAME
		std::ostringstream ss;
		ss << "actor:" << m_actor->m_id;
		std::string name = ss.str();
		log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onExit enter.", name.c_str());
#endif
		m_actor->onExit();
#ifdef LOG4CPP_CATEGORY_NAME
		log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onExit exit.", name.c_str());
#endif
		if (m_own) {
			delete m_actor;
		}
	}
	bool WaitInitDone() override {
		m_thread = std::thread([this]()
		{
#if defined(_GNU_SOURCE) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 12)
			{
				char name_buf[16];
				std::ostringstream ss;
				ss << "actor:" << m_actor->m_id;
				std::string name = ss.str();
				snprintf(name_buf, sizeof name_buf, name.c_str());
				name_buf[sizeof name_buf - 1] = '\0';
				pthread_setname_np(pthread_self(), name_buf);
			}
#endif
#endif
			std::unique_ptr<messageType> msg;
#ifdef LOG4CPP_CATEGORY_NAME
			std::ostringstream ss;
			ss << "actor:" << m_actor->m_id;
			std::string name = ss.str();
			log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onEnter enter.", name.c_str());
#endif
			m_initSucc = m_actor->onEnterMayFailed();
			m_initDone = true;
#ifdef LOG4CPP_CATEGORY_NAME
			log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onEnter exit [%s].", name.c_str(), m_initSucc ? "true" : "false");
#endif
			if (!m_initSucc)
			{
				return;
			}
			while(!m_exitFlag) {
				if (!m_messageQueue.pop(msg)) {
					continue;
				}
				m_actor->onMessage(msg->src, msg->id, *(msg->msg));
				msg.reset();
			}
		});
		while(!m_initDone) {
			std::this_thread::yield();
		}
		return m_initSucc;
	}
	const ActorIdType& id() const override {
		return m_actor->id();
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
	volatile bool m_own;
	volatile bool m_exitFlag;
	volatile bool m_initDone;
	volatile bool m_initSucc;
	Actor<ActorIdType, MessageIdType, MessageType>* m_actor;
	ActorManager<ActorIdType, MessageIdType, MessageType>& m_mgr;
	message_queue<std::unique_ptr<messageType>> m_messageQueue;
	std::thread m_thread;
};

template<typename ActorIdType, typename MessageIdType, typename MessageType>
class ActorImplNoThread : public ActorImplBase<ActorIdType> {
public:
	typedef typename detail::Message<ActorIdType, MessageIdType, MessageType> messageType;
	ActorImplNoThread(const ActorIdType& id, ActorManager<ActorIdType, MessageIdType, MessageType>& mgr, Actor<ActorIdType, MessageIdType, MessageType>* actor, bool own, size_t messageQueueOverhead)
		: m_own(own), m_exitFlag(false), m_initDone(false), m_initSucc(false), m_actor(actor), m_mgr(mgr), m_messageQueue(messageQueueOverhead) {
		actor->m_impl = this;
		actor->m_id = id;
	}
	virtual ~ActorImplNoThread() {
		m_exitFlag = true;
		m_messageQueue.close();
		std::unique_ptr<messageType> msg;
		while (m_messageQueue.try_pop(msg)) {
			m_actor->onMessage(msg->src, msg->id, *(msg->msg));
	}
#ifdef LOG4CPP_CATEGORY_NAME
		std::ostringstream ss;
		ss << "actor:" << m_actor->m_id;
		std::string name = ss.str();
		log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onExit enter.", name.c_str());
#endif
		m_actor->onExit();
#ifdef LOG4CPP_CATEGORY_NAME
		log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onExit exit.", name.c_str());
#endif
		if (m_own) {
			delete m_actor;
		}
}
	bool WaitInitDone() override {
#ifdef LOG4CPP_CATEGORY_NAME
		std::ostringstream ss;
		ss << "actor:" << m_actor->m_id;
		std::string name = ss.str();
		log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onEnter enter.", name.c_str());
#endif
		m_initSucc = m_actor->onEnterMayFailed();
		m_initDone = true;
#ifdef LOG4CPP_CATEGORY_NAME
		log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("Actor[%s] onEnter exit [%s].", name.c_str(), m_initSucc ? "true" : "false");
#endif
		return m_initSucc;
	}
	void Poll() override {
		std::unique_ptr<messageType> msg;
		while(!m_exitFlag) {
			if (!m_messageQueue.try_pop(msg)) {
				break;
			}
			hasProcess = true;
			m_actor->onMessage(msg->src, msg->id, *(msg->msg));
		}
	}
	bool InThreadPool() override {
		return true;
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
	const ActorIdType& id() const override {
		return m_actor->id();
	}
private:
	volatile bool m_own;
	volatile bool m_exitFlag;
	volatile bool m_initDone;
	volatile bool m_initSucc;
	Actor<ActorIdType, MessageIdType, MessageType>* m_actor;
	ActorManager<ActorIdType, MessageIdType, MessageType>& m_mgr;
	message_queue<std::unique_ptr<messageType>> m_messageQueue;
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
	typedef ActorImplBase<ActorIdType> ActorHolder;
	typedef detail::Message<ActorIdType, MessageIdType, MessageType> messageType;
public:
	ActorManager(unsigned int threadPoolSize = 1) : m_threadGroup("am"), m_exitFlag(false) {
		if (threadPoolSize == 0) {
			threadPoolSize = std::thread::hardware_concurrency();
		}
		for(unsigned int i=0; i<threadPoolSize; i++) {
			char thrName[256] = { 0 };
			snprintf(thrName, sizeof(thrName), "pool-%03d", i + 1);
			m_threadGroup.Attach(thrName, std::bind(&pollRoutine, this));
		}
		m_threadGroup.WaitInitDone();
	}
	~ActorManager() {
		m_actorQueue.close();
		m_exitFlag = true;
		m_threadGroup.Join();
		std::map<ActorIdType, std::shared_ptr<ActorHolder>> actors;
		{
			std::lock_guard<shared_mutex> lck(m_actorMutex);
			actors.swap(m_actors);
		}
		//process pending message
		std::shared_ptr<ActorHolder> actor;
		while(m_actorQueue.try_pop(actor)) {
			actor->Poll();
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
		std::shared_ptr<ActorHolder> holder(new ActorImpl<ActorIdType, MessageIdType, MessageType>(name, *this, actor, true, messageQueueOverhead));
		return registerActor(name, holder);
	}
	bool registerActor(const ActorIdType& name, Actor<ActorIdType, MessageIdType, MessageType>& actor, size_t messageQueueOverhead = 1024) {
		std::shared_ptr<ActorHolder> holder(new ActorImpl<ActorIdType, MessageIdType, MessageType>(name, *this, &actor, false, messageQueueOverhead));
		return registerActor(name, holder);
	}
	bool registerActor(const ActorIdType& name, ActorNoThread<ActorIdType, MessageIdType, MessageType>* actor, size_t messageQueueOverhead = 1024) {
		std::shared_ptr<ActorHolder> holder(new ActorImplNoThread<><ActorIdType, MessageIdType, MessageType>(name, *this, actor, true, messageQueueOverhead));
		return registerActor(name, holder);
	}
	bool registerActor(const ActorIdType& name, ActorNoThread<ActorIdType, MessageIdType, MessageType>& actor, size_t messageQueueOverhead = 1024) {
		std::shared_ptr<ActorHolder> holder(new ActorImplNoThread<ActorIdType, MessageIdType, MessageType>(name, *this, &actor, false, messageQueueOverhead));
		return registerActor(name, holder);
	}
	void releaseActor(const ActorIdType& name) {
		std::shared_ptr<ActorHolder> holder;
		{
			std::lock_guard<shared_mutex> lck(m_actorMutex);
			auto it = m_actors.find(name);
			if (it != m_actors.end()) {
				holder = it->second;
				m_actors.erase(it);
			} else {
#ifdef LOG4CPP_CATEGORY_NAME
				std::ostringstream ss;
				ss << "actor:" << name;
				std::string name2 = ss.str();
				log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("release Actor[%s] notFound.", name2.c_str());
#endif
			}
		}
	}
	bool hasActor(const ActorIdType& name) {
		shared_lock<shared_mutex> lck(m_actorMutex);
		return m_actors.find(name) != m_actors.end();
	}
	void pollRoutine(ThreadGroup::InitDone done) {
		done();
		while(!m_exitFlag) {
			std::shared_ptr<ActorHolder> actor;
			if (!m_actorQueue.pop(actor)) {
				continue;
			}
			actor->Poll();
			{
				std::lock_guard<shared_mutex> lck(m_actorMutex);
				if (m_actors.find(actor->id()) != m_actors.end()) {
					m_actorQueue.push(actor);
				}
			}
		}
	}
private:
	bool registerActor(const ActorIdType& name, std::shared_ptr<ActorHolder> actor) {
		std::shared_ptr<void> defer(nullptr, [name](void*)
		{
#ifdef LOG4CPP_CATEGORY_NAME
			std::ostringstream ss;
			ss << "actor:" << name;
			std::string name2 = ss.str();
			log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("register Actor[%s] exit.", name2.c_str());
#endif
		});
		try {
			{
				std::lock_guard<shared_mutex> lck(m_actorMutex);
				m_actors.insert(std::make_pair(name, actor));
				if (actor->InThreadPool()) {
					m_actorQueue.push(actor);
				}
			}
			if (!holder->WaitInitDone())
			{
#ifdef LOG4CPP_CATEGORY_NAME
				std::ostringstream ss;
				ss << "actor:" << name;
				std::string name2 = ss.str();
				log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("register Actor[%s] WaitInitDoneFailed.", name2.c_str());
#endif
				std::lock_guard<shared_mutex> lck(m_actorMutex);
				m_actors.erase(name);
				return false;
			}
		}
		catch(const std::exception& e) {
#ifdef LOG4CPP_CATEGORY_NAME
			std::ostringstream ss;
			ss << "actor:" << name;
			std::string name2 = ss.str();
			log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("register Actor[%s] got exception[%s].", name2.c_str(), e.what());
#endif
			std::lock_guard<shared_mutex> lck(m_actorMutex);
			m_actors.erase(name);
			return false;
		}
		catch (...) {
#ifdef LOG4CPP_CATEGORY_NAME
			std::ostringstream ss;
			ss << "actor:" << name;
			std::string name2 = ss.str();
			log4cpp::Category::getInstance(LOG4CPP_CATEGORY_NAME).notice("register Actor[%s] got exception[unknown].", name2.c_str());
#endif
			std::lock_guard<shared_mutex> lck(m_actorMutex);
			m_actors.erase(name);
			return false;
		}
		return true;
	}
	shared_mutex m_actorMutex;
	std::map<ActorIdType, std::shared_ptr<ActorHolder>> m_actors;
	message_queue<std::shared_ptr<ActorHolder>> m_actorQueue;
	ThreadGroup m_threadGroup;
	std::atomic<bool> m_exitFlag;
};
