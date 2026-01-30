#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "mpmc_blocking_q.h"

int main() {
    MpmcBlockingQueue<int> q(10);

    const int producers = 2;
    const int consumers = 2;
    const int items_per_producer = 1000;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::vector<std::thread> threads;

    // producers
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&q, &produced, items_per_producer, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                int x = p * 1000000 + i;
                q.Enqueue(std::move(x));
                ++produced;
            }
        });
    }

    // consumers
    for (int c = 0; c < consumers; ++c) {
        threads.emplace_back([&q, &consumed, producers, items_per_producer]() {
            const int total = producers * items_per_producer;
            while (true) {
                int x = 0;
                if (q.DequeueFor(x, std::chrono::milliseconds(100))) {
                    ++consumed;
                    if (consumed.load() >= total) break;
                } else {
                    if (consumed.load() >= total) break;
                }
            }
        });
    }

    for (auto &t : threads) t.join();

    std::cout << "produced = " << produced.load() << "\n";
    std::cout << "consumed = " << consumed.load() << "\n";
    std::cout << "queue_size = " << q.Size() << "\n";
    std::cout << "overrun_counter = " << q.OverrunCounter() << "\n";
    std::cout << "discard_counter = " << q.DiscardCounter() << "\n";

    return 0;
}
