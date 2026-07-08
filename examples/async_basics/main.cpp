#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

#include <llc/async/async.h>

using namespace std::chrono_literals;

namespace {

llc::Task<int> add(int a, int b) {
    co_return a + b;
}

llc::Task<int> slow_add(int a, int b, llc::EventLoop &loop) {
    co_await llc::sleep(1ms, loop);
    co_return a + b;
}

llc::Task<int> compute(llc::EventLoop &loop) {
    auto x = co_await add(1, 2);
    auto [y, z] = co_await llc::when_all(slow_add(x, 10, loop), slow_add(20, 30, loop));
    co_return y + z;
}

} // namespace

int main() {
    llc::EventLoop loop;
    auto work = compute(loop);
    loop.schedule(work);
    loop.run();

    const auto result = work.result();
    assert(result == 63);
    std::cout << "async result = " << result << '\n';
    return 0;
}
