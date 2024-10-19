#pragma once

#include <memory.h>
#include "CLockFreeQueue.h"
#include "CLockFreeStack.h"
#include "RingBuffer.h"
#include "Session.h"
#include "IHandler.h"
#include "LanServer.h"

#define QUEUE
#include "CTlsObjectPool.h"

using NET_HEADER = short;

enum ServerType
{
	Net,
	Lan
};

class Packet
{
public:
	struct LanHeader
	{
		short payloadLen_;
	};

	struct NetHeader
	{
		unsigned char code_;
		unsigned short payloadLen_;
		unsigned char randomKey_;
		unsigned char checkSum_;
	};

	static constexpr int RINGBUFFER_SIZE = 10000;
	static constexpr int BUFFER_SIZE = (RINGBUFFER_SIZE / 8) + sizeof(NetHeader);

	template<ServerType type>
	void Clear(void)
	{
		if constexpr (type == Net)
		{
			front_ = rear_ = sizeof(NetHeader);
		}
		else
		{
			front_ = rear_ = sizeof(LanHeader);
		}
	}

	int GetData(char* pDest, int sizeToGet)
	{
		if (rear_ - front_ < sizeToGet)
		{
			return 0;
		}
		else
		{
			memcpy_s(pDest, sizeToGet, pBuffer_ + front_, sizeToGet);
			front_ += sizeToGet;
			return sizeToGet;
		}
	}

	int PutData(char* pSrc, int sizeToPut)
	{
		memcpy_s(pBuffer_ + rear_, sizeToPut, pSrc, sizeToPut);
		rear_ += sizeToPut;
		return sizeToPut;
	}

	int GetUsedDataSize(void)
	{
		return rear_ - front_;
	}

	char* GetBufferPtr(void)
	{
		return pBuffer_ + sizeof(LanHeader);
	}

	int MoveWritePos(int sizeToWrite)
	{
		rear_ += sizeToWrite;
		return sizeToWrite;
	}

	int MoveReadPos(int sizeToRead)
	{
		front_ += sizeToRead;
		return sizeToRead;
	}


	Packet& operator <<(const unsigned char value)
	{
		*(unsigned char*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(unsigned char& value)
	{
		value = *(unsigned char*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const char value)
	{
		*(char*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(char& value)
	{
		value = *(char*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const short value)
	{
		*(short*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(short& value)
	{
		value = *(short*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const unsigned short value)
	{
		*(unsigned short*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(unsigned short& value)
	{
		value = *(unsigned short*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const int value)
	{
		*(int*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(int& value)
	{
		value = *(int*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const unsigned int value)
	{
		*(unsigned int*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(unsigned int& value)
	{
		value = *(unsigned int*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const long value)
	{
		*(long*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}

	Packet& operator >>(long& value)
	{
		value = *(long*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const unsigned long value)
	{
		*(unsigned long*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(unsigned long& value)
	{
		value = *(unsigned long*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const __int64 value)
	{
		*(__int64*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}

	Packet& operator >>(__int64& value)
	{
		value = *(__int64*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const unsigned __int64 value)
	{
		*(unsigned __int64*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(unsigned __int64& value)
	{
		value = *(unsigned __int64*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const float value)
	{
		*(float*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}
	Packet& operator >>(float& value)
	{
		value = *(float*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	Packet& operator <<(const double value)
	{
		*(double*)(pBuffer_ + rear_) = value;
		rear_ += sizeof(value);
		return *this;
	}

	Packet& operator >>(double& value)
	{
		value = *(double*)(pBuffer_ + front_);
		front_ += sizeof(value);
		return *this;
	}

	char* pBuffer_;
	int front_;
	int rear_;
	int refCnt_ = 0;

#pragma warning(disable : 26495)
	Packet()
	{
		pBuffer_ = new char[BUFFER_SIZE];
	}
#pragma warning(default : 26495)

	~Packet()
	{
		delete[] pBuffer_;
	}

	__forceinline LONG IncreaseRefCnt()
	{
		return InterlockedIncrement((LONG*)&refCnt_);
	}
	__forceinline LONG DecrementRefCnt()
	{
		return InterlockedDecrement((LONG*)&refCnt_);
	}

	template<ServerType st>
	void SetHeader()
	{
		if constexpr (st == Net)
		{
			NetHeader* pHeader = (NetHeader*)pBuffer_;
			pHeader->code_ = 0x89;
			pHeader->payloadLen_ = rear_ - front_;
			pHeader->randomKey_ = rand();

			unsigned char* const pPayload = (unsigned char*)pHeader + sizeof(NetHeader);
			unsigned long long sum = 0;
			for (int i = 0; i < pHeader->payloadLen_; ++i)
			{
				sum += pPayload[i];
			}
			pHeader->checkSum_ = sum % UCHAR_MAX;
		}
		else
		{
			((LanHeader*)pBuffer_)->payloadLen_ = rear_ - front_;
		}
	}

	template<ServerType type>
	static Packet* Alloc()
	{
		Packet* pPacket = packetPool.Alloc();
		pPacket->Clear<type>();
		return pPacket;
	}

	private:
	static inline CTlsObjectPool<Packet, false> packetPool;
	static void Free(Packet* pPacket)
	{
		packetPool.Free(pPacket);
	}

	friend class SmartPacket;
	friend void LanServer::RecvProc(Session* pSession, int numberOfBytesTransferred);
	friend __forceinline void ClearPacket(Session* pSession);
	friend __forceinline void ReleaseSendFailPacket(Session* pSession);
};

class SmartPacket
{
public:
	Packet* pPacket_;
	SmartPacket(Packet*&& pPacket)
		:pPacket_{pPacket}
	{
		pPacket_->IncreaseRefCnt();
	}

	~SmartPacket()
	{
		if(pPacket_->DecrementRefCnt() == 0)
		{
			Packet::Free(pPacket_);
		}
	}

	__forceinline Packet& operator*()
	{
		return *pPacket_;
	}

	__forceinline Packet* operator->()
	{
		return pPacket_;
	}

	__forceinline Packet* GetPacket() 
	{
		return pPacket_;
	}
};

#undef QUEUE
