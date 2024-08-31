#pragma once
#include <type_traits>
#include <initializer_list>
#include <new>
#include <cassert>
#include <stddef.h>
#include "FreeListNode.h"
#pragma warning(disable : 4101)
#pragma warning(disable : 4984)
#define PROFILE

template <typename T>
class FreeList
{
public:
	FreeList(bool constr_dest_on_alloc_free = true, int capacity = 0)
		:bPlacementNew{ constr_dest_on_alloc_free }, head_{}
#ifdef _DEBUG
	,capacity_{ 0 }, size_{ 0 }
#endif
	{
		InitializeCriticalSection(&cs_);
		EnterCriticalSection(&cs_);
		top_ = &head_;
		for (int i = 0; i < capacity; ++i)
		{
			Push(GetNodeFromHeap());
		}
		LeaveCriticalSection(&cs_);
	}
#ifdef _DEBUG
	// 프리리스트의 노드가 최대엿을때의 갯수, 결국 소멸자 호출시점에서 capacity_ != size_이면 메모리 누수가 발생한것
	int capacity_;
	// 현재 가지고잇는 프리리스트의 갯수
	int size_;
#ifdef DETECT_MEMORY_LEAK
	// 소멸자에서만 호출되어야 하는 함수
	__forceinline bool isMemoryLeak()
	{
		return capacity_ != size_;
	}
#endif
#endif

	void ReleaseAll()
	{
		EnterCriticalSection(&cs_);
		if (IsEmpty())
			return;
		while (top_->pNext)
		{
			FreeListNode<T>* temp = top_;
			top_ = top_->pNext;
			delete temp;
		}
		LeaveCriticalSection(&cs_);
	}

	~FreeList()
	{
		ReleaseAll();
		DeleteCriticalSection(&cs_);
	}
	
	// constr_dest_on_alloc_free_가 false인데 호출하면 프로그램 크래시함
	template<typename V>
	__forceinline FreeListNode<T>* GetNodeFromHeap(const V& other)
	{
#ifdef _DEBUG
		++capacity_;
#endif
		return new FreeListNode<T>{ other };
	}
// 타입상관없음.
	FreeListNode<T>* GetNodeFromHeap()
	{
#ifdef _DEBUG
		++capacity_;
#endif
		return new FreeListNode<T>;
	}

	template<typename U = T, typename V, typename = std::enable_if_t<std::is_class_v<U>>>
	T* Alloc(const V& other)
	{
		EnterCriticalSection(&cs_);
		T* pData_to_user;
		if (IsEmpty())
			pData_to_user = &GetNodeFromHeap(other)->data;
		else
		{
			if(bPlacementNew)
				new (pData_to_user) T(other);
		}
		LeaveCriticalSection(&cs_);
		return pData_to_user;
	}

	T* Alloc(void)
	{
		EnterCriticalSection(&cs_);
		T* pData_to_user;
		if (IsEmpty())
		{
			pData_to_user = &GetNodeFromHeap()->data;
		}
		else
		{
			pData_to_user = &top_->data;
			top_ = top_->pNext;
			if (bPlacementNew)
				new (pData_to_user) T{};
		}
#ifdef _DEBUG
			--size_;
			if (size_ < 0) size_ = 0;
#endif
		LeaveCriticalSection(&cs_);
		return pData_to_user;
	}

	__forceinline bool IsEmpty()
	{
		return top_ == &head_;
	}
	__forceinline void Push(FreeListNode<T>* pData)
	{
		FreeListNode<T>* next_node = top_;// 현재 탑 위치 저장
		top_ = pData;
		pData->pNext = next_node;
#ifdef _DEBUG
		++size_;
		if(size_ > capacity_)
			capacity_ = size_;
#endif 
	}
	__forceinline FreeListNode<T>* Top()
	{
		if(IsEmpty())
			return nullptr;

		return top_;
	}
	void Free(T* p)
	{
		EnterCriticalSection(&cs_);
		if (bPlacementNew)
			p->~T();
		int offset = offsetof(FreeListNode<T>, data);
		FreeListNode<T>* node = reinterpret_cast<FreeListNode<T>*>(reinterpret_cast<char*>(p) - offset);
		Push(node);
		LeaveCriticalSection(&cs_);
	}
	__forceinline void Pop()
	{
		if (IsEmpty())
			return;

		FreeListNode<T>* pop_target = top_;
		top_ = top_->pNext;
#ifdef _DEBUG
		--size_;
		if (size_ < 0) size_ = 0;
#endif
	}
	FreeListNode<T>* top_;
	FreeListNode<T> head_;
	CRITICAL_SECTION cs_;
	bool bPlacementNew;
};



