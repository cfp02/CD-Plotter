#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include "gcode_parser.h"
#include "config.h"

class CommandQueue {
private:
    Command queue[QUEUE_SIZE];
    int head;
    int tail;
    int count;

public:
    CommandQueue();
    bool enqueue(const Command& cmd);
    bool dequeue(Command& cmd);
    bool isEmpty() const;
    bool isFull() const;
    int getCount() const;
    void clear();
};

#endif

