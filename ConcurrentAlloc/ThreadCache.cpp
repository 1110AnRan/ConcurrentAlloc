#include "ThreadCache.h"
#include "CentralCache.h"

// byte 申请的对象大小
void* ThreadCache::Allocate(size_t byte) {
	assert(byte <= MAX_BYTES);

	// aliginSize 对齐数
	// index第几个桶
	size_t aliginSize = SizeClass::RoundUp(byte);
	size_t index = SizeClass::Index(byte);
	if (!_freeLists[index].Empty()) {
		return _freeLists[index].Pop();
	}
	else {
		return FetchFromCentralCache(index, aliginSize);
	}
}

// byte 对象大小的字节数
// ptr 起始地址
void ThreadCache::Deallocate(void* ptr, size_t byte)
{
	assert(ptr);
	assert(byte <= MAX_BYTES);

	// 找到对应的哈希桶
	size_t index = SizeClass::Index(byte);
	_freeLists[index].Push(ptr);

	// 对象过长， 将其释放回cc
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize()) {
		ListTooLong(_freeLists[index], byte);
	}
}

// index 哈希桶对应的下表
// byte 对象的大小
void* ThreadCache::FetchFromCentralCache(size_t index, size_t byte)
{
	// 慢开始反馈调节
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

// byte 对象大小的字节数
void ThreadCache::ListTooLong(FreeList& list, size_t byte)
{
	void* start = nullptr;
	void* end = nullptr;
	// 一次性取出剩余的所有对象
	list.PopRange(start, end, list.MaxSize());

	CentralCache::GetInstance()->ReleaseListToSpans(start, byte);
}
