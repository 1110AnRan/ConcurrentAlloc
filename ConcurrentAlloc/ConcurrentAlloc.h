#pragma once
#include"ThreadCache.h"
#include"PageCache.h"

static void* ConcurrentAlloc(size_t byte) {
	if (byte > MAX_BYTES) {	//大于256KB的申请

		// 计算出对齐后需要申请的页数
		size_t alignSize = SizeClass::RoundUp(byte);
		size_t kPage = alignSize >> PAGE_SHIFT;

		// 对pc操作一定要上锁
		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kPage);
		PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else {
		// 通过TLS，每个线程独享tc
		if (pTLSThreadCache == nullptr) {
			// pTLSThreadCache = new ThreadCache;
			static std::mutex tcMtx;
			static ObjectPool<ThreadCache> tcPool;
			// 防止出现线程安全问题
			tcMtx.lock();
			pTLSThreadCache = tcPool._new();
			tcMtx.unlock();
		}
		return pTLSThreadCache->Allocate(byte);
	}
	
}

static void ConcurrentFree(void* ptr) {
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t byte = span->_objSize;
	if (byte > MAX_BYTES) {
		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();

	}
	else {
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, byte);
	}
	
}
