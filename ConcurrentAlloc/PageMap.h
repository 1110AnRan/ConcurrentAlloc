#pragma once

#include "Common.h"

// Single-level radix tree
template<int BITS>
class TCMalloc_PageMap1 {
private:
	void** array_;								//存储映射关系的数组
	static constexpr int LENGTH = 1 << BITS;		//页的数目
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap1() {
		size_t size = sizeof(void*) << BITS;							//需要开辟数组的大小
		size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT);	//按页对齐
		array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);			//向堆申请空间
		memset(array_, 0, size);										//清空数组
	}
	void* get(Number k) const {
		if ((k >> BITS) > 0) {	//k不在[0, 2^BITS - 1]
			return nullptr;
		}
		return array_[k];
	}

	void set(Number k, void* v) {
		assert((k >> BITS) == 0);	//k的范围必须在[0, 2^BITS - 1]
		array_[k] = v;				//建立映射关系
	}

};


// Two-level radix tree
template<int BITS>
class TCMalloc_PageMap2 {
private:
	static constexpr int ROOT_BITS = 5;					//第一层对应前5个比特位
	static constexpr int ROOT_LENGTH = 1 << ROOT_BITS;	//第一层元素的个数
	static constexpr int LEAF_BITS = BITS - ROOT_BITS;	//第二层对应其余的比特位
	static constexpr int LEAF_LENGTH = 1 << LEAF_BITS;	//第二层元素的个数
	//第一层数组中存储的元素类型
	struct Leaf {
		void* values[LEAF_LENGTH];	//第二层的元素类型
	};
	Leaf* root_[ROOT_LENGTH];	//第一层数组
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap2() {
		memset(root_, 0, sizeof(root_));	//清理第一层
		PreallocateMoreMemory();
	}
	void PreallocateMoreMemory() {
		Ensure(0, 1 << BITS);
	}

	//开辟[start, start + n - 1] 空间
	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> LEAF_BITS;
			if (i1 >= ROOT_LENGTH) {	//不在范围内
				return false;
			}
			if (root_[i1] == nullptr) {
				//开辟空间
				static ObjectPool<Leaf> leafPool;
				Leaf* leaf = (Leaf*)leafPool._new;
				memset(leaf, 0, sizeof(*leaf));
				root_[i1] = leaf;
			}
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}
	void* get(Number k) const {
		Number i1 = k >> LEAF_BITS;			//第一层对应的下标
		Number i2 = k & (LEAF_BITS - 1);	//第二层对应的下标
		if ((k >> BITS) > 0 || root_[i1] == nullptr) {	//页号不在范围内或者没有建立相应的映射
			return nullptr;
		}
		return root_[i1]->values[i2];		//返回对应的映射
	}
	void set(Number k, void* v){
		Number i1 = k >> LEAF_BITS;			//第一层对应的下标
		Number i2 = k & (LEAF_BITS - 1);	//第二层对应的下标
		assert(i1 < ROOT_LENGTH);
		root_[i1]->values[i2] = v;			//建立该页号与对应span的映射
	}

};

//三层基数树
template<int BITS>
class TCMalloc_PageMap3 {
private:
	static constexpr int INTERIOR_BITS = (BITS + 2) / 3;			//第一。二层的位数
	static constexpr int INTERIOR_LENGTH = 1 << INTERIOR_BITS;		//第一。二层的长度
	static constexpr int LEAF_BITS = BITS - 2 * INTERIOR_BITS;		//第三层的位数
	static constexpr int LEAF_LENGTH = 1 << LEAF_BITS;				//第三层的长度
	struct Node {
		Node* ptrs[INTERIOR_LENGTH];
	};

	struct Leaf {
		void* values[LEAF_LENGTH];
	};
	Node* Newnode() {
		static ObjectPool<Node> nodePool;
		Node* result = nodePool._new();
		if (result != nullptr) {
			memset(result, 0, sizeof(*result));
		}
		return result;
	}
	Node* root_;
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap3() {
		root_ = Newnode();
	}

	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);			//第一层
			const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);	//第二层
			if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH) {
				return false;
			}
			if (root_->ptrs[i1] == nullptr) {	//第一层未开辟
				// 开辟空间
				Node* n = Newnode();
				if (n == nullptr) {
					return false;
				}
				root_->ptrs[i1] = n;
			}
			if (root_->ptrs[i1]->ptrs[i2] == nullptr) {	//第二层未开劈
				// 开辟空间
				static ObjectPool<Leaf> leafPool;
				Leaf* leaf = leafPool._new();
				if (leaf == nullptr) {
					return false;
				}
				memset(leaf, 0, sizeof(*leaf));
				root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);
			}
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
	}

	void set(Number k, void* v) {
		assert((k >> BITS) > 0);
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);			//第一层
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);	//第二层
		const Number i3 = k & (LEAF_LENGTH - 1);					//第三层
		Ensure(k, 1);												//确保对应的空间是开辟好了的
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
	}

	void* get(Number k) const {
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);			//第一层
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);	//第二层
		const Number i3 = k & (LEAF_LENGTH - 1);					//第三层
		if ((k >> BITS) > 0 || root_->ptrs[i1] == nullptr || root_->ptrs[i1]->ptrs[i2] == nullptr) {
			return nullptr;
		}
		return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3];	//返回对应的span指针
	}

	void PreallocateMoreMemory() {

	}
};