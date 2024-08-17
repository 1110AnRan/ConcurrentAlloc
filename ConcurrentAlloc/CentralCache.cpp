#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

// byte ��ȡ��������С
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t byte)
{
    // ��ȡbytes��Ӧ��SpanList
    size_t index = SizeClass::Index(byte);

    //cc����
    _spanLists[index]._mtx.lock();

    // �ڶ�Ӧ�Ĺ�ϣͰ�л�ȡһ��span
    Span* span = GetOneSpan(_spanLists[index], byte);
    // ���
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
    // ����[start, end]��tc������span��_freeList��ֵ
    span->_freeList = NextObj(end);

    NextObj(end) = nullptr;
    span->_useCount += actualNum;
    _spanLists[index]._mtx.unlock();    //������ɺ����
    return actualNum;
}

// spanlist ��ǰ��spanlist
// byte ����Ĵ�С
Span* CentralCache::GetOneSpan(SpanList& spanlist, size_t byte)
{
    //1.���ڶ�Ӧ��spanlist��Ѱ��span
    Span* it = spanlist.Begin();
    while (it != spanlist.End()) {
        if (it->_freeList != nullptr) { //�ҵ��˾�return
            return it;
        }
        else {
            it = it->_next; //û�ҵ��ͼ�����
        }
    }

   
    //2.��û�У�����Ҫ��pagecache����span
    // �Ȱ����ŵ�����ֹtc�ͷ��ڴ�ʱ����
    spanlist._mtx.unlock();
    PageCache::GetInstance()->_pageMtx.lock();
    Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(byte));
    span->_isUse = true;
    span->_objSize = byte;
    PageCache::GetInstance()->_pageMtx.unlock();
    // ����span�Ĵ���ڴ����ʼ��ַ�ʹ���ڴ�Ĵ�С
    char* start = (char*)(span->_pageId << PAGE_SHIFT);
    size_t bytes = span->_n << PAGE_SHIFT;
    // �г�size��С�Ŀ�
    char* end = start + bytes;
    span->_freeList = start;
    void* tail = start;
    start += byte;
    while (start < end) {
        NextObj(tail) = start;
        tail = NextObj(tail);
        start += byte;
    }
    NextObj(tail) = nullptr;    //�ÿ�
    //���кõ�spanͷ�嵽spanList
    spanlist._mtx.lock();
    spanlist.PushFront(span);
    return span;
}

// byte ÿ������Ĵ�С�ֽ���
void CentralCache::ReleaseListToSpans(void* start, size_t byte)
{
    // �ҵ���Ӧ��Ͱ
    size_t index = SizeClass::Index(byte);

    // ����
    _spanLists[index]._mtx.lock();
    while (start) {
        // ͷ��
        void* next = NextObj(start);
        Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

        NextObj(start) = span->_freeList;
        span->_freeList = start;
        start = next;

        span->_useCount--;  //���¼���
        if (span->_useCount == 0) { //˵���ֳ�ȥ�Ķ��󶼱��ͷ��ˣ���ʱ��span�ͷŸ�pc
            // �����span��spanlists��ɾ�������ո�pc
            _spanLists[index].Erase(span);
            span->_freeList = nullptr;
            span->_next = nullptr;
            span->_prev = nullptr;

            _spanLists[index]._mtx.unlock();
            // ��pc����ǰһ��Ҫ����
            PageCache::GetInstance()->_pageMtx.lock();
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);
            PageCache::GetInstance()->_pageMtx.unlock();
            _spanLists[index]._mtx.lock();
        }
        
        
    }
    _spanLists[index]._mtx.unlock();    //����
}
