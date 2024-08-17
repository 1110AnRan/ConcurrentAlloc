#pragma once

#include "Common.h"

// Single-level radix tree
template<int BITS>
class TCMalloc_PageMap1 {
private:
	void** array_;								//�洢ӳ���ϵ������
	static constexpr int LENGTH = 1 << BITS;		//ҳ����Ŀ
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap1() {
		size_t size = sizeof(void*) << BITS;							//��Ҫ��������Ĵ�С
		size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT);	//��ҳ����
		array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);			//�������ռ�
		memset(array_, 0, size);										//�������
	}
	void* get(Number k) const {
		if ((k >> BITS) > 0) {	//k����[0, 2^BITS - 1]
			return nullptr;
		}
		return array_[k];
	}

	void set(Number k, void* v) {
		assert((k >> BITS) == 0);	//k�ķ�Χ������[0, 2^BITS - 1]
		array_[k] = v;				//����ӳ���ϵ
	}

};


// Two-level radix tree
template<int BITS>
class TCMalloc_PageMap2 {
private:
	static constexpr int ROOT_BITS = 5;					//��һ���Ӧǰ5������λ
	static constexpr int ROOT_LENGTH = 1 << ROOT_BITS;	//��һ��Ԫ�صĸ���
	static constexpr int LEAF_BITS = BITS - ROOT_BITS;	//�ڶ����Ӧ����ı���λ
	static constexpr int LEAF_LENGTH = 1 << LEAF_BITS;	//�ڶ���Ԫ�صĸ���
	//��һ�������д洢��Ԫ������
	struct Leaf {
		void* values[LEAF_LENGTH];	//�ڶ����Ԫ������
	};
	Leaf* root_[ROOT_LENGTH];	//��һ������
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap2() {
		memset(root_, 0, sizeof(root_));	//�����һ��
		PreallocateMoreMemory();
	}
	void PreallocateMoreMemory() {
		Ensure(0, 1 << BITS);
	}

	//����[start, start + n - 1] �ռ�
	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> LEAF_BITS;
			if (i1 >= ROOT_LENGTH) {	//���ڷ�Χ��
				return false;
			}
			if (root_[i1] == nullptr) {
				//���ٿռ�
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
		Number i1 = k >> LEAF_BITS;			//��һ���Ӧ���±�
		Number i2 = k & (LEAF_BITS - 1);	//�ڶ����Ӧ���±�
		if ((k >> BITS) > 0 || root_[i1] == nullptr) {	//ҳ�Ų��ڷ�Χ�ڻ���û�н�����Ӧ��ӳ��
			return nullptr;
		}
		return root_[i1]->values[i2];		//���ض�Ӧ��ӳ��
	}
	void set(Number k, void* v){
		Number i1 = k >> LEAF_BITS;			//��һ���Ӧ���±�
		Number i2 = k & (LEAF_BITS - 1);	//�ڶ����Ӧ���±�
		assert(i1 < ROOT_LENGTH);
		root_[i1]->values[i2] = v;			//������ҳ�����Ӧspan��ӳ��
	}

};

//���������
template<int BITS>
class TCMalloc_PageMap3 {
private:
	static constexpr int INTERIOR_BITS = (BITS + 2) / 3;			//��һ�������λ��
	static constexpr int INTERIOR_LENGTH = 1 << INTERIOR_BITS;		//��һ������ĳ���
	static constexpr int LEAF_BITS = BITS - 2 * INTERIOR_BITS;		//�������λ��
	static constexpr int LEAF_LENGTH = 1 << LEAF_BITS;				//������ĳ���
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
			const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);			//��һ��
			const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);	//�ڶ���
			if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH) {
				return false;
			}
			if (root_->ptrs[i1] == nullptr) {	//��һ��δ����
				// ���ٿռ�
				Node* n = Newnode();
				if (n == nullptr) {
					return false;
				}
				root_->ptrs[i1] = n;
			}
			if (root_->ptrs[i1]->ptrs[i2] == nullptr) {	//�ڶ���δ����
				// ���ٿռ�
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
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);			//��һ��
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);	//�ڶ���
		const Number i3 = k & (LEAF_LENGTH - 1);					//������
		Ensure(k, 1);												//ȷ����Ӧ�Ŀռ��ǿ��ٺ��˵�
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
	}

	void* get(Number k) const {
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);			//��һ��
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);	//�ڶ���
		const Number i3 = k & (LEAF_LENGTH - 1);					//������
		if ((k >> BITS) > 0 || root_->ptrs[i1] == nullptr || root_->ptrs[i1]->ptrs[i2] == nullptr) {
			return nullptr;
		}
		return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3];	//���ض�Ӧ��spanָ��
	}

	void PreallocateMoreMemory() {

	}
};