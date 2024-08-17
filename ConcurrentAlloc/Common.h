#pragma once

#include<iostream>
#include<vector>
#include<thread>
#include<mutex>
#include<cassert>
#include<unordered_map>

using std::vector;
using std::cout;
using std::endl;

// 8KB
static constexpr size_t MAX_BYTES = 256 * 1024;	// ThreadCache单次申请的最大字节数
static constexpr size_t NFREELIST = 208;	// 哈希表中自由链表个数
static constexpr size_t NPAGES = 129;		// span的最大管理页数
static constexpr size_t PAGE_SHIFT = 13;	// 一页多少位，这里给一页8KB，就是13位

#ifdef _WIN64
typedef unsigned long long PAGE_ID;
#elif _WIN32
typedef size_t PAGE_ID;
#else
	//Linux
#endif

#ifdef _WIN32
#include<Windows.h>
#else
	//Linux
#endif

// 在堆上按页申请空间
inline static void* SystemAlloc(size_t kpages) {

#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpages << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif
	//Linux下的brx或mmap等
#endif
	if (ptr == nullptr) {
		throw std::bad_alloc();
	}
	return ptr;
}

//释放空间
inline static void SystemFree(void* ptr) {

#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#elif
	//Linux下的sbrx或ummap等
#endif

}

class SizeClass; // 这里要声明一下，不然PageMap中用到了SizeClass会报错，或者直接将PageMap放到最后面引用

static void*& NextObj(void* obj) {
	return *(void**)obj;
}

#include"ObjectPool.h"
#include"PageMap.h"

class FreeList {
public:
	void PushRange(void* start, void* end) {
		assert(start);
		assert(end);

		NextObj(end) = _freeList;
		_freeList = start;
	}

	// 头插
	void Push(void* obj) {
		assert(obj);

		
		// *(void**)obj = _freelist;
		// 改进一下
		NextObj(obj) = _freeList;
		_freeList = obj;
		_size++;
	}

	// 插入一段区间至_freelist
	void PushRange(void* start, void* end, size_t n) {
		assert(start);
		assert(end);

		// 头插
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}

	// 头删
	void* Pop() {
		assert(_freeList);

		//头删
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		_size--;

		return obj;
	}

	// 从_freeList中获取一段范围的对象
	void PopRange(void*& start, void*& end, size_t n) {
		// 这段区间的头部为start， 尾部为end
		assert(n <= _size);

		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1; i++) {
			end = NextObj(end);
		}

		_freeList = NextObj(end);	//将_freeList指向新的头部
		NextObj(end) = nullptr;	//得到的区间末尾置空
		_size -= n;

	}

	bool Empty() {
		return _freeList == nullptr;
	}

	size_t& MaxSize() {
		return _maxSize;
	}

	size_t Size() {
		return _size;
	}
private:
	void* _freeList = nullptr;	//自由链表
	size_t _maxSize = 1;
	size_t _size = 0;
};

//计算对象大小的映射规则
class SizeClass {
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128]					8byte对齐	    freelist[0,16)
	// [128+1,1024]				16byte对齐	    freelist[16,72)
	// [1024+1,8*1024]			128byte对齐	    freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐     freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte对齐   freelist[184,208)
public:
	//thread cache一次向central cache获取对象的上限
	static size_t NumMoveSize(size_t size) {
		assert(size > 0);

		int num = MAX_BYTES / size;
		if (num < 2) {
			num = 2;
		}
		if (num > 512) {
			num = 512;
		}
		return num;
	}

	// centralcache一次向pagecache申请多少页
	static size_t NumMovePage(size_t size) {
		size_t num = NumMoveSize(size);
		size_t nPage = num * size;

		nPage >>= PAGE_SHIFT;
		if (nPage == 0) {
			nPage = 1;
		}
		return nPage;
	}

	static inline size_t _RoundUp(size_t bytes, size_t alignNum) {
		return ((bytes + alignNum - 1) & ~(alignNum - 1));
	}

	// bytes为申请的空间大小
	static inline size_t RoundUp(size_t bytes) {
		if (bytes <= 128)
		{
			return _RoundUp(bytes, 8);
		}
		else if (bytes <= 1024)
		{
			return _RoundUp(bytes, 16);
		}
		else if (bytes <= 8 * 1024)
		{
			return _RoundUp(bytes, 128);
		}
		else if (bytes <= 64 * 1024)
		{
			return _RoundUp(bytes, 1024);
		}
		else if (bytes <= 256 * 1024)
		{
			return _RoundUp(bytes, 8 * 1024);
		}
		else
		{
			// 大于256KB的按页对齐
			return _RoundUp(bytes, 1 << PAGE_SHIFT);
		}
	}

	// align_shift 2的n次方
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
			return -1;
		}
	}
};

// 管理以页为单位的大块内存块
struct Span {
	PAGE_ID _pageId = 0;	//大块内存的起始页号
	size_t _n = 0;			//页的数量

	Span* _next = nullptr;	//双链表
	Span* _prev = nullptr;

	size_t _objSize = 0;		//管理的内存块的大小
	size_t _useCount = 0;		//小内存块分配给tc的计数
	void* _freeList = nullptr;	//自由链表

	bool _isUse = false;	//是否在使用
};

// 是一个双向循环链表
// 从双链表删除的span会还给下一层的page cache
// 相当于只是把这个span从双链表中移除
// 因此不需要对删除的span进行delete操作
// span要上锁
// 多个threadcache会对span产生锁竞争
class SpanList {
public:
	SpanList() {
		_head = new Span;
		//_head = SpanList::_spanPool._new();
		_head->_next = _head;
		_head->_prev = _head;
	}

	bool Empty() {
		return _head == _head->_next;
	}

	Span* PopFront() {
		Span* ret = _head->_next;
		Erase(ret);
		return ret;
	}
	
	void PushFront(Span* span) {
		Insert(Begin(), span);
	}

	void Insert(Span* pos, Span* newSpan) {
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;

		prev->_next = newSpan;
		newSpan->_prev = prev;
		
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	// 将删除的pos还给page cache 而不是delete pos
	void Erase(Span* pos) {
		assert(pos);

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	Span* Begin() {
		return _head->_next;
	}
	Span* End() {
		return _head;
	}
	
private:
	Span* _head = nullptr;
	//static ObjectPool<Span> _spanPool;
public:
	std::mutex _mtx;	//桶锁
};