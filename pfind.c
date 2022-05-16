#include <stdatomic.h>
#include <threads.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

/* global variables */
queue *paths;
thrd_t *threads;
atomic_int num_waiting;
atomic_int num_found;
mtx_t search_mutex;


/* FIFO queue */

/* Struct
------------------------------------------------------*/
typedef struct node {
	struct node *next;
	struct node *prev;
	void *value;
} node;

typedef struct queue {
	struct node *first;
	struct node *last;
} queue;
/*-----------------------------------------------------*/

/* Functions
-------------------------------------------------------*/
/* Checks if a given queue is empty */
int is_empty(queue *q) {
	return (q->first == NULL && q->last == NULL);
}

/* Adds an element to a given queue */
void enqueue(queue *q, void *value) {
	node *new_node = (node *)malloc(sizeof(node));
	// assert malloc
	
	new_node->value = value;
	new_node->next = q->last;
	new_node->prev = NULL;
	
	q->last = new_node;
}

/* Returns the first element of a given queue */
node *dequeue(queue *q) {
	node *to_deq = q->first;
	void *ret = to_deq->value;
	
	q->first = to_deq->prev;
	
	return ret;
}
/*-----------------------------------------------------*/
	

int search(void *search_term) {
	dirnet *entry;
	char *file_name, *path, *new_path;
	struct stat buf;
	
	mtx_lock(&search_mutex);
	
	path = dequeue(paths);
	
	while ((entry = readdir(path)) != NULL) {
		file_name = entry->d_name;
		
		strcpy(new_path, path);
		strcat(new_path, file_name);
		
		lstat(new_path, &buf);
		if (S_ISDIR(buf.st_mode)) {
			enqueue(paths, new_path);
		}
		
		// compile and than continue to check file_name
	
	
	mtx_unlock(&search_mutex);
}

int main(int argc, char *argv[]) {
	// assert argc == 4
	char *root_path = argv[1];
	char *search_term = argv[2];
	int num_threads = atoi(argv[3]);
	// assert opendir(root_path) != NULL
	
	/* init paths queue */
	paths = (queue *)malloc(sizeof(queue));
	// assert malloc
	
	paths->first = NULL;
	paths->last = NULL;
	
	enqueue(paths, root_path);
	
	/* init threads */
	threads = (thrd_t *)malloc(num_threads * sizeof(thrd_t));
	// assert malloc
	for (int i = 0; i < num_threads; ++i) {
		thrd_create(&threads[i], search, search_term);
	}
	
	/* wait for all threads to complete */
	for (int i = 0; i < num_threads; i++) {
    		thrd_join(threads[i], NULL);
	}
	
	/* init mtx */
	mtx_init(search_mutex);
	
	/* init CV */
	
	/* if everything went fine */
	printf("Done searching, found %d files\n", num_found);
	exit(0);
	
}











