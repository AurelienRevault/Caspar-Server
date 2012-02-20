#pragma once

#include <functional>
#include <memory>

#include <boost/thread/future.hpp>
#include <boost/thread/thread.hpp>

#include <boost/utility/declval.hpp>

#include "../enum_class.h"

namespace caspar {

struct launch_policy_def
{
	enum type
	{
		async,
		deferred
	};
};
typedef enum_class<launch_policy_def> launch;

namespace detail {

template<typename R>
struct invoke_function
{	
	template<typename F>
	void operator()(boost::detail::future_object<R>& p, F& f)
	{
        p.mark_finished_with_result_internal(f());
	}
};

template<>
struct invoke_function<void>
{	
	template<typename F>
	void operator()(boost::detail::future_object<void>& p, F& f)
	{
		f();
        p.mark_finished_with_result_internal();
	}
};

}
	
template<typename F>
auto async(launch lp, F&& f) -> boost::unique_future<decltype(f())>
{		
	typedef decltype(f()) result_type;
	
	if(lp == launch::deferred)
	{			
		// HACK: THIS IS A MAYOR HACK!

        typedef boost::shared_ptr<boost::detail::future_object<result_type>> future_ptr;
		
		auto fake_future		= boost::make_shared<future_ptr::value_type>();
		auto fake_future_raw	= fake_future.get();

		int dummy;
		fake_future->set_wait_callback(std::function<void(int)>([f, fake_future_raw](int) mutable
		{			
            boost::lock_guard<boost::mutex> lock(fake_future_raw->mutex);
            detail::invoke_function<result_type>()(*fake_future_raw, f);
		}), &dummy);
		
		boost::unique_future<result_type> future;
		reinterpret_cast<future_ptr&>(future) = std::move(fake_future); // Get around the "private" encapsulation.
		return std::move(future);
	}
	else
	{
		typedef boost::packaged_task<decltype(f())> task_t;

		auto task	= task_t(std::forward<F>(f));	
		auto future = task.get_future();
		
		boost::thread(std::move(task)).detach();
	
		return std::move(future);
	}
}
	
template<typename F>
auto async(F&& f) -> boost::unique_future<decltype(f())>
{	
	return async(launch::async, std::forward<F>(f));
}

template<typename T>
auto make_shared(boost::unique_future<T>&& f) -> boost::shared_future<T>
{	
	return boost::shared_future<T>(std::move(f));
}

template<typename T>
auto flatten(boost::unique_future<T>&& f) -> boost::unique_future<decltype(f.get().get())>
{
	auto shared_f = make_shared(std::move(f));
	return async(launch::deferred, [=]() mutable
	{
		return shared_f.get().get();
	});
}

}