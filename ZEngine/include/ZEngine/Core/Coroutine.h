#pragma once
#include <future>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#define ZENGINE_COROUTINE_NAMESPACE std
#elif __has_include(<experimental/coroutine>)
#include <experimental/coroutine>
#define ZENGINE_COROUTINE_NAMESPACE std::experimental
#else
#error Compiler support for coroutines missing!
#endif


#if defined(__cpp_impl_coroutine) || !defined(_MSC_VER)
namespace ZENGINE_COROUTINE_NAMESPACE {

    template <typename T, typename... Args>
    struct coroutine_traits<future<T>, Args...> {
        struct promise_type {
            promise<T> m_promise;

            auto get_return_object() {
                return m_promise.get_future();
            }

            suspend_never initial_suspend() const noexcept {
                return {};
            }

            suspend_never final_suspend() const noexcept {
                return {};
            }

            template <typename U>
            void return_value(U&& u) {
                m_promise.set_value(std::forward<U>(u));
            }

            void set_exception(std::exception_ptr e) {
                m_promise.set_exception(std::move(e));
            }

            void unhandled_exception()
            {
                m_promise.set_exception(std::current_exception());
            }
        };
    };

    template <typename... Args>
    struct coroutine_traits<future<void>, Args...> {
        struct promise_type {
            promise<void> m_promise;

            auto get_return_object() {
                return m_promise.get_future();
            }

            suspend_never initial_suspend() const noexcept {
                return {};
            }

            suspend_never final_suspend() const noexcept {
                return {};
            }

            void return_value() {
                m_promise.set_value();
            }

            void set_exception(std::exception_ptr e) {
                m_promise.set_exception(std::move(e));
            }

            void unhandled_exception()
            {
                m_promise.set_exception(std::current_exception());
            }
        };
    };

    template <typename T>
    struct Awaiter {
        std::future<T>& m_internal_future;

        bool await_ready() const {
            return future_status::ready == m_internal_future.wait_for(chrono::milliseconds::zero());
        }

        void await_suspend(coroutine_handle<> callback) {

            thread worker_thread([this, callback]() mutable {
                m_internal_future.wait();
                callback();
            });

            worker_thread.detach();
        }

        decltype(auto) await_resume() {
            return m_internal_future.get();
        }
    };

} // namespace ZENGINE_COROUTINE_NAMESPACE

template <typename T>
auto operator co_await(std::future<T>&& f) {
    return ZENGINE_COROUTINE_NAMESPACE::Awaiter<T>{f};
}

template <typename T>
auto operator co_await(std::future<T>& f) {
    return ZENGINE_COROUTINE_NAMESPACE::Awaiter<T>{f};
}
#endif
