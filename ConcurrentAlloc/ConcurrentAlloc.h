#pragma once
#include"ThreadCache.h"
#include"PageCache.h"

static void* ConcurrentAlloc(size_t byte) {
	if (byte > MAX_BYTES) {	//����256KB������

		// ������������Ҫ�����ҳ��
		size_t alignSize = SizeClass::RoundUp(byte);
		size_t kPage = alignSize >> PAGE_SHIFT;

		// ��pc����һ��Ҫ����
		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kPage);
		PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else {
		// ͨ��TLS��ÿ���̶߳���tc
		if (pTLSThreadCache == nullptr) {
			// pTLSThreadCache = new ThreadCache;
			static std::mutex tcMtx;
			static ObjectPool<ThreadCache> tcPool;
			// ��ֹ�����̰߳�ȫ����
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
