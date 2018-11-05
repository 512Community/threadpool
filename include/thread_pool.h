#ifndef __THREAD_POOL__h__
#define __THREAD_POOL__h__

#ifdef __cplusplus
extern "C" {
#endif
	int threadpool_create(void **__pool, unsigned int min_thr_num, unsigned int max_thr_num, unsigned int queue_max_size);
	/*销毁线程池*/
	int threadpool_destroy(void *pool);
	/*向线程池的任务队列中添加一个任务*/
	int threadpool_add_task(void *pool, void *(*function)(void *arg), void *arg);

#ifdef __cplusplus
}
#endif

#endif
