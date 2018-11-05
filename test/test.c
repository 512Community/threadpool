#include <stdio.h>
#include <thread_pool.h>

void *test(void *arg)
{
	printf("hello world\n");
}

int main(int argc, const char *argv[])
{
	int rc;
	void *pool;
	int i;

	rc = threadpool_create(&pool, 0, 50, 10);
	if (rc < 0) {
		printf("threadpool_create false\n");
		return -1;
	}

	for (i = 0; i < 100; i++) {
		rc = threadpool_add_task(pool, test, NULL);
		if (rc < 0) {
			printf("threadpool_create false\n");
			return -1;
		}

		printf("i:%d\n", i);

	}
	
	threadpool_destroy(pool);

	return 0;
}
