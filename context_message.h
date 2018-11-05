#pragma once
#include <string>

class IContext
{
public:
	virtual ~IContext() {}
};

template<typename MessageType = std::string>
class ContextMessage
{
public:
	ContextMessage() {}
	ContextMessage(std::shared_ptr<MessageType> msg)
		: m_msg(msg)
	{}
	ContextMessage(std::shared_ptr<MessageType> msg, std::shared_ptr<IContext> ctx)
		: m_msg(msg)
		, m_context(ctx)
	{}
	void setContext(std::shared_ptr<IContext> ctx)
	{
		m_context = ctx;
	}
	void setMessage(std::shared_ptr<MessageType> msg)
	{
		m_msg = msg;
	}
	std::shared_ptr<IContext> context() const
	{
		return m_context;
	}
	std::shared_ptr<MessageType> message() const
	{
		return m_msg;
	}
private:
	std::shared_ptr<IContext> m_context;
	std::shared_ptr<MessageType> m_msg;
};
