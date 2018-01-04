#pragma once
#if _MSC_VER >= 1900
#include <mutex>
#include <atomic>
#endif
#include <map>
#include <deque>
#include <functional>
#include <type_traits>

#define NESTED_LESS(l, r) \
if (l < r) return true; \
if (r < l) return false

class noncopyable {
public:
	noncopyable() {}
private:
	noncopyable(noncopyable&&) {}
	noncopyable(noncopyable&) {}
	void operator=(noncopyable&&) const {}
	void operator=(noncopyable&) const {}
};

template<typename T>
class Singleton : public noncopyable {
public:
#if _MSC_VER >= 1900
	template<typename ...Args>
	static void create(Args&&... args) {
		std::call_once(_create, [&] {
			_obj = new T(std::forward<Args>(args)...);
		});
	}
	static void destroy() {
		std::call_once(_delete, [&]{
			delete _obj;
			_obj = nullptr;
		});
	}
#else
	static void create() {
		_obj = new T();
	}
	static void destroy() {
		delete _obj;
		_obj = 0;
	}
#endif
	
	static T& inst() {
		return *_obj;
	}
private:
	static T* _obj;
#if _MSC_VER >= 1900
	static std::once_flag _create;
	static std::once_flag _delete;
#endif
};

template<typename T> T* Singleton<T>::_obj = nullptr;
#if _MSC_VER >= 1900
template<typename T> std::once_flag Singleton<T>::_create;
template<typename T> std::once_flag Singleton<T>::_delete;
#endif

#if _MSC_VER >= 1900

template<typename ListenerFunction, typename ListenerHandle = size_t>
class Listener : public noncopyable {
public:
	template<typename func>
	ListenerHandle operator+(func f) {
		auto key = atomic_fetch_add(&m_listenerKey, ListenerHandle(1));
		m_listeners[key] = f;
		return key;
	}
	template<typename func>
	void operator+=(func f) {
		auto key = atomic_fetch_add(&m_listenerKey, ListenerHandle(1));
		m_listeners[key] = f;
	}
	void operator-(ListenerHandle h) {
		m_listeners.erase(h);
	}
	template<typename ...Args>
	void notify(Args... args) {
		for (auto f : m_listeners) {
			f.second(std::forward<Args>(args)...);
		}
	}
private:
	std::atomic<ListenerHandle> m_listenerKey;
	std::map<ListenerHandle, std::function<ListenerFunction>> m_listeners;
};

template<typename ...Types>
struct Vistor;

template<typename T, typename ...Types>
struct Vistor<T, Types...> : Vistor<Types...> {
	using Vistor<Types...>::Visit;
	virtual void visit(const T&) const = 0;
	virtual ~Vistor() {}
};

template<typename T>
struct Vistor<T> {
	virtual void visit(const T&) = 0;
	virtual ~Vistor() {}
};

template<typename ExecuteFunction>
struct Command {
	template<class F, class ...BindArgs>
	void bind(F&& f, BindArgs&& ... args) {
		m_function = std::bind(f, std::forward<BindArgs&&>(args)...);
	}
	template<typename ...ExecuteArgs>
	typename std::function<ExecuteFunction>::result_type execute(ExecuteArgs&&... args) {
		return m_function(std::forward<ExecuteArgs>(args)...);
	}
private:
	std::function<ExecuteFunction>  m_function;
};

template <typename T>
struct has_reset {
private:
	template <typename U>
	static decltype(std::declval<U>().reset(), std::true_type()) test(int) {
		return std::true_type();
	}
	template <typename>
	static std::false_type test(...) {
		return std::false_type();
	}
public:
	typedef decltype(test<T>(0)) type;
};

template<typename T>
class range {
public:
	class iterator {
	public:
		iterator(const T& cur, const T& step) : _cur(cur), _step(step) {}
		T operator*() const {
			return _cur;
		}
		bool operator==(const iterator& rhs) const {
			return !(_cur != rhs);
		}
		bool operator!=(const iterator& rhs) const {
			if (_step == 0) {
				return _cur != rhs._cur;
			}
			return _step < 0 ? _cur > rhs._cur : _cur < rhs._cur;
		}
		iterator operator++() {
			_cur += _step;
			return *this;
		}
		iterator operator++(int) {
			auto ret = iterator(_cur, _step);
			_cur += _step;
			return ret;
		}
		iterator operator--() {
			_cur -= _step;
			return *this;
		}
		iterator operator--(int) {
			auto ret = iterator(_cur, _step);
			_cur -= _step;
			return ret;
		}
	private:
		T _cur;
		T _step;
	};
	range(const T& beg, const T& end, const T& step = T(1)) : _beg(beg), _end(end), _step(step) {}
	iterator begin() const {
		return iterator(_beg, _step);
	}
	iterator end() const {
		return iterator(_end, _step);
	}
private:
	const T _beg, _end, _step;
};



template<typename T, typename LockType = void, typename = typename std::enable_if<has_reset<T>::type::value>::type>
class ObjectPool : noncopyable {
public:
	template<typename ...Args>
	ObjectPool(size_t num, Args&&... args) {
		while(num--) {
			_objs.emplace_back(new T(std::forward <Args&&>(args)...));
		}
	}
	std::shared_ptr<T> get() {
		std::lock_guard<LockType> _(_lock);
		if (_objs.empty()) {
			throw(std::bad_alloc());
		}
		auto p = std::shared_ptr<T>(_objs.front(), [&](T* ptr)
		{
			_free(ptr);
		});
		_objs.pop_front();
		return p;
	}
	T* alloc() noexcept {
		std::lock_guard<LockType> _(_lock);
		if (_objs.empty()) {
			return nullptr;
		}
		auto p = _objs.front();
		_objs.pop_front();
		return p;
	}
	void free(T* ptr) noexcept {
		std::lock_guard<LockType> _(_lock);
		_objs.emplace_back(ptr);
	}
private:
	std::deque<T*> _objs;
	LockType _lock;
};

template<typename T>
class ObjectPool<T, void, typename std::enable_if<has_reset<T>::type::value>::type> : noncopyable {
public:
	template<typename ...Args>
	ObjectPool(size_t num, Args&&... args) {
		while(num--) {
			_objs.emplace_back(new T(std::forward<Args&&>(args)...));
		}
	}
	std::shared_ptr<T> get() {
		if (_objs.empty()) {
			throw(std::bad_alloc());
		}
		auto p = std::shared_ptr<T>(_objs.front(), [&](T* ptr)
		{
			_free(ptr);
		});
		_objs.pop_front();
		return p;
	}
	T* alloc() noexcept {
		if (_objs.empty()) {
			return nullptr;
		}
		auto p = _objs.front();
		_objs.pop_front();
		return p;
	}
	void free(T* ptr) noexcept {
		_objs.emplace_back(ptr);
	}
private:
	std::deque<T*> _objs;
};

#endif