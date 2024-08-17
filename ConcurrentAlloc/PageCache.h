#pragma once
#include"Common.h"

//����ģʽ
class PageCache {
public:
	static PageCache* GetInstance() {
		return &_sInst;
	}

	// ��os����k����С��ҳ
	Span* NewSpan(size_t k);

	// ��ȡ�Ӷ���span��ӳ��
	Span* MapObjectToSpan(void* obj);

	// ��cc�п��е�span����
	void ReleaseSpanToPageCache(Span* span);
private:
	// ʹ�ö����ڴ���滻new��delete
	ObjectPool<Span> _spanPool;
	SpanList _spanLists[NPAGES];	//�±�i�洢����i��ҳ��span
	// std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;
public:
	std::mutex _pageMtx;	//����
private:
	PageCache() {}
	PageCache(const PageCache&) = delete;

	static PageCache _sInst;
};