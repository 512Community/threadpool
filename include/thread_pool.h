#ifndef __THREAD_POOL__h__
#define __THREAD_POOL__h__

#define FLAGS_WAIT_TASK_EXIT 1
#ifdef __cplusplus
extern "C" {
#endif
	/* function:创建线程池
	 * value:
	 * 	__pool:句柄参数.
	 * 	default_thr_num:线程池动态创建或销毁一次的线程数,如果传入0则使用100作为默认值.
	 * 	min_thr_num:线程池保留的最小线程数.
	 * 	max_thr_num:线程池保留的最大线程数.
	 * 	queue_max_size:任务队列保留的最大任务数.
	 * return value:
	 * 	0为成功,小于0为失败.
	 * */
	int threadpool_create(void **__pool, unsigned int default_thr_num, unsigned int min_thr_num,
						unsigned int max_thr_num, unsigned int queue_max_size);
	/* function:销毁线程池
	 * value:
	 * 	_pool:句柄参数
	 * 	flags:设置为FLAGS_WAIT_TASK_EXIT,则等待线程池中所有任务结束后才销毁线程池，
	 * 	      设置为!FLAGS_WAIT_TASK_EXIT,则直接销毁线程池,不考虑在这执行的任务。
	 * return value:
	 * 	0为成功,小于0为失败.
	 * */
	int threadpool_destroy(void *_pool, int flags);

	/* function:向线程池的任务队列中添加一个任务
	 * value:
	 * 	pool:句柄参数
	 * 	functio:线程执行任务
	 * 	arg:线程参数
	 * return value:
	 * 	0为成功,小于0为失败.
	 * */
	int threadpool_add_task(void *pool, void *(*function)(void *arg), void *arg);

#ifdef __cplusplus
}
#endif

#endif
