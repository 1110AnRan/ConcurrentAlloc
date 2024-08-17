#include"PageCache.h"

PageCache PageCache::_sInst;

//获取一个k页的span
Span* PageCache::NewSpan(size_t k)
{
	// k∈[1, 128]
	// assert(k > 0 && k < NPAGES);
	
	// k∈[1, ∞]
	assert(k > 0);
	if (k > NPAGES - 1) {	//大于128页直接找堆申请
		void* ptr = SystemAlloc(k);
		// 替换
		// Span* span = new Span;
		Span* span = PageCache::_spanPool._new();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		//建立页号与span之间的映射
		// _idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);
		return span;
	}
	// 小于等于128页
	// 如果当前的桶有span，返回一个span
	if (!_spanLists[k].Empty()) {
		Span* kSpan = _spanLists[k].PopFront();

		// 建立页号与span的映射，方便cc回收tc释放的内存
		for (PAGE_ID i = 0; i < kSpan->_n; i++) {
			// _idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}
		return kSpan;
	}

	// 在k号桶后面找一个非空的span
	for (size_t i = k + 1; i < NPAGES; i++) {
		if (!_spanLists[i].Empty()) {
			Span* nSpan = _spanLists[i].PopFront();
			// 替换
			// Span* kSpan = new Span;
			Span* kSpan = PageCache::_spanPool._new();
			// 把nSpan的头部k页切下来
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);
			//存储nSpan首尾页号与span对应的关系，方便pc回收
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);
			// 建立页号与span的映射，方便cc回收tc释放的内存
			for (PAGE_ID i = 0; i < kSpan->_n; i++) {
				// _idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}
			return kSpan;
		}
	}
	//到这说明桶里面没有比k页更大的桶了，直接向OS申请一个128页的span

	// Span* BigSpan = new Span;
	Span* bigSpan = PageCache::_spanPool._new();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// 页号与span的对应关系
Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;

	// std::unique_lock<std::mutex> lock(_pageMtx);	 //构造时加锁，析构时自动解锁
	// auto ret = _idSpanMap.find(id);
	Span* ret = (Span*)_idSpanMap.get(id);
	if (/*ret != _idSpanMap.end()*/ ret != nullptr) {
		return ret;
	}
	else {
		assert(false);
		return nullptr;
	}
}

// 释放空闲的span给pc，同时尝试进行前后合并
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	if (span->_n > NPAGES - 1) {
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);

		SystemFree(ptr);
		// delete span;
		PageCache::_spanPool._delete(span);
		return;
	}
	// 进行前合并
	while (1) {
		// auto ret = _idSpanMap.find(span->_pageId - 1);
		Span* ret = (Span*)_idSpanMap.get(span->_pageId - 1);
		if (ret == nullptr) {
			break;
		}
		Span* prevSpan = ret;
		// 如果前一个span正在被使用
		if (prevSpan->_isUse) {
			break;
		}

		// 如果前一个span管理的页数加上当前span的页数大于128
		if (prevSpan->_n + span->_n > NPAGES - 1) {
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);	//在对应的哈希桶中将其删除

		// delete prevSpan;
		PageCache::_spanPool._delete(prevSpan);
	}
	// 进行后合并
	while (1) {
		// auto ret = _idSpanMap.find(span->_pageId + span->_n);
		Span* ret = (Span*)_idSpanMap.get(span->_pageId + span->_n);
		if (ret == nullptr) {
			break;
		}

		Span* nextSpan = ret;
		// 如果正在被使用
		if (nextSpan->_isUse) {
			break;
		}
		// 如果合并后的页数大于128
		if (nextSpan->_n + span->_n > NPAGES - 1) {
			break;
		}

		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);

		// delete nextSpan;
		PageCache::_spanPool._delete(nextSpan);
	}
	// 插入到对应的桶中
	_spanLists[span->_n].PushFront(span);
	// 建立对应的首尾页映射
	// _idSpanMap[span->_pageId] = span;
	// _idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
	// 将该span设置为未被使用的状态
	span->_isUse = false;
}
