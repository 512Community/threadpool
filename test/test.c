#include <stdio.h>
#include <unistd.h>
#include <thread_pool.h>
#include <pthread.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int num = 0;
void *test(void *arg)
{
	pthread_mutex_lock(&mutex);
	printf("hello world:%d\n", num);
	num++;
	usleep(1000);
	pthread_mutex_unlock(&mutex);
}

int main(int argc, const char *argv[])
{
	int rc;
	void *pool;
	int i;

	rc = threadpool_create(&pool, 0, 100, 1000, 10000);
	if (rc < 0) {
		printf("threadpool_create false\n");
		return -1;
	}

	for (i = 0; i < 10000; i++) {
		rc = threadpool_add_task(pool, test, NULL);
		if (rc < 0) {
			printf("threadpool_create false\n");
			return -1;
		}

	}
	
	sleep(20);
	threadpool_destroy(pool, FLAGS_WAIT_TASK_EXIT);

	return 0;
}
