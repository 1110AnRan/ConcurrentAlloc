#pragma once

#include "Common.h"

class ThreadCache {
public:
	//申请大小为byte的对象
	void* Allocate(size_t byte);
	
	// 释放地址从ptr开始的大小为byte的对象
	void Deallocate(void* ptr, size_t byte);

	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t byte);

	// 空闲内存过长，将其还给cc
	void ListTooLong(FreeList& list, size_t byte);
private:
	FreeList _freeLists[NFREELIST];
};

//TLS(Thread Loacl Storage)
static __declspec(thread) ThreadCache* pTLSThreadCache = nullptr;