#pragma once
template<typename T>
class FreeListNode
{
public:
#ifdef _DEBUG
	__int64 pre_header;
#endif
	FreeListNode* pNext;
	T data;
#ifdef _DEBUG
	__int64 post_header;
#endif
	FreeListNode()
		: data{}, pNext{ nullptr }
#ifdef _DEBUG
		, pre_header{ _int64() }, post_header{ _int64() }
#endif 
	{}

	FreeListNode(const T& init_list)
		: data{init_list }, pNext{ nullptr }
#ifdef _DEBUG
		, pre_header{ _int64() }, post_header{ _int64() }
#endif 
	{}
};


