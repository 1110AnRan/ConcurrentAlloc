#pragma once
#include"Common.h"

//单例模式
class PageCache {
public:
	static PageCache* GetInstance() {
		return &_sInst;
	}

	// 向os申请k个大小的页
	Span* NewSpan(size_t k);

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	// 将cc中空闲的span回收
	void ReleaseSpanToPageCache(Span* span);
private:
	// 使用定长内存池替换new和delete
	ObjectPool<Span> _spanPool;
	SpanList _spanLists[NPAGES];	//下标i存储的是i个页的span
	// std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;
public:
	std::mutex _pageMtx;	//大锁
private:
	PageCache() {}
	PageCache(const PageCache&) = delete;

	static PageCache _sInst;
};