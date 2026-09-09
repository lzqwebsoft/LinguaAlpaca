#pragma once
#pragma execution_character_set("utf-8")

#include <atomic>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <wx/app.h>

namespace LinguaAlpaca::UI {

namespace detail {
#if (defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
using std::apply;
#else
template <typename F, typename Tuple, size_t... I>
constexpr decltype(auto) apply_impl(F&& f, Tuple&& t, std::index_sequence<I...>) {
    return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}

template <typename F, typename Tuple>
constexpr decltype(auto) apply(F&& f, Tuple&& t) {
    return apply_impl(
        std::forward<F>(f),
        std::forward<Tuple>(t),
        std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{});
}
#endif
} // namespace detail

/**
 * @brief 线程安全异步 UI 生命周期混入基类
 * 
 * 适用于任何接收后台工作线程异步回调并需要更新 UI 的组件。
 * 派生类只需继承此类，并使用 BindUi 包装回调函数，即可享受：
 * 1. 自动 RAII 析构生命周期标记重置（无需手动管理 token）
 * 2. 自动参数值拷贝跨线程转发到 wxTheApp->CallAfter
 * 3. 自动存活校验与空指针拦截
 */
class AsyncTrackable {
public:
    AsyncTrackable()
        : m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {}

    virtual ~AsyncTrackable() {
        if (m_aliveToken) {
            *m_aliveToken = false;
        }
    }

    /// 获取底层存活状态 Token
    std::shared_ptr<std::atomic<bool>> GetAliveToken() const {
        return m_aliveToken;
    }

    /**
     * @brief 将普通闭包包装为线程安全的 UI 调度闭包
     * @param fn 在主 UI 线程执行的回调函数
     * @return 可安全跨线程传递并调用的包装仿函数
     */
    template <typename Func>
    auto BindUi(Func&& fn) {
        auto alive = m_aliveToken;
        return [alive, fn = std::forward<Func>(fn)](auto&&... args) {
            if (!*alive) return;
            if (wxTheApp) {
                auto tupleArgs = std::make_tuple(std::forward<decltype(args)>(args)...);
                wxTheApp->CallAfter([alive, fn, tupleArgs = std::move(tupleArgs)]() mutable {
                    if (!*alive) return;
                    detail::apply(fn, std::move(tupleArgs));
                });
            }
        };
    }

private:
    std::shared_ptr<std::atomic<bool>> m_aliveToken;
};

} // namespace LinguaAlpaca::UI
