#pragma once
#include "Common.h"

// ����ȫ��ֻ��һ��CentralCache
// ����ʹ�õ���ģʽ
class CentralCache {
public:

	//�����ӿ�
	static CentralCache* GetInstance() {
		return &_sInst;
	}

	//��central cache��ȡһ������batchNum�Ķ����thread cache
	// 
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t byte);


	// ���һ��span
	Span* GetOneSpan(SpanList& spanlist, size_t byte);

	// ��tc�п��еĳ������ͷŻظ�cc
	void ReleaseListToSpans(void* start, size_t byte);

private:
	SpanList _spanLists[NFREELIST];
private:
	CentralCache(){}

	CentralCache(const CentralCache&) = delete;	//������

	static CentralCache _sInst;
};