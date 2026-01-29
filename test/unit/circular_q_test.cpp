#include "circular_queue.h"
#include <cassert>
#include <iostream>

int main() {
    CircularQueue<int> q(3);

    assert(q.Empty());

    q.PushBack(1);
    q.PushBack(2);

    assert(!q.Empty());
    assert(q.Size() == 2);
    assert(q.Front() == 1);

    q.PopFront();
    assert(q.Front() == 2);

    q.PushBack(3);
    q.PushBack(4); // 覆盖 2

    assert(q.Front() == 3);
    assert(q.OverrunCounter() == 1);

    std::cout << "All CircularQueue tests passed.\n";
}
