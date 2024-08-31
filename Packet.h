#pragma once
#include <memory.h>

typedef short NET_HEADER;

class Packet
{
public:
	enum RING_SIZE
	{
		RINGBUFFER_SIZE = 10000
	};

	enum NET_HEADER_SIZE
	{
		NET_HEADER_SIZE = sizeof(short)
	};

	enum PACKET_SIZE
	{
		DEFAULT_SIZE = (RINGBUFFER_SIZE / 8 + NET_HEADER_SIZE)
	};



	void Clear(void)
	{
		front_ = rear_ = NET_HEADER_SIZE;
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
		return pBuffer_ + NET_HEADER_SIZE;
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
	static Packet* Alloc();
	static void Free(Packet* pPacket);
	static void ReleasePacketPool();
	char* pBuffer_;
private:	
	int front_ = NET_HEADER_SIZE;
	int rear_ = NET_HEADER_SIZE;

	Packet()
		:front_{ NET_HEADER_SIZE }, rear_{ NET_HEADER_SIZE }
	{
		pBuffer_ = new char[DEFAULT_SIZE];
	}

	~Packet()
	{
		delete[] pBuffer_;
	}

	friend unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	template<typename T> friend class FreeList;
	template<typename T> friend class FreeListNode;
};
