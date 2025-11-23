#include "command_queue.h"

CommandQueue::CommandQueue() {
    head = 0;
    tail = 0;
    count = 0;
}

bool CommandQueue::enqueue(const Command& cmd) {
    if (isFull()) {
        return false;
    }
    
    queue[tail] = cmd;
    tail = (tail + 1) % QUEUE_SIZE;
    count++;
    return true;
}

bool CommandQueue::dequeue(Command& cmd) {
    if (isEmpty()) {
        return false;
    }
    
    cmd = queue[head];
    head = (head + 1) % QUEUE_SIZE;
    count--;
    return true;
}

bool CommandQueue::isEmpty() const {
    return count == 0;
}

bool CommandQueue::isFull() const {
    return count >= QUEUE_SIZE;
}

int CommandQueue::getCount() const {
    return count;
}

void CommandQueue::clear() {
    head = 0;
    tail = 0;
    count = 0;
}

