#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <thread_pool.h>
#include <stdint.h>

#define PTHREAD_ERR(func, loop) do { \
	if(func) { 	\
		printf(#func " failed:%d\n", errno);\
		goto loop; 		\
	}  				\
} while(0)\

/*任务*/
struct threadpool_task {
	void *(*function)(void *);
	void *arg;
};

/*线程池信息*/
struct thread_info {
	unsigned int max_num;		/* 线程池中最大线程数 */
};

/*任务队列信息*/
struct task_queue_info {
	unsigned int front;		/* 队头 */
	unsigned int rear;		/* 队尾 */
	unsigned int size;		/* 已经存在的任务数 */
	unsigned int max_size;		/* 队列能容纳的最大任务数 */
};
/*线程池管理*/
struct threadpool {
#define POOL(ptr, type, number) ((ptr)->type##_info.number)

	pthread_mutex_t mutex;			/* 锁住整个结构体 */
	pthread_cond_t  queue_not_full;		/* 条件变量，任务队列不为满 */
	pthread_cond_t  queue_not_empty;	/* 任务队列不为空 */

	struct thread_info thread_info;
	struct task_queue_info queue_info;

	pthread_t *threads;			/* 存放线程的tid,实际上就是管理了线 数组 */
	struct threadpool_task *task_queue;	/* 任务队列 */

	/*状态*/
	int shutdown;				/* true为关闭 */

};

static void *threadpool_thread(void *threadpool);
static int threadpool_free(struct threadpool **__pool);

int threadpool_create(void **__pool,
		      unsigned int max_thr_num,
		      unsigned int queue_max_size)
{
	int i;
	struct threadpool *pool = NULL;

	pool = calloc(1, sizeof(*pool));
	if (!pool) {
		printf("malloc threadpool false; \n");
		goto out;
	}

	POOL(pool, thread, max_num) = max_thr_num;
	POOL(pool, queue, max_size) = queue_max_size;

	pool->threads = calloc(max_thr_num, sizeof(pthread_t));
	if (!pool->threads) {
		printf("calloc pthread_t false; \n");
		goto out;
	}

	pool->task_queue = calloc(queue_max_size, sizeof(struct threadpool_task));
	if (!pool->task_queue) {
		printf("calloc threadpool_task false; \n");
		goto out;
	}

	PTHREAD_ERR(pthread_mutex_init(&(pool->mutex), NULL), out);
	PTHREAD_ERR(pthread_cond_init(&(pool->queue_not_empty), NULL), out);
	PTHREAD_ERR(pthread_cond_init(&(pool->queue_not_full), NULL), out);

	for (i = 0; i < max_thr_num; i++)
		PTHREAD_ERR(pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool), out);

	*__pool = pool;

	return 0;

out:
	threadpool_free(&pool);
	*__pool = NULL;
	return -1;
}

int threadpool_destroy(void *_pool)
{
	int i;
	struct threadpool *pool = _pool;

	if (pool == NULL)
		return -1;

	pool->shutdown = 1;
	for (i = 0; i < POOL(pool, thread, max_num); i++)
		PTHREAD_ERR(pthread_cond_broadcast(&(pool->queue_not_empty)), out);

	for (i = 0; i < POOL(pool, thread, max_num); i++)
		PTHREAD_ERR(pthread_join(pool->threads[i], NULL), out);

	threadpool_free(&pool);
	return 0;
out:
	return -1;
}

int threadpool_add_task(void *_pool, void *(*function)(void *arg), void *arg)
{
	unsigned int queue_front;
	unsigned int queue_rear;
	unsigned int queue_size;
	unsigned int queue_max_size;
	struct threadpool *pool = _pool;

	pthread_mutex_lock(&pool->mutex);

	queue_front = POOL(pool, queue, front);
	queue_rear = POOL(pool, queue, rear);
	queue_size = POOL(pool, queue, size);
	queue_max_size = POOL(pool, queue, max_size);

	/*如果队列满了,调用wait阻塞*/
	while ((POOL(pool, queue, size) == queue_max_size) && (!pool->shutdown))
		PTHREAD_ERR(pthread_cond_wait(&pool->queue_not_full, &pool->mutex), out);

	if (pool->shutdown)
		goto out;

	/*添加任务到任务队列*/
	pool->task_queue[queue_rear].function = function;
	pool->task_queue[queue_rear].arg = arg;

	POOL(pool, queue, rear) = (queue_rear + 1) % queue_max_size;  /* 逻辑环  */
	POOL(pool, queue, size)++;

	/*添加完任务后,队列就不为空了,唤醒线程池中的一个线程*/
	PTHREAD_ERR(pthread_cond_signal(&pool->queue_not_empty), out);
	pthread_mutex_unlock(&pool->mutex);

	return 0;
out:
	pthread_mutex_unlock(&pool->mutex);
	return -1;
}


/*释放线程池*/
static int threadpool_free(struct threadpool **__pool)
{
	struct threadpool *pool = *__pool;
	if (!pool)
		return -1;


	if (pool->threads)
		free(pool->threads);

	pthread_mutex_destroy(&pool->mutex);
	pthread_cond_destroy(&pool->queue_not_empty);
	pthread_cond_destroy(&pool->queue_not_full);

	if (pool->task_queue)
		free(pool->task_queue);

	free(pool);
	*__pool = NULL;

	return 0;
}

static void *threadpool_thread(void *threadpool)
{
	unsigned int i;
	struct threadpool *pool = threadpool;
	struct threadpool_task task;

	for (;;) {
		pthread_mutex_lock(&pool->mutex);
		//无任务等待，有任务则跳出
		while (!(POOL(pool, queue, size) | pool->shutdown))
			PTHREAD_ERR(pthread_cond_wait(&pool->queue_not_empty, &pool->mutex), out);

		if (pool->shutdown)
			goto out;

		task.function = pool->task_queue[POOL(pool, queue, front)].function;
		task.arg = pool->task_queue[POOL(pool, queue, front)].arg;

		POOL(pool, queue, front) = (POOL(pool, queue, front) + 1) % POOL(pool, queue, max_size);
		POOL(pool, queue, size)--;

		//通知可以添加新任务
		PTHREAD_ERR(pthread_cond_broadcast(&pool->queue_not_full), out);
		pthread_mutex_unlock(&pool->mutex);

		(*(task.function))(task.arg);
	}
out:
	pthread_mutex_unlock(&pool->mutex);

	return NULL;
}
