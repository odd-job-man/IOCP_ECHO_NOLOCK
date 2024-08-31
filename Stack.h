#pragma once
class Stack
{
public:
	void* buf_;
	int iTop_;
	DWORD dwMaxElement_;
	DWORD dwElementSize_;

	BOOL Init(DWORD dwElementNum, DWORD dwElementSize);
	BOOL Push(void** pItem);
	BOOL Pop(void** pOutItem);
	BOOL Clear();

	__forceinline BOOL IsEmpty()
	{
		return (iTop_ == -1);
	}

	__forceinline BOOL IsFull()
	{
		return (iTop_ == dwMaxElement_ - 1);
	}
};
