#include <cassert>
#include <iostream>

#include <llc/async/async.h>

namespace {

llc::Task<int> value_task(int value) {
    co_return value;
}

llc::Task<int, llc::Error> delayed_error(llc::Error error, llc::EventLoop &loop) {
    co_await llc::sleep(1, loop);
    co_await llc::fail(error);
    co_return 0;
}

llc::Task<int, llc::Error> delayed_value(int value, int delay_ms, llc::EventLoop &loop) {
    co_await llc::sleep(delay_ms, loop);
    co_return value;
}

llc::Task<> check_when_all_values() {
    auto [a, b, c] = co_await llc::when_all(value_task(1), value_task(2), value_task(3));
    assert(a + b + c == 6);
}

llc::Task<> check_when_all_error(llc::EventLoop &loop) {
    int slow_done = 0;

    auto slow = [&]() -> llc::Task<int, llc::Error> {
        co_await llc::sleep(50, loop);
        slow_done += 1;
        co_return 42;
    };

    auto result = co_await llc::when_all(
        delayed_error(llc::Error::k_connection_refused, loop), slow());
    assert(result.has_error());
    assert(result.error() == llc::Error::k_connection_refused);
    assert(slow_done == 0);
}

llc::Task<> check_with_token_pre_cancel() {
    llc::CancellationSource source;
    source.cancel();

    int started = 0;
    auto worker = [&]() -> llc::Task<int> {
        started += 1;
        co_return 1;
    };

    auto result = co_await llc::with_token(worker(), source.token());
    assert(result.is_cancelled());
    assert(started == 0);
}

llc::Task<> check_task_group_cancel(llc::EventLoop &loop) {
    int finished = 0;

    auto slow = [&]() -> llc::Task<> {
        co_await llc::sleep(50, loop);
        finished += 1;
    };

    llc::TaskGroup<> group(loop);
    assert(group.spawn(slow()));
    assert(group.spawn(slow()));
    group.cancel();
    co_await group.join();

    assert(finished == 0);
}

llc::Task<> run_checks(llc::EventLoop &loop) {
    co_await check_when_all_values();
    co_await check_when_all_error(loop);
    co_await check_with_token_pre_cancel();
    co_await check_task_group_cancel(loop);
}

} // namespace

int main() {
    llc::EventLoop loop;
    auto checks = run_checks(loop);
    loop.schedule(checks);
    loop.run();

    checks.result();
    std::cout << "async runtime checks passed\n";
    return 0;
}
