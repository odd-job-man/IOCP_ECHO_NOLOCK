#include <windows.h>
#include "Stack.h"

BOOL Stack::Init(DWORD dwElementNum, DWORD dwElementSize)
{
	buf_ = (void**)malloc(dwElementSize * dwElementNum);
	iTop_ = -1;
	dwMaxElement_ = dwElementNum;
	dwElementSize_ = dwElementSize;
	return TRUE;
}

BOOL Stack::Push(void** pItem)
{
	if (IsFull())	
		return FALSE;

	++iTop_;
	memcpy((BYTE*)buf_ + dwElementSize_ * iTop_, (const void*)pItem, dwElementSize_);
	return TRUE;
}

BOOL Stack::Pop(void** pOutItem)
{
	if (IsEmpty())
		return FALSE;

	memcpy(pOutItem, (BYTE*)buf_ + dwElementSize_ * iTop_, dwElementSize_);
	--iTop_;
	return TRUE;
}

BOOL Stack::Clear()
{
	free(buf_);
	return FALSE;
}

