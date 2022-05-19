#include <stdatomic.h>
#include <threads.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>



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
void *dequeue(queue *q) {
	node *to_deq;
	void *ret;
	printf("in dequeue\n");
	to_deq = q->first;
	printf("q->first successful\n");
	ret = to_deq->value;
	printf("to_deq->value successful\n");
	printf("ret = %s\n", (char *)ret);	
	q->first = to_deq->prev;
	
	return ret;
}
/*-----------------------------------------------------*/

/* global variables */
queue *paths;
thrd_t *threads;
atomic_int num_waiting;
atomic_int num_found;
mtx_t search_mutex;


int search(void *search_term) {
	struct dirent *entry;
	DIR *d;
	char *file_name, *path;
	char new_path[PATH_MAX];
	struct stat buf;
	char *term_string = (char *)search_term;
	printf("in search\n");	
	mtx_lock(&search_mutex);
	printf("after locking search mutex\n");
	path = (char *)dequeue(paths);
	mtx_unlock(&search_mutex);
	printf("dequeue was successful\n");
	d = opendir(path);
	if (d) {
		while ((entry = readdir(d)) != NULL) {
			/* extract file name */
			file_name = entry->d_name;
		
			/* put path to entry in new_path */
			strcpy(new_path, path);
			strcat(new_path, file_name);
		
			/* check if entry is a directory */
			lstat(new_path, &buf);
			if (S_ISDIR(buf.st_mode)) { /* entry is a directory, enqueue it to paths */
				mtx_lock(&search_mutex);
				enqueue(paths, new_path);
				mtx_unlock(&search_mutex);
			}
		
			/* entry is a file */
			/* check if entry contains search term */
			else if (strstr(file_name, term_string) != NULL) { /* entry contains search_term */
				printf("%s\n", new_path);
				num_found++;
			}
		}
	}
	
	return 0; /* everything went fine */
}

int main(int argc, char *argv[]) {
	char *root_path, *search_term;
	int num_threads;
	
	// assert argc == 4
	root_path = argv[1];
	search_term = argv[2];
	num_threads = atoi(argv[3]);
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
	mtx_init(&search_mutex, mtx_plain);
	
	/* init CV */
	
	/* if everything went fine */
	printf("Done searching, found %d files\n", num_found);
	exit(0);
	
}











