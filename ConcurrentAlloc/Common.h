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
static constexpr size_t MAX_BYTES = 256 * 1024;	// ThreadCache�������������ֽ���
static constexpr size_t NFREELIST = 208;	// ��ϣ���������������
static constexpr size_t NPAGES = 129;		// span��������ҳ��
static constexpr size_t PAGE_SHIFT = 13;	// һҳ����λ�������һҳ8KB������13λ

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

// �ڶ��ϰ�ҳ����ռ�
inline static void* SystemAlloc(size_t kpages) {

#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpages << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif
	//Linux�µ�brx��mmap��
#endif
	if (ptr == nullptr) {
		throw std::bad_alloc();
	}
	return ptr;
}

//�ͷſռ�
inline static void SystemFree(void* ptr) {

#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#elif
	//Linux�µ�sbrx��ummap��
#endif

}

class SizeClass; // ����Ҫ����һ�£���ȻPageMap���õ���SizeClass�ᱨ������ֱ�ӽ�PageMap�ŵ����������

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

	// ͷ��
	void Push(void* obj) {
		assert(obj);

		
		// *(void**)obj = _freelist;
		// �Ľ�һ��
		NextObj(obj) = _freeList;
		_freeList = obj;
		_size++;
	}

	// ����һ��������_freelist
	void PushRange(void* start, void* end, size_t n) {
		assert(start);
		assert(end);

		// ͷ��
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}

	// ͷɾ
	void* Pop() {
		assert(_freeList);

		//ͷɾ
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		_size--;

		return obj;
	}

	// ��_freeList�л�ȡһ�η�Χ�Ķ���
	void PopRange(void*& start, void*& end, size_t n) {
		// ��������ͷ��Ϊstart�� β��Ϊend
		assert(n <= _size);

		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1; i++) {
			end = NextObj(end);
		}

		_freeList = NextObj(end);	//��_freeListָ���µ�ͷ��
		NextObj(end) = nullptr;	//�õ�������ĩβ�ÿ�
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
	void* _freeList = nullptr;	//��������
	size_t _maxSize = 1;
	size_t _size = 0;
};

//��������С��ӳ�����
class SizeClass {
	// ������������10%���ҵ�����Ƭ�˷�
	// [1,128]					8byte����	    freelist[0,16)
	// [128+1,1024]				16byte����	    freelist[16,72)
	// [1024+1,8*1024]			128byte����	    freelist[72,128)
	// [8*1024+1,64*1024]		1024byte����     freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte����   freelist[184,208)
public:
	//thread cacheһ����central cache��ȡ���������
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

	// centralcacheһ����pagecache�������ҳ
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

	// bytesΪ����Ŀռ��С
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
			// ����256KB�İ�ҳ����
			return _RoundUp(bytes, 1 << PAGE_SHIFT);
		}
	}

	// align_shift 2��n�η�
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// ����ӳ�����һ����������Ͱ
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// ÿ�������ж��ٸ���
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

// ������ҳΪ��λ�Ĵ���ڴ��
struct Span {
	PAGE_ID _pageId = 0;	//����ڴ����ʼҳ��
	size_t _n = 0;			//ҳ������

	Span* _next = nullptr;	//˫����
	Span* _prev = nullptr;

	size_t _objSize = 0;		//������ڴ��Ĵ�С
	size_t _useCount = 0;		//С�ڴ������tc�ļ���
	void* _freeList = nullptr;	//��������

	bool _isUse = false;	//�Ƿ���ʹ��
};

// ��һ��˫��ѭ������
// ��˫����ɾ����span�ỹ����һ���page cache
// �൱��ֻ�ǰ����span��˫�������Ƴ�
// ��˲���Ҫ��ɾ����span����delete����
// spanҪ����
// ���threadcache���span����������
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

	// ��ɾ����pos����page cache ������delete pos
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
	std::mutex _mtx;	//Ͱ��
};