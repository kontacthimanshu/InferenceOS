#include <inferenceos/scheduler.h>

void wait_queue_initialize(struct ios_wait_queue *queue)
{
    IOS_ASSERT(queue != NULL);
    queue->head = NULL;
    queue->tail = NULL;
}

void wait_queue_remove(struct ios_wait_queue *queue, struct ios_scheduler_task *task)
{
    struct ios_scheduler_task *previous = NULL;
    struct ios_scheduler_task *current;

    if (queue == NULL || task == NULL) {
        return;
    }
    current = queue->head;
    while (current != NULL) {
        if (current == task) {
            if (previous == NULL) {
                queue->head = current->wait_next;
            } else {
                previous->wait_next = current->wait_next;
            }
            if (queue->tail == current) {
                queue->tail = previous;
            }
            current->wait_next = NULL;
            current->wait_queue = NULL;
            return;
        }
        previous = current;
        current = current->wait_next;
    }
}

struct ios_scheduler_task *wait_queue_wake_one(struct ios_wait_queue *queue)
{
    struct ios_scheduler_task *task;

    if (queue == NULL || queue->head == NULL) {
        return NULL;
    }
    task = queue->head;
    queue->head = task->wait_next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    task->wait_next = NULL;
    task->wait_queue = NULL;
    if (IOS_FAILED(scheduler_wake_task(task))) {
        return NULL;
    }
    return task;
}

ios_size wait_queue_wake_all(struct ios_wait_queue *queue)
{
    ios_size count = 0;
    while (wait_queue_wake_one(queue) != NULL) {
        ++count;
    }
    return count;
}
