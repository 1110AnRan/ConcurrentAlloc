#include "ThreadCache.h"
#include "CentralCache.h"

// byte ����Ķ����С
void* ThreadCache::Allocate(size_t byte) {
	assert(byte <= MAX_BYTES);

	// aliginSize ������
	// index�ڼ���Ͱ
	size_t aliginSize = SizeClass::RoundUp(byte);
	size_t index = SizeClass::Index(byte);
	if (!_freeLists[index].Empty()) {
		return _freeLists[index].Pop();
	}
	else {
		return FetchFromCentralCache(index, aliginSize);
	}
}

// byte �����С���ֽ���
// ptr ��ʼ��ַ
void ThreadCache::Deallocate(void* ptr, size_t byte)
{
	assert(ptr);
	assert(byte <= MAX_BYTES);

	// �ҵ���Ӧ�Ĺ�ϣͰ
	size_t index = SizeClass::Index(byte);
	_freeLists[index].Push(ptr);

	// ��������� �����ͷŻ�cc
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize()) {
		ListTooLong(_freeLists[index], byte);
	}
}

// index ��ϣͰ��Ӧ���±�
// byte ����Ĵ�С
void* ThreadCache::FetchFromCentralCache(size_t index, size_t byte)
{
	// ����ʼ��������
#ifdef _WIN32
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(byte));
#elif
	size_t batchNum = std::min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(byte));
#endif // _WIN32

	
	if (batchNum == _freeLists[index].MaxSize()) {
		_freeLists[index].MaxSize() += 1;
	}


	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, byte);
	assert(actualNum >= 1);
	if (actualNum == 1) {
		assert(start == end);
		return start;
	}
	else {
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}

// byte �����С���ֽ���
void ThreadCache::ListTooLong(FreeList& list, size_t byte)
{
	void* start = nullptr;
	void* end = nullptr;
	// һ����ȡ��ʣ������ж���
	list.PopRange(start, end, list.MaxSize());

	CentralCache::GetInstance()->ReleaseListToSpans(start, byte);
}
