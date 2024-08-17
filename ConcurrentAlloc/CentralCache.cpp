#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

// byte 获取的容量大小
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t byte)
{
    // 获取bytes对应的SpanList
    size_t index = SizeClass::Index(byte);

    //cc上锁
    _spanLists[index]._mtx.lock();

    // 在对应的哈希桶中获取一个span
    Span* span = GetOneSpan(_spanLists[index], byte);
    // 检查
    assert(span);
    assert(span->_freeList);

    size_t actualNum = 1;
    start = span->_freeList;
    end = span->_freeList;

    while (NextObj(end) && n - 1) {
        end = NextObj(end);
        actualNum++;
        n--;
    }
    // 返回[start, end]给tc，调整span的_freeList的值
    span->_freeList = NextObj(end);

    NextObj(end) = nullptr;
    span->_useCount += actualNum;
    _spanLists[index]._mtx.unlock();    //操作完成后解锁
    return actualNum;
}

// spanlist 当前的spanlist
// byte 对象的大小
Span* CentralCache::GetOneSpan(SpanList& spanlist, size_t byte)
{
    //1.先在对应的spanlist中寻找span
    Span* it = spanlist.Begin();
    while (it != spanlist.End()) {
        if (it->_freeList != nullptr) { //找到了就return
            return it;
        }
        else {
            it = it->_next; //没找到就继续找
        }
    }

   
    //2.若没有，则需要向pagecache申请span
    // 先把锁放掉，防止tc释放内存时阻塞
    spanlist._mtx.unlock();
    PageCache::GetInstance()->_pageMtx.lock();
    Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(byte));
    span->_isUse = true;
    span->_objSize = byte;
    PageCache::GetInstance()->_pageMtx.unlock();
    // 计算span的大块内存的起始地址和大块内存的大小
    char* start = (char*)(span->_pageId << PAGE_SHIFT);
    size_t bytes = span->_n << PAGE_SHIFT;
    // 切成size大小的块
    char* end = start + bytes;
    span->_freeList = start;
    void* tail = start;
    start += byte;
    while (start < end) {
        NextObj(tail) = start;
        tail = NextObj(tail);
        start += byte;
    }
    NextObj(tail) = nullptr;    //置空
    //将切好的span头插到spanList
    spanlist._mtx.lock();
    spanlist.PushFront(span);
    return span;
}

// byte 每个对象的大小字节数
void CentralCache::ReleaseListToSpans(void* start, size_t byte)
{
    // 找到对应的桶
    size_t index = SizeClass::Index(byte);

    // 上锁
    _spanLists[index]._mtx.lock();
    while (start) {
        // 头插
        void* next = NextObj(start);
        Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

        NextObj(start) = span->_freeList;
        span->_freeList = start;
        start = next;

        span->_useCount--;  //更新计数
        if (span->_useCount == 0) { //说明分出去的对象都被释放了，这时将span释放给pc
            // 把这个span从spanlists中删除，回收给pc
            _spanLists[index].Erase(span);
            span->_freeList = nullptr;
            span->_next = nullptr;
            span->_prev = nullptr;

            _spanLists[index]._mtx.unlock();
            // 对pc操作前一定要上锁
            PageCache::GetInstance()->_pageMtx.lock();
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);
            PageCache::GetInstance()->_pageMtx.unlock();
            _spanLists[index]._mtx.lock();
        }
        
        
    }
    _spanLists[index]._mtx.unlock();    //解锁
}
