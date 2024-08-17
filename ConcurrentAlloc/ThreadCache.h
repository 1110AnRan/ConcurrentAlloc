#pragma once

#include "Common.h"

class ThreadCache {
public:
	//�����СΪbyte�Ķ���
	void* Allocate(size_t byte);
	
	// �ͷŵ�ַ��ptr��ʼ�Ĵ�СΪbyte�Ķ���
	void Deallocate(void* ptr, size_t byte);

	// �����Ļ����ȡ����
	void* FetchFromCentralCache(size_t index, size_t byte);

	// �����ڴ���������仹��cc
	void ListTooLong(FreeList& list, size_t byte);
private:
	FreeList _freeLists[NFREELIST];
};

//TLS(Thread Loacl Storage)
static __declspec(thread) ThreadCache* pTLSThreadCache = nullptr;