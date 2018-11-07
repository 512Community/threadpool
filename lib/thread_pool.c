#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <msg_log.h>
#include <thread_pool.h>

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

#define PTHREAD_ERR(func, loop) do { \
	if(func) { 	\
		ESLOG_ERR(#func " failed:%d\n", errno);\
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
	unsigned int default_num;       /* 动态创建和销毁的线程数 */
	unsigned int min_num;		/* 线程池中最小线程数 */
	unsigned int max_num;		/* 线程池中最大线程数 */
	unsigned int live_num;		/* 线程池中存活的线程数 */
	unsigned int busy_num;		/* 忙线程，正在工作的线程 */
	unsigned int exit_num;		/* 需要销毁的线程数 */
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
#define DEFAULT_THREAD_NUM 100			/* 每次创建或销毁的线程个数*/

	pthread_mutex_t mutex;			/* 锁住整个结构体 */
	pthread_mutex_t busy_thr_num_mutex;	/* 用于使用忙线程数时的锁 */
	pthread_cond_t  queue_not_full;		/* 条件变量，任务队列不为满 */
	pthread_cond_t  queue_not_empty;	/* 任务队列不为空 */

	struct thread_info thread_info;
	struct task_queue_info queue_info;

	pthread_t *threads;			/* 存放线程的tid,实际上就是管理了线 数组 */
	pthread_t admin_tid;			/* 管理者线程tid */
	struct threadpool_task *task_queue;	/* 任务队列 */

	/*状态*/
	int shutdown;				/* true为关闭 */

};

static void *threadpool_thread(void *threadpool);
static int threadpool_free(struct threadpool **__pool);
static void *admin_thread(void *threadpool);
static int is_thread_alive(pthread_t tid);
static int thread_create(struct threadpool *threadpool);
static int thread_destory(struct threadpool *threadpool);

int threadpool_create(void **__pool,
		      unsigned int default_thr_num,
		      unsigned int min_thr_num,
		      unsigned int max_thr_num,
		      unsigned int queue_max_size)
{
	int i;
	struct threadpool *pool = NULL;

	pool = malloc(sizeof(*pool));
	if (!pool) {
		ESLOG_ERR("malloc threadpool false; \n");
		goto out;
	}

	if (default_thr_num)
		POOL(pool, thread, default_num) = default_thr_num;
	else
		POOL(pool, thread, default_num) = DEFAULT_THREAD_NUM;

	POOL(pool, thread, min_num) = min_thr_num;
	POOL(pool, thread, max_num) = max_thr_num;
	POOL(pool, thread, busy_num) = 0;
	POOL(pool, thread, live_num) = min_thr_num;
	POOL(pool, thread, exit_num) = 0;
	POOL(pool, thread, exit_num) = 0;

	POOL(pool, queue, front) = 0;
	POOL(pool, queue, rear) = 0;
	POOL(pool, queue, size) = 0;
	POOL(pool, queue, max_size) = queue_max_size;
	pool->shutdown = false;

	pool->threads = calloc(max_thr_num, sizeof(pthread_t));
	if (!pool->threads) {
		ESLOG_ERR("calloc pthread_t false; \n");
		goto out;
	}

	pool->task_queue = calloc(queue_max_size, sizeof(struct threadpool_task));
	if (!pool->task_queue) {
		ESLOG_ERR("calloc threadpool_task false; \n");
		goto out;
	}

	PTHREAD_ERR(pthread_mutex_init(&(pool->mutex), NULL), out);
	PTHREAD_ERR(pthread_mutex_init(&(pool->busy_thr_num_mutex), NULL), out);
	PTHREAD_ERR(pthread_cond_init(&(pool->queue_not_empty), NULL), out);
	PTHREAD_ERR(pthread_cond_init(&(pool->queue_not_full), NULL), out);

	for (i = 0; i < min_thr_num; i++)
		PTHREAD_ERR(pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool), out);

	PTHREAD_ERR(pthread_create(&pool->admin_tid, NULL, admin_thread, (void *)pool), out);

	*__pool = pool;

	return 0;

out:
	threadpool_free(&pool);
	*__pool = NULL;
	return -1;
}

int threadpool_destroy(void *_pool, int flags)
{
	int i;
	struct threadpool *pool;

	pool = _pool;
	if (pool == NULL)
		return -1;

	if(flags)
		while(POOL(pool, queue, size));

	pool->shutdown = true;
	for (i = 0; i < POOL(pool, thread, live_num); i++)
		PTHREAD_ERR(pthread_cond_broadcast(&(pool->queue_not_empty)), out);

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

	/*清空工作线程的回调函数的参数arg*/
	if (pool->task_queue[queue_rear].arg) {
		free(pool->task_queue[queue_rear].arg);
		pool->task_queue[queue_rear].arg = NULL;
	}

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
	pthread_mutex_destroy(&pool->busy_thr_num_mutex);
	pthread_cond_destroy(&pool->queue_not_empty);
	pthread_cond_destroy(&pool->queue_not_full);

	if (pool->task_queue)
		free(pool->task_queue);

	free(pool);
	*__pool = NULL;

	return 0;
}

/*管理线程*/
static void *admin_thread(void *threadpool)
{
	pthread_detach(pthread_self());
	struct threadpool *pool = threadpool;
	while (!pool->shutdown) {
		if(thread_create(pool) < 0)
			return NULL;

		if(thread_destory(pool) < 0)
			return NULL;
	}

	return NULL;
}
static int thread_create(struct threadpool *pool)
{
	unsigned int i, add, queue_size;
	unsigned int default_thr_num, live_thr_num, max_thr_num;

	pthread_mutex_lock(&pool->mutex);
	max_thr_num = POOL(pool, thread, max_num);
	default_thr_num = POOL(pool, thread, default_num);
	queue_size = POOL(pool, queue, size);
	live_thr_num = POOL(pool, thread, live_num);
	pthread_mutex_unlock(&pool->mutex);

	/* 创建新线程--live的线程数小于实际任务数量,并且存活线程数小于最大线程数 */
	/* 如果实际任务很多,并且执行的速度很慢，那么线程池很快就会到达max */
	if (live_thr_num < queue_size && live_thr_num < max_thr_num) {
		pthread_mutex_lock(&pool->mutex);

		/* 如果 pool->threads中一直没有有效值,则循环结束,主要防止max小于default的情况发生 */
		/* 如果 创建default个线程，则循环结束 */
		for (i = 0, add = 0; i < max_thr_num && add < default_thr_num; i++) {
			/* 如果live此时大于max，那么就不在继续创建 */
			if (POOL(pool, thread, live_num) > max_thr_num)
				break;

			if (pool->threads[i] == 0 || !is_thread_alive(pool->threads[i])) {
				PTHREAD_ERR(pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool), out);
				add++;
				POOL(pool, thread, live_num)++;
			}
		}
		pthread_mutex_unlock(&pool->mutex);
	}

	return 0;
out:
	pthread_mutex_unlock(&pool->mutex);
	return -1;

}

static int thread_destory(struct threadpool *pool)
{
	unsigned int i, queue_size, default_thr_num;
	unsigned int live_thr_num, min_thr_num, busy_thr_num;

	pthread_mutex_lock(&pool->mutex);
	min_thr_num = POOL(pool, thread, min_num);
	default_thr_num = POOL(pool, thread, default_num);
	queue_size = POOL(pool, queue, size);
	live_thr_num = POOL(pool, thread, live_num);
	pthread_mutex_unlock(&pool->mutex);

	pthread_mutex_lock(&pool->busy_thr_num_mutex);
	busy_thr_num = POOL(pool, thread, busy_num);
	pthread_mutex_unlock(&pool->busy_thr_num_mutex);
	/* busy的线程x2 大于live的线程，并且live的线程大于min线程,通知线程池的线程进行自杀 */
	/* busy x2并没有明确的要求 */
	/* live > min 是为了保证线程池最少有min个线程living */
	if ((busy_thr_num * 2) < live_thr_num  &&  live_thr_num > min_thr_num) {
		pthread_mutex_lock(&pool->mutex);
		POOL(pool, thread, exit_num) = default_thr_num;
		pthread_mutex_unlock(&pool->mutex);

		for (i = 0; i < default_thr_num; i++) {
			//通知正在处于空闲的线程，自杀
			PTHREAD_ERR(pthread_cond_signal(&pool->queue_not_empty), out);
		}
	}

	return 0;
out:
	return -1;
}

static int is_thread_alive(pthread_t tid)
{
	int rc;
	rc = pthread_kill(tid, 0);
	if (rc == ESRCH)
		return false;

	return true;
}

static void *threadpool_thread(void *threadpool)
{
	unsigned int i;
	struct threadpool *pool = threadpool;
	struct threadpool_task task;

	pthread_detach(pthread_self());
	for (;;) {
		pthread_mutex_lock(&pool->mutex);
		//无任务等待，有任务则跳出
		while (!(POOL(pool, queue, size) | pool->shutdown)) {
			PTHREAD_ERR(pthread_cond_wait(&pool->queue_not_empty, &pool->mutex), out);

			//判断是否需要清除线程,自杀功能
			if (POOL(pool, thread, exit_num) == 0)
				continue;

			ESLOG_INFO("exit_num:%d\n", POOL(pool, thread, exit_num));
			POOL(pool, thread, exit_num)--;
			//如果live的线程数大于min线程，则销毁掉.
			if (POOL(pool, thread, live_num) > POOL(pool, thread, min_num)) {
				POOL(pool, thread, live_num)--;
				for (i = 0; i < POOL(pool, thread, max_num); i++) {
					if(pthread_equal(pool->threads[i], pthread_self())) {
						memset(pool->threads + i, 0, sizeof(pthread_t));
						break;
					}
				}
				goto out;
			}
		}

		//线程池开关状态
		if (pool->shutdown)
			goto out;

		//否则该线程可以拿出任务
		task.function = pool->task_queue[POOL(pool, queue, front)].function;
		task.arg = pool->task_queue[POOL(pool, queue, front)].arg;

		POOL(pool, queue, front) = (POOL(pool, queue, front) + 1) % POOL(pool, queue, max_size);
		POOL(pool, queue, size)--;


		//通知可以添加新任务
		PTHREAD_ERR(pthread_cond_broadcast(&pool->queue_not_full), out);

		//释放线程锁
		pthread_mutex_unlock(&pool->mutex);

		//执行刚才取出的任务
		pthread_mutex_lock(&pool->busy_thr_num_mutex);
		POOL(pool, thread, busy_num)++;
		pthread_mutex_unlock(&pool->busy_thr_num_mutex);

		(*(task.function))(task.arg);

		//任务结束处理
		pthread_mutex_lock(&pool->busy_thr_num_mutex);
		POOL(pool, thread, busy_num)--;
		pthread_mutex_unlock(&pool->busy_thr_num_mutex);
	}
out:
	pthread_mutex_unlock(&pool->mutex);

	return NULL;
}

