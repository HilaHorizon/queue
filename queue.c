#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

/*
#define TCOUNT 10
#define COUNT_LIMIT 12

int count = 0;
const long thread_ids[] = {0, 1, 2};
const int NUM_THREADS = sizeof thread_ids / sizeof(long);

mtx_t count_mutex;
cnd_t count_threshold_cv;


//----------------------------------------------------------------------------
int inc_count(void *t) {
  long my_id = (long)t;

  for (int i = 0; i < TCOUNT; i++) {
    mtx_lock(&count_mutex);
    count++;

    // Check the value of count and signal waiting thread when condition is
    // reached.  Note that this occurs while mutex is locked.
    if (count == COUNT_LIMIT) {
      cnd_signal(&count_threshold_cv);
      printf("inc_count(): thread %ld, count = %d  Threshold reached.\n", my_id,
             count);
    }

    printf("inc_count(): thread %ld, count = %d, unlocking mutex\n", my_id,
           count);
    mtx_unlock(&count_mutex);

    // Do some "work" so threads can alternate on mutex lock
    thrd_sleep(&(struct timespec){.tv_sec = 1}, NULL); // sleep 1 sec
  }
  thrd_exit(0);
}

//----------------------------------------------------------------------------
int watch_count(void *t) {
  long my_id = (long)t;

  printf("Starting watch_count(): thread %ld\n", my_id);

  mtx_lock(&count_mutex);
  while (count < COUNT_LIMIT) {
    printf("watch_count(): thread %ld Meditating on condition variable.\n",
           my_id);
    cnd_wait(&count_threshold_cv, &count_mutex);
    printf("watch_count(): thread %ld Condition signal received.\n", my_id);

    count += 125;

    printf("watch_count(): thread %ld count now = %d.\n", my_id, count);
  }
  mtx_unlock(&count_mutex);
  thrd_exit(0);
}

//----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
  thrd_t threads[NUM_THREADS];

  // Initialize mutex and condition variable objects
  mtx_init(&count_mutex, mtx_plain);
  cnd_init(&count_threshold_cv);

  thrd_create(&threads[0], watch_count, (void *)thread_ids[0]);
  for (int i = 1; i < NUM_THREADS; ++i) {
    thrd_create(&threads[i], inc_count, (void *)thread_ids[i]);
  }

  // Wait for all threads to complete
  for (int i = 0; i < NUM_THREADS; i++) {
    thrd_join(threads[i], NULL);
  }

  printf("Main(): Waited on %d  threads. Done.\n", NUM_THREADS);

  // Clean up and exit
  mtx_destroy(&count_mutex);
  cnd_destroy(&count_threshold_cv);
  thrd_exit(0);
}
//============================== END OF FILE =================================
*/

// Queue node structure for linked list implementation
typedef struct queue_node {
    void* data;
    struct queue_node* next;
} queue_node_t;

// Thread waiting structure to maintain FIFO order of sleeping threads
typedef struct waiting_thread {
    cnd_t condition;
    struct waiting_thread* next;
    int ready; // Flag
} waiting_thread_t;

// Global queue structure
struct {
    queue_node_t* head;        // Points to dummy head node
    queue_node_t* tail;        // Points to last node
    mtx_t queue_mutex;         // Protects queue operations
    waiting_thread_t* waiting_head; // Head of waiting threads list
    waiting_thread_t* waiting_tail; // Tail of waiting threads list
    atomic_size_t visited_count;    // Thread-safe counter for visited()
} global_queue;

void initQueue(void) {
    // Initialize the queue with a dummy head node - for simple insert
    global_queue.head = malloc(sizeof(queue_node_t));
    global_queue.head->data = NULL;
    global_queue.head->next = NULL;
    global_queue.tail = global_queue.head;
    
    // Initialize the mutex for protecting queue operations
    mtx_init(&global_queue.queue_mutex, mtx_plain);
    
    // Initialize waiting threads list (empty initially)
    global_queue.waiting_head = NULL;
    global_queue.waiting_tail = NULL;
    
    // Initialize atomic counter for visited items
    atomic_store(&global_queue.visited_count, 0);
}

void enqueue(void* item) {
    mtx_lock(&global_queue.queue_mutex);
    
    // Create new node for the item
    queue_node_t* new_node = malloc(sizeof(queue_node_t));
    new_node->data = item;
    new_node->next = NULL;
    
    // Add to tail of queue
    global_queue.tail->next = new_node;
    global_queue.tail = global_queue.tail->next;
    
    // Wake up the oldest waiting thread if any exist
    if (global_queue.waiting_head != NULL) {
        waiting_thread_t* thread_to_wake = global_queue.waiting_head;
        global_queue.waiting_head = thread_to_wake->next;
        
        // If this was the last waiting thread, update tail pointer
        if (global_queue.waiting_head == NULL) {
            global_queue.waiting_tail = NULL;
        }
        
        // Signal the thread to wake up
        thread_to_wake->ready = 1;
        cnd_signal(&thread_to_wake->condition);
    }
    
    mtx_unlock(&global_queue.queue_mutex);
}

void* dequeue(void) {
    mtx_lock(&global_queue.queue_mutex);
    
    // Check if queue has items (head is dummy node)
    while (global_queue.head->next == NULL) {
        // Queue is empty, thread must wait
        waiting_thread_t* new_waiting_thread= malloc(sizeof(waiting_thread_t));
        cnd_init(&new_waiting_thread->condition);
        new_waiting_thread->next=NULL;
        new_waiting_thread->ready=0;

        // Add to tail of waiting threads list (FIFO order)
        if (global_queue.waiting_tail == NULL) {
            global_queue.waiting_tail = my_wait;
            global_queue.waiting_head = global_queue.waiting_tail;
        } else {
            global_queue.waiting_tail->next = new_waiting_thread;
            global_queue.waiting_tail = global_queue.waiting_tail->next;
        }         
        
        // wait until enqueue happens and we can dequeue
        while(!new_waiting_thread->ready){
          cnd_wait(&new_waiting_thread->condition, &global_queue.queue_mutex);
        }

        cnd_destroy(&new_waiting_thread->conditio);
        free(new_waiting_thread);
        
        // After waking up, check again for items (loop will exit if item available)
    }
    
    // At this point, queue definitely has an item
    queue_node_t* node_to_remove = global_queue.head->next;
    void* data = node_to_remove->data;
    
    // Update head pointer
    global_queue.head->next = node_to_remove->next;
    if (global_queue.tail == node_to_remove) {
        global_queue.tail = global_queue.head;
    }
    
    free(node_to_remove);
    
    // Increment visited counter atomically
    atomic_fetch_add(&global_queue.visited_count, 1);
    
    mtx_unlock(&global_queue.queue_mutex);
    return data;
}



void destroyQueue(void){

  queue_node_t curr = global_queue.head;
  queue_node_t next;

  while (curr) {
        next = curr->next;
        free(curr);        // free queue item
        curr = next;
  }

  waiting_thread_t curr = waiting_head;
  waiting_thread_t next;

  while (curr) {
        next = curr->next;
        cnd_destroy(&curr->condition);
        free(curr);        // free threads
        curr = next;
  }

  // Destroy the mutex
  mtx_destroy(&global_queue.queue_mutex);

  // Reset all pointers to NULL for safety
  global_queue.head = NULL;
  global_queue.tail = NULL;
  global_queue.waiting_head = NULL;
  global_queue.waiting_tail = NULL;

}








size_t visited(void) {
    // Return the atomic counter value
    return atomic_load(&global_queue.visited_count);
}