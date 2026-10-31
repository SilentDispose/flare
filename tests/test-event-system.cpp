#include "test-framework.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <flare/event.h>

void RunEventTests(Test::Context& context) {
    context.Begin("event");
    Flare::Event<int> event;
    int total = 0;
    Flare::Subscription first = event.Subscribe([&total](int value) { total += value; });
    Flare::Subscription second = event.Subscribe([&total](int value) { total += value * 2; });
    context.Check(event.Size() == 2, "two subscribers");
    event.Emit(5);
    context.Check(total == 15, "both handlers ran");

    first.Unsubscribe();
    context.Check(!first.IsActive(), "unsubscribed handle is inactive");
    context.Check(second.IsActive(), "other handle remains active");
    event.Emit(2);
    context.Check(total == 19, "only the remaining handler ran");

    second.Unsubscribe();
    context.Check(event.Empty(), "event has no handlers");
    event.Emit(10);
    context.Check(total == 19, "no handler ran after clearing");

    Flare::Event<> signal;
    int count = 0;
    auto handle = signal.Subscribe([&count]() { ++count; });
    signal.Emit();
    signal.Emit();
    context.Check(count == 2, "no-argument event emitted twice");
    handle.Unsubscribe();

    Flare::Event<std::string> textEvent;
    std::string received;
    auto textHandle = textEvent.Subscribe([&received](const std::string& value) { received = value; });
    textEvent.Emit("hello");
    context.Check(received == "hello", "string payload delivered");
    textHandle.Unsubscribe();

    Flare::Event<int> threaded;
    std::atomic<int> threadCount{0};
    threaded.Subscribe([&threadCount](int) { threadCount.fetch_add(1); });
    std::vector<std::thread> threads;
    for (int index = 0; index < 4; ++index) {
        threads.emplace_back([&threaded]() {
            for (int iteration = 0; iteration < 100; ++iteration) {
                threaded.Emit(1);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    context.Check(threadCount.load() == 400, "concurrent emits invoke handlers");

    Flare::Event<int> reentrant;
    int reentrantCount = 0;
    reentrant.Subscribe([&](int value) {
        ++reentrantCount;
        if (value == 1) {
            reentrant.Emit(2);
        }
    });
    reentrant.Emit(1);
    context.Check(reentrantCount == 2, "reentrant emit is supported");
}
