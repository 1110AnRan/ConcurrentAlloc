#include"PageCache.h"

PageCache PageCache::_sInst;

//��ȡһ��kҳ��span
Span* PageCache::NewSpan(size_t k)
{
	// k��[1, 128]
	// assert(k > 0 && k < NPAGES);
	
	// k��[1, ��]
	assert(k > 0);
	if (k > NPAGES - 1) {	//����128ҳֱ���Ҷ�����
		void* ptr = SystemAlloc(k);
		// �滻
		// Span* span = new Span;
		Span* span = PageCache::_spanPool._new();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		//����ҳ����span֮���ӳ��
		// _idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);
		return span;
	}
	// С�ڵ���128ҳ
	// �����ǰ��Ͱ��span������һ��span
	if (!_spanLists[k].Empty()) {
		Span* kSpan = _spanLists[k].PopFront();

		// ����ҳ����span��ӳ�䣬����cc����tc�ͷŵ��ڴ�
		for (PAGE_ID i = 0; i < kSpan->_n; i++) {
			// _idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}
		return kSpan;
	}

	// ��k��Ͱ������һ���ǿյ�span
	for (size_t i = k + 1; i < NPAGES; i++) {
		if (!_spanLists[i].Empty()) {
			Span* nSpan = _spanLists[i].PopFront();
			// �滻
			// Span* kSpan = new Span;
			Span* kSpan = PageCache::_spanPool._new();
			// ��nSpan��ͷ��kҳ������
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);
			//�洢nSpan��βҳ����span��Ӧ�Ĺ�ϵ������pc����
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);
			// ����ҳ����span��ӳ�䣬����cc����tc�ͷŵ��ڴ�
			for (PAGE_ID i = 0; i < kSpan->_n; i++) {
				// _idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}
			return kSpan;
		}
	}
	//����˵��Ͱ����û�б�kҳ�����Ͱ�ˣ�ֱ����OS����һ��128ҳ��span

	// Span* BigSpan = new Span;
	Span* bigSpan = PageCache::_spanPool._new();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// ҳ����span�Ķ�Ӧ��ϵ
Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;

	// std::unique_lock<std::mutex> lock(_pageMtx);	 //����ʱ����������ʱ�Զ�����
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

// �ͷſ��е�span��pc��ͬʱ���Խ���ǰ��ϲ�
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	if (span->_n > NPAGES - 1) {
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);

		SystemFree(ptr);
		// delete span;
		PageCache::_spanPool._delete(span);
		return;
	}
	// ����ǰ�ϲ�
	while (1) {
		// auto ret = _idSpanMap.find(span->_pageId - 1);
		Span* ret = (Span*)_idSpanMap.get(span->_pageId - 1);
		if (ret == nullptr) {
			break;
		}
		Span* prevSpan = ret;
		// ���ǰһ��span���ڱ�ʹ��
		if (prevSpan->_isUse) {
			break;
		}

		// ���ǰһ��span�����ҳ�����ϵ�ǰspan��ҳ������128
		if (prevSpan->_n + span->_n > NPAGES - 1) {
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);	//�ڶ�Ӧ�Ĺ�ϣͰ�н���ɾ��

		// delete prevSpan;
		PageCache::_spanPool._delete(prevSpan);
	}
	// ���к�ϲ�
	while (1) {
		// auto ret = _idSpanMap.find(span->_pageId + span->_n);
		Span* ret = (Span*)_idSpanMap.get(span->_pageId + span->_n);
		if (ret == nullptr) {
			break;
		}

		Span* nextSpan = ret;
		// ������ڱ�ʹ��
		if (nextSpan->_isUse) {
			break;
		}
		// ����ϲ����ҳ������128
		if (nextSpan->_n + span->_n > NPAGES - 1) {
			break;
		}

		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);

		// delete nextSpan;
		PageCache::_spanPool._delete(nextSpan);
	}
	// ���뵽��Ӧ��Ͱ��
	_spanLists[span->_n].PushFront(span);
	// ������Ӧ����βҳӳ��
	// _idSpanMap[span->_pageId] = span;
	// _idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
	// ����span����Ϊδ��ʹ�õ�״̬
	span->_isUse = false;
}
