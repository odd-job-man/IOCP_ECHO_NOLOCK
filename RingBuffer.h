#pragma once

constexpr int BUFFER_SIZE = 10000;
constexpr int ACTUAL_SIZE = BUFFER_SIZE + 1;

class RingBuffer
{
public:
	RingBuffer(void);
	~RingBuffer(void);
	int GetUseSize(void);
	int GetFreeSize(void);
	int Enqueue(const char* pSource, int iSizeToPut);
	int Dequeue(char* pOutDest, int iSizeToOut);
	int Peek(char* pOutTarget, int iSizeToPeek);
	void ClearBuffer(void);
	int DirectEnqueueSize(void);
	int DirectDequeueSize(void);
	int MoveInPos(int iSizeToMove);
	int MoveOutPos(int iSizeToMove);
	char* GetWriteStartPtr(void);
	char* GetReadStartPtr(void);
	int iOutPos_;
	int iInPos_;
	char Buffer_[ACTUAL_SIZE];
};
