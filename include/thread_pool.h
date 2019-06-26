#ifndef __THREAD_POOL__h__
#define __THREAD_POOL__h__

#ifdef __cplusplus
extern "C" {
#endif
	/* function:创建线程池
	 * value:
	 * 	__pool:句柄参数.
	 * 	max_thr_num:线程池保留的最大线程数.
	 * 	queue_max_size:任务队列保留的最大任务数.
	 * return value:
	 * 	0为成功,小于0为失败.
	 * */
	int threadpool_create(void **__pool, unsigned int max_thr_num, unsigned int queue_max_size);
	/* function:销毁线程池
	 * value:
	 * 	_pool:句柄参数
	 * return value:
	 * 	0为成功,小于0为失败.
	 * */
	int threadpool_destroy(void *_pool);

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
