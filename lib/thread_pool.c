#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <msg_log.h>
#include <thread_pool.h>

#define true 1
#define false 0


/*任务*/
struct threadpool_task {
	void *(*function)(void *);
	void *arg;
};

/*线程池信息*/
struct thread_info {
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
#define MIN_WAIT_TASK_NUM 10			/*当任务数超过了它，就该添加新线程了*/
#define DEFAULT_THREAD_NUM 10			/*每次创建或销毁的线程个数*/

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

int threadpool_create(void **__pool, unsigned int min_thr_num, unsigned int max_thr_num, unsigned int queue_max_size)
{
	int i;
	struct threadpool *pool = NULL;

	pool = malloc(sizeof(*pool));
	if (!pool) {
		ESLOG_ERR("malloc threadpool false; \n");
		goto out;
	}

	POOL(pool, thread, min_num) = min_thr_num;
	POOL(pool, thread, max_num) = max_thr_num;
	POOL(pool, thread, busy_num) = 0;
	POOL(pool, thread, live_num) = min_thr_num;
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

	if (pthread_mutex_init(&(pool->mutex), NULL) || pthread_mutex_init(&(pool->busy_thr_num_mutex), NULL)) {
		ESLOG_ERR("pthread mutex init false, errno:%d\n", errno);
		goto out;
	} 

	if (pthread_cond_init(&(pool->queue_not_empty), NULL) || pthread_cond_init(&(pool->queue_not_full), NULL)) {
		ESLOG_ERR("pthread cond init false, errno:%d\n", errno);
		goto out;
	}

	for (i = 0; i < min_thr_num; i++)
		pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool);

	pthread_create(&pool->admin_tid, NULL, admin_thread, (void *)pool);

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

	pthread_join(pool->admin_tid, NULL);

	for (i = 0; i < POOL(pool, thread, live_num); i++)
		pthread_cond_broadcast(&(pool->queue_not_empty));

	for (i = 0; i < POOL(pool, thread, live_num); i++)
		pthread_join(pool->threads[i], NULL);

	threadpool_free(&pool);
	return 0;
}

int threadpool_add_task(void *_pool, void *(*function)(void *arg), void *arg)
{
	unsigned int queue_front;
	unsigned int queue_rear;
	unsigned int queue_size;
	unsigned int queue_max_size;
	struct threadpool *pool = _pool;

	pthread_mutex_lock(&(pool->mutex));

	queue_front = POOL(pool, queue, front);
	queue_rear = POOL(pool, queue, rear);
	queue_size = POOL(pool, queue, size);
	queue_max_size = POOL(pool, queue, max_size);

	/*如果队列满了,调用wait阻塞*/
	while ((POOL(pool, queue, size) == queue_max_size) && (!pool->shutdown))
		pthread_cond_wait(&(pool->queue_not_full), &(pool->mutex));

	if (pool->shutdown) {
		pthread_mutex_unlock(&(pool->mutex));
		return -1;
	}

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
	pthread_cond_signal(&(pool->queue_not_empty));
	pthread_mutex_unlock(&(pool->mutex));

	return 0;
}


/*释放线程池*/
static int threadpool_free(struct threadpool **__pool)
{
	struct threadpool *pool = *__pool;
	if (!pool)
		return -1;


	if (pool->threads)
		free(pool->threads);

	pthread_mutex_destroy(&(pool->mutex));
	pthread_mutex_destroy(&(pool->busy_thr_num_mutex));
	pthread_cond_destroy(&(pool->queue_not_empty));
	pthread_cond_destroy(&(pool->queue_not_full));

	if (pool->task_queue)
		free(pool->task_queue);

	free(pool);
	*__pool = NULL;

	return 0;
}

/*管理线程*/
static void *admin_thread(void *threadpool)
{
	int i, add;
	unsigned int queue_size;
	unsigned int live_thr_num;
	unsigned int busy_thr_num;

	struct threadpool *pool = threadpool;
	while (!pool->shutdown) {

		pthread_mutex_lock(&(pool->mutex));
		queue_size = POOL(pool, queue, size);
		live_thr_num = POOL(pool, thread, live_num);
		pthread_mutex_unlock(&(pool->mutex));

		pthread_mutex_lock(&(pool->busy_thr_num_mutex));
		busy_thr_num = POOL(pool, thread, busy_num);
		pthread_mutex_unlock(&(pool->busy_thr_num_mutex));

		ESLOG_INFO("admin busy live -%d--%d-\n", busy_thr_num, live_thr_num);
		ESLOG_INFO("admin queue_size -%d\n", queue_size);

		/*创建新线程 实际任务数量大于 最小正在等待的任务数量，存活线程数小于最大线程数*/
		if (queue_size >= MIN_WAIT_TASK_NUM && live_thr_num <= POOL(pool, thread, max_num)) {
			ESLOG_INFO("admin add-----------\n");

			pthread_mutex_lock(&(pool->mutex));
			add = 0;

			/*一次增加 DEFAULT_THREAD_NUM 个线程*/
			for (i = 0; add < DEFAULT_THREAD_NUM; i++) {

				if (i < POOL(pool, thread, max_num) && POOL(pool, thread, live_num) < POOL(pool, thread, max_num))
					break;

				if (pool->threads[i] == 0 || !is_thread_alive(pool->threads[i])) {
					if (pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool)) {
						ESLOG_ERR("pthread create false\n");
						return NULL;
					}
					pthread_detach(pool->threads[i]);

					add++;
					POOL(pool, thread, live_num)++;
					ESLOG_INFO("new thread -----------------------\n");
				}
			}

			pthread_mutex_unlock(&(pool->mutex));
		}

		/*销毁多余的线程 忙线程x2 都小于 存活线程，并且存活的大于最小线程数*/
		if ((busy_thr_num * 2) < live_thr_num  &&  live_thr_num > POOL(pool, thread, min_num)) {
			ESLOG_INFO("admin busy --%d--%d----\n", busy_thr_num, live_thr_num);
			/*一次销毁DEFAULT_THREAD_NUM个线程*/
			pthread_mutex_lock(&(pool->mutex));
			POOL(pool, thread, exit_num) = DEFAULT_THREAD_NUM;
			pthread_mutex_unlock(&(pool->mutex));

			for (i = 0; i < DEFAULT_THREAD_NUM; i++) {
				//通知正在处于空闲的线程，自杀
				pthread_cond_signal(&(pool->queue_not_empty));
				ESLOG_INFO("admin cler --\n");
			}
		}
	}

	return NULL;
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
	struct threadpool *pool = threadpool;
	struct threadpool_task task;

	for (;;) {
		pthread_mutex_lock(&pool->mutex);

		//无任务则阻塞在 任务队列不为空 上，有任务则跳出
		while ((POOL(pool, queue, size) == 0) && (!pool->shutdown)) { 
			ESLOG_INFO("thread 0x%x is waiting \n", (unsigned int)pthread_self());
			pthread_cond_wait(&(pool->queue_not_empty), &(pool->mutex));

			//判断是否需要清除线程,自杀功能
			ESLOG_INFO("exit_num:%d\n", POOL(pool, thread, exit_num));
			if (POOL(pool, thread, exit_num) > 0) {
				POOL(pool, thread, exit_num)--;
				//判断线程池中的线程数是否大于最小线程数，是则结束当前线程
				if (POOL(pool, thread, live_num) > POOL(pool, thread, min_num)) {
					ESLOG_INFO("thread 0x%x is exiting \n", (unsigned int)pthread_self());
					POOL(pool, thread, live_num)--;
					break;
				}
			}
		}

		//线程池开关状态
		if (pool->shutdown) {
			ESLOG_INFO("shutdown true 0x%x \n", (unsigned int)pthread_self());
			break;
		}

		//否则该线程可以拿出任务
		task.function = pool->task_queue[POOL(pool, queue, front)].function;
		task.arg = pool->task_queue[POOL(pool, queue, front)].arg;

		POOL(pool, queue, front) = (POOL(pool, queue, front) + 1) % POOL(pool, queue, max_size);
		POOL(pool, queue, size)--;


		//通知可以添加新任务
		pthread_cond_broadcast(&pool->queue_not_full);

		//释放线程锁
		pthread_mutex_unlock(&pool->mutex);

		//执行刚才取出的任务
		ESLOG_INFO("thread 0x%x start working \n", (unsigned int)pthread_self());
		pthread_mutex_lock(&pool->busy_thr_num_mutex);
		POOL(pool, thread, busy_num)++;
		pthread_mutex_unlock(&pool->busy_thr_num_mutex);

		(*(task.function))(task.arg);

		//任务结束处理
		ESLOG_INFO("thread 0x%x end working \n", (unsigned int)pthread_self());
		pthread_mutex_lock(&pool->busy_thr_num_mutex);
		POOL(pool, thread, busy_num)--;
		pthread_mutex_unlock(&pool->busy_thr_num_mutex);
	}

	pthread_mutex_unlock(&(pool->mutex));

	return NULL;
}

