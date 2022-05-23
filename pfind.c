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
#include <errno.h>



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

/* Adds an element to a given queue
   Returns 0 on success and -1 on failiure */
int enqueue(queue *q, void *value) {
	/* create new node */
	node *new_node = (node *)malloc(sizeof(node));
	if (new_node == NULL) {
		return -1;
	}
	
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
	
	return 0;
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

/* Returns an empty queue or NULL if failed */
queue *init_queue() {
	queue *q = (queue *)malloc(sizeof(queue));
	if (q == NULL) {
		return NULL;
	}
	
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
atomic_int exit_code;

mtx_t paths_mutex;
mtx_t conds_mutex;
mtx_t start_mutex;

char *search_term;


void err_in_thrd() {
	exit_code = 1;
	num_thrds_alive--;
}

/* Wake next sleeping thread up.
   USE AFTER LOCKING conds_mutex !!! */
void wake_next() {
	long next_thrd = (long)dequeue(conds_queue);
	cnd_signal(&cv_arr[next_thrd]);
}

void exit_all_thrds() {
	/* raise flag */
	done = 1;

	/* wake all threads up */
	mtx_lock(&conds_mutex);
	while (!is_empty(conds_queue)) {
		wake_next();
	}
	mtx_unlock(&conds_mutex);
	
	/* exit */
	thrd_exit(0);
}

/* Returns 0 on success and -1 on failure */
int wait_for_tasks(long thrd_id) {
	/* check if all threads are sleeping */
	if (num_thrds_alive - 1 == num_thrds_waiting && is_empty(paths_queue)) {
		mtx_unlock(&paths_mutex);
		exit_all_thrds();
	}
	
	num_thrds_waiting++;
	
	/* enqueue new waiting thread to conds_queue */
	mtx_lock(&conds_mutex);
	if (enqueue(conds_queue, (void *)thrd_id) < 0) {
		mtx_unlock(&conds_mutex);
		fprintf(stderr, "Error in wait_for_tasks: enqueue failed\n");
		err_in_thrd();
		return -1;
	}
	mtx_unlock(&conds_mutex);

	cnd_wait(&cv_arr[thrd_id], &paths_mutex);

	/* thread woke up! */
	num_thrds_waiting--;
	
	return 0;
}


void exit_if_really_empty() {
	if (done == 1) {
		mtx_unlock(&paths_mutex);
		thrd_exit(0);
	}
}


/* Returns path + "/" + file_name or NULL if fails */
char *update_path(char *path, char *file_name) {
	char *new_path = (char *)malloc(PATH_MAX * sizeof(char));
	if (new_path == NULL) {
		return NULL;
	}
	
	/* create new_path */
	strcpy(new_path, path);
        strcat(new_path, "/");
        strcat(new_path, file_name);
        
        return new_path;
}

/* Returns 0 on success and -1 on failure */
int search_path(char *path, long thrd_id) {
	struct dirent *entry;
	DIR *d;
	struct stat buf;
	char *file_name;
	char *new_path;

	/* open directory on path */
	d = opendir(path);

	if (d == NULL) {
		if (errno != EACCES) {
			fprintf(stderr, "Error in search_path: opendir failed on path %s\n", path);
			err_in_thrd();
			return -1;
		}
		else {
			printf("Directory %s: Permission denied.\n", path);
			return 0;
		}
	}

	/* search directory */
	while ((entry = readdir(d)) != NULL) {
		//printf("thread %ld in while in search_path\n", thrd_id);
		/* extract file name */
		file_name = entry->d_name;

		/* ignore "." and ".." */
		if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
            		continue;
        	}
        	
		/* put path to entry in new_path */
		new_path = update_path(path, file_name);
		if (new_path == NULL) {
			fprintf(stderr, "Error in update_path: malloc failed\n");
			err_in_thrd();		
			return -1;
		}
		
		/* check if entry is a directory */
		if (stat(new_path, &buf) < 0) {
			closedir(d);
			fprintf(stderr, "Error in search_path: stat failed\n");
			err_in_thrd();
			return -1;
		}

		if (S_ISDIR(buf.st_mode)) { /* entry is a directory, enqueue it to paths */
			/* add to paths_queue */
			mtx_lock(&paths_mutex);
			if (enqueue(paths_queue, new_path) < 0) {
				closedir(d);
				mtx_unlock(&paths_mutex);
				fprintf(stderr, "Error in search_path: enqueue failed\n");
				err_in_thrd();
				return -1;
			}
			mtx_unlock(&paths_mutex);

			/* wake up a thread */
			mtx_lock(&conds_mutex);
			if (!is_empty(conds_queue)) {
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

	closedir(d);
	return 0;
	
}

/* Returns 0 on success and -1 on failure */
int thrd_func(void *thrd_id) {
	char *curr_path;
	long id = (long)thrd_id;
	
	/* wait until all threads are created before starting */ 
	mtx_lock(&start_mutex);
	num_thrds_created++;
	cnd_wait(&start_cv, &start_mutex);
	mtx_unlock(&start_mutex);

	while (1) {
		mtx_lock(&paths_mutex);
		
		while (is_empty(paths_queue)) {
			exit_if_really_empty();
			if (wait_for_tasks(id) < 0) {
				return -1;
			}
		}

		curr_path = (char *)dequeue(paths_queue);
		
		mtx_unlock(&paths_mutex);

		if (search_path(curr_path, id) < 0) {
			return -1;
		}
	}
	return 0; /* everything went fine */
}

/* Initializes global variable thrds_arr */
void init_threads() {
	thrds_arr = (thrd_t *)malloc(num_thrds * sizeof(thrd_t));
	if (thrds_arr == NULL) {
		fprintf(stderr, "Error in main: error in malloc\n");
		exit(1);
	}
	
	/* creating threads */
	for (long i = 0; i < num_thrds; ++i) {
		thrd_create(&thrds_arr[i], thrd_func, (void *)i);
	}
	
	/* wake all threads up once all threads created */
	while (1) {
		mtx_lock(&start_mutex);
		if (num_thrds_created == num_thrds) {
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
	if (cv_arr == NULL) {
		fprintf(stderr, "Error in main: error in malloc\n");
		exit(1);
	}
	
	for (int i = 0; i < num_thrds; ++i) {
		cnd_init(&cv_arr[i]);
	}
}

/* Cleans up all CVs in cv_arr */
void destroy_cv_arr() {
	for (int i = 0; i < num_thrds; ++i) {
		cnd_destroy(&cv_arr[i]);
	}
}


/* Initialize all global variables */
void init_global_vars(char *root_path) {
	/* init atomic vars */
	num_thrds_created = 0;
	num_thrds_waiting = 0;
	num_thrds_alive = num_thrds;
	num_files_found = 0;
	done = 0;
	exit_code = 0;
	
	/* init paths queue */
	paths_queue = init_queue();
	if (paths_queue == NULL) {
		fprintf(stderr, "Error in main: init_queue failed\n");
		exit(1);
	}
	if (enqueue(paths_queue, root_path) < 0) {
		fprintf(stderr, "Error in main: enqueue failed\n");
		exit(1);
	}
	
	/* init cond_queue */
	conds_queue = init_queue();
	if (conds_queue == NULL) {
		fprintf(stderr, "Error in main: init_queue failed\n");
		exit(1);
	}
		
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
	DIR *d;

	/* extract arguments */
	if (argc != 4) {
		fprintf(stderr, "Error in main: incorrect number of arguments\n");
		exit(1);
	}
	
	root_path = argv[1];
	search_term = argv[2];
	num_thrds = atoi(argv[3]);
	
	d = opendir(root_path);
	if (d == NULL) {
		fprintf(stderr, "Error in main: root directory can't be searched\n");
		exit(1);
	}
	closedir(d);
	
	/* init global variables */
	init_global_vars(root_path);
	
	/* wait for all threads to complete */
	for (int i = 0; i < num_thrds; i++) {
    		if (thrd_join(thrds_arr[i], NULL) == thrd_error) {
    			fprintf(stderr, "Error in main: thrd_join failed\n");
			exit(1);
		}
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
	exit(exit_code);
}











