#pragma once
#include "Common.h"

// 由于全局只有一个CentralCache
// 所以使用单例模式
class CentralCache {
public:

	//单例接口
	static CentralCache* GetInstance() {
		return &_sInst;
	}

	//从central cache获取一定数量batchNum的对象给thread cache
	// 
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t byte);


	// 获得一个span
	Span* GetOneSpan(SpanList& spanlist, size_t byte);

	// 将tc中空闲的长对象释放回给cc
	void ReleaseListToSpans(void* start, size_t byte);

private:
	SpanList _spanLists[NFREELIST];
private:
	CentralCache(){}

	CentralCache(const CentralCache&) = delete;	//防拷贝

	static CentralCache _sInst;
};