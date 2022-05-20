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
	return q->first == NULL;
}

/* Adds an element to a given queue */
void enqueue(queue *q, void *value) {
	/* create new node */
	node *new_node = (node *)malloc(sizeof(node));
	// assert malloc
	
	new_node->value = value;
	new_node->next = q->last;
	new_node->prev = NULL;

	/* update queue */
	if (is_empty(q)) {
		q->first = new_node;
	}
	else {
		(q->last)->prev = new_node;
	}
	q->last = new_node;
	
}

/* Returns the value of the first element of a given queue */
void *dequeue(queue *q) {
	node *to_deq;
	void *ret;

	if (is_empty(q)) {
		return NULL;
	}
	
	/* extract value from first element */
	to_deq = q->first;
	ret = to_deq->value;

	/* update queue */
	q->first = to_deq->prev;
	if (is_empty(q)) {
		q->last = NULL;
	}
	
	return ret;
}

/* Initializes empty queue */
queue *init_queue() {
	queue *q = (queue *)malloc(sizeof(queue));
	// assert malloc
	
	q->first = NULL;
	q->last = NULL;
	
	return q;
}
	
/*-----------------------------------------------------*/

/* global variables */
queue *paths_queue;
queue *conds_queue;

thrd_t *thrds_arr;

cnd_t *cv_arr;
cnd_t start_cv;

int num_thrds;

atomic_int num_thrds_waiting;
atomic_int num_thrds_alive;
atomic_int num_files_found;
atomic_int done;
atomic_int num_thrds_created;

mtx_t paths_mutex;
mtx_t conds_mutex;
mtx_t start_mutex;

char *search_term;

/* Wake next sleeping thread up.
   USE AFTER LOCKING conds_mutex !!! */
void wake_next() {
	long next_thrd = (long)dequeue(conds_queue);
	printf("waking %ld up\n", next_thrd);
	cnd_signal(&cv_arr[next_thrd]);
}

void exit_all_thrds(long id) {
	printf("thread %ld in exit_all_thrds\n", id);
	/* raise flag */
	done = 1;

	/* wake all threads up */
	mtx_lock(&conds_mutex);
	int i = 0;
	node *curr = conds_queue->first;
	while (curr != NULL) {
		printf("%ld\n", (long)(curr->value));
		curr = curr->next;
	}
	while (!is_empty(conds_queue)) {
		printf("iter %d\n", i);
		wake_next();
		i++;
	}
	mtx_unlock(&conds_mutex);
	
	/* exit */
	printf("thread %ld exiting\n", id);
	thrd_exit(0);
}

void wait_for_tasks(long thrd_id) {
	printf("thread %ld in wait_for_tasks\n", thrd_id);
	/* check if all threads are sleeping */
	if (num_thrds_alive - 1 == num_thrds_waiting) {
		mtx_unlock(&paths_mutex);
		exit_all_thrds(thrd_id);
	}
	
	num_thrds_waiting++;
	
	mtx_lock(&conds_mutex);
	enqueue(conds_queue, (void *)thrd_id);
	mtx_unlock(&conds_mutex);
	printf("thread %ld is waiting...\n", thrd_id);
	cnd_wait(&cv_arr[thrd_id], &paths_mutex);
	mtx_unlock(&paths_mutex);
	printf("thread %ld done waiting\n", thrd_id);
	/* thread woke up! */
	num_thrds_waiting--;
}


void exit_if_really_empty(long id) {
	if (done == 1) {
		printf("thread %ld exiting\n", id);
		thrd_exit(0);
	}
}

/* Returns path + "/" + file_name */
char *update_path(char *path, char *file_name) {
	char *new_path = (char *)malloc(PATH_MAX * sizeof(char));
	
	strcpy(new_path, path);
        strcat(new_path, "/");
        strcat(new_path, file_name);
        
        return new_path;
}


void search_path(char *path, long thrd_id) {
	struct dirent *entry;
	DIR *d;
	struct stat buf;
	char *file_name;
	char *new_path;
	
	d = opendir(path);
	/* assert d != NULL */
	
	while ((entry = readdir(d)) != NULL) {
		/* extract file name */
		file_name = entry->d_name;
			
		/* ignore "." and ".." */
		if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0){
            		continue;
        	}
        	
		/* put path to entry in new_path */
		new_path = update_path(path, file_name);
		
		/* check if entry is a directory */
		stat(new_path, &buf);
		if (S_ISDIR(buf.st_mode)) { /* entry is a directory, enqueue it to paths */
			/* add to paths_queue */
			mtx_lock(&paths_mutex);
			enqueue(paths_queue, new_path);
			mtx_unlock(&paths_mutex);

			/* wake up a thread */
			mtx_lock(&conds_mutex);
			while (!is_empty(conds_queue)) {
				wake_next();
			}
			mtx_unlock(&conds_mutex);
		}

		/* entry is a file */
		/* check if entry contains search term */
		else if (strstr(file_name, search_term) != NULL) { /* entry contains search_term */
			printf("%s\n", new_path);
			num_files_found++;
		}
	}
	
}

	
int thrd_func(void *thrd_id) {
	char *curr_path;
	printf("in thrd_func\n");
	long id = (long)thrd_id;
	printf("thread %ld is in thrd_func\n", id);
	
	/* wait until all threads are created before starting */ 
	mtx_lock(&start_mutex);
	num_thrds_created++;
	cnd_wait(&start_cv, &start_mutex);
	mtx_unlock(&start_mutex);
	printf("thread %ld woke up!\n", id);
	while (1) {
		mtx_lock(&paths_mutex);
		if (is_empty(paths_queue)) {
			wait_for_tasks(id);
		}
		else {
			mtx_unlock(&paths_mutex);
		}
		
		exit_if_really_empty(id);

		mtx_lock(&paths_mutex);
		curr_path = (char *)dequeue(paths_queue);
		mtx_unlock(&paths_mutex);
		
		search_path(curr_path, id);
	}
	return 0; /* everything went fine */
}

/* Initializes global variable thrds_arr */
void init_threads() {
	thrds_arr = (thrd_t *)malloc(num_thrds * sizeof(thrd_t));
	// assert malloc
	for (long i = 0; i < num_thrds; ++i) {
		printf("creating thread %ld\n", i);
		thrd_create(&thrds_arr[i], thrd_func, (void *)i);
	}
	
	/* wake all threads up once all threads created */
	while (1) {
		mtx_lock(&start_mutex);
		if (num_thrds_created == num_thrds) {
			printf("all created\n");
			cnd_broadcast(&start_cv);
			mtx_unlock(&start_mutex);
			break;
		}
		mtx_unlock(&start_mutex);
	}
}

/* Initializes global variable cv_arr */
void init_cv_arr() {
	cv_arr = (cnd_t *)malloc(num_thrds * sizeof(cnd_t));
	// assert malloc
	for (int i = 0; i < num_thrds; ++i) {
		cnd_init(&cv_arr[i]);
	}
}

/* Cleans up all CVs in cv_arr */
void destroy_cv_arr() {
	for (int i = 0; i < num_thrds; ++i) {
		cnd_destroy(&cv_arr[i]);
	}
	
	free(cv_arr); // remove free?
}


/* Initialize all global variables */
void init_global_vars(char *root_path) {
	/* init atomic vars */
	num_thrds_created = 0;
	num_thrds_waiting = 0;
	num_thrds_alive = num_thrds;
	num_files_found = 0;
	done = 0;
	
	/* init paths queue */
	paths_queue = init_queue();
	enqueue(paths_queue, root_path);
	
	/* init cond_queue */
	conds_queue = init_queue();
	
	/* init mtxs */
	mtx_init(&paths_mutex, mtx_plain);
	mtx_init(&conds_mutex, mtx_plain);
	mtx_init(&start_mutex, mtx_plain);
	
	/* init CVs */
	init_cv_arr();
	
	/* init threads */
	init_threads();
}


int main(int argc, char *argv[]) {
	char *root_path;
	
	/* extract arguments */
	// assert argc == 4
	root_path = argv[1];
	search_term = argv[2];
	num_thrds = atoi(argv[3]);
	// assert opendir(root_path) != NULL
	
	/* init global variables */
	init_global_vars(root_path);
	
	
	/* wait for all threads to complete */
	for (int i = 0; i < num_thrds; i++) {
    		thrd_join(thrds_arr[i], NULL);
    		printf("joined thread %d\n", i);
	}
	
	
	/* if everything went fine */
	/* clean up */
	mtx_destroy(&paths_mutex);
	mtx_destroy(&conds_mutex);
	mtx_destroy(&start_mutex);
	cnd_destroy(&start_cv);
	destroy_cv_arr();
	
	/* print summery and exit */
	printf("Done searching, found %d files\n", num_files_found);
	exit(0);
	
}











