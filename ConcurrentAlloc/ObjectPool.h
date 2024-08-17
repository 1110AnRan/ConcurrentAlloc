#pragma once

#include "Common.h"

template<class T>
class ObjectPool {
public:
	T* _new() {
		T* obj = nullptr;

		// 优先使用被释放的空间
		if (_freeList) {
			void* next = *(void**)_freeList;
			obj = (T*)_freeList;
			_freeList = next;
		}
		else {
			if (_remainBytes < sizeof T) {
				_remainBytes = 1024 * 128;
				_memory = (char*)SystemAlloc(_remainBytes >> 13);
				if (_memory == nullptr) {
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;
			size_t ObjectSize = sizeof(T) > sizeof(void*) ? sizeof(T) : sizeof(void*);
			_memory += ObjectSize;
			_remainBytes -= ObjectSize;
		}

		//使用定位new调用T的构造函数初始化
		new(obj)T;

		return obj;
	}

	void _delete(T* obj) {

		//调用T的析构函数
		obj->~T();

		// _freelist是void*，任意类型的指针可以赋值给void*
		*(void**)obj = _freeList;
		_freeList = obj;
	}
private:
	char* _memory = nullptr;	//指向申请的内存空间
	size_t _remainBytes = 0;	//剩余空间的字节数
	void* _freeList = nullptr;	//指向被释放的内存空间
};

//struct TreeNode
//{
//	int _val;
//	TreeNode* _left;
//	TreeNode* _right;
//	TreeNode()
//		:_val(0)
//		, _left(nullptr)
//		, _right(nullptr)
//	{}
//};
//
//void TestObjectPool()
//{
//	// 申请释放的轮次
//	const size_t Rounds = 3;
//	// 每轮申请释放多少次
//	const size_t N = 100000;
//	std::vector<TreeNode*> v1;
//	v1.reserve(N);
//
//	//malloc和free
//	size_t begin1 = clock();
//	for (size_t j = 0; j < Rounds; ++j)
//	{
//		for (int i = 0; i < N; ++i)
//		{
//			v1.push_back(new TreeNode);
//		}
//		for (int i = 0; i < N; ++i)
//		{
//			delete v1[i];
//		}
//		v1.clear();
//	}
//	size_t end1 = clock();
//
//	//定长内存池
//	ObjectPool<TreeNode> TNPool;
//	std::vector<TreeNode*> v2;
//	v2.reserve(N);
//	size_t begin2 = clock();
//	for (size_t j = 0; j < Rounds; ++j)
//	{
//		for (int i = 0; i < N; ++i)
//		{
//			v2.push_back(TNPool._new());
//		}
//		for (int i = 0; i < N; ++i)
//		{
//			TNPool._delete(v2[i]);
//		}
//		v2.clear();
//	}
//	size_t end2 = clock();
//
//	cout << "new cost time:" << end1 - begin1 << endl;
//	cout << "object pool cost time:" << end2 - begin2 << endl;
//}