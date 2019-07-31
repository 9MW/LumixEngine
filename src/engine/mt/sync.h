#pragma once
#include "engine/lumix.h"
#ifdef __linux__
	#include <pthread.h>
#endif

namespace Lumix
{
namespace MT
{


#if defined _WIN32
	typedef void* SemaphoreHandle;
	typedef void* MutexHandle;
	typedef void* EventHandle;
#elif defined __linux__
	struct SemaphoreHandle
	{
		pthread_mutex_t mutex;
		pthread_cond_t cond;
		i32 count;
	};
	typedef pthread_mutex_t MutexHandle;
	struct EventHandle
	{
		pthread_mutex_t mutex;
		pthread_cond_t cond;
		bool signaled;
		bool manual_reset;
	};
#endif


class alignas(8) LUMIX_ENGINE_API CriticalSection
{
public:
	CriticalSection();
	~CriticalSection();

	CriticalSection(const CriticalSection&) = delete;

	void enter();
	void exit();

private:
	u8 data[64];
};


class LUMIX_ENGINE_API Semaphore
{
public:
	Semaphore(int init_count, int max_count);
	~Semaphore();

	void signal();

	void wait();
	bool poll();

private:
	SemaphoreHandle m_id;
};


class LUMIX_ENGINE_API Event
{
public:
	explicit Event(bool manual_reset);
	~Event();

	void reset();

	void trigger();

	void wait();
	void waitTimeout(u32 timeout_ms);
	bool poll();

	static void waitMultiple(Event& event0, Event& event1, u32 timeout_ms);

private:
	EventHandle m_id;
};


class CriticalSectionLock
{
public:
	explicit CriticalSectionLock(CriticalSection& cs)
		: m_critical_section(cs)
	{
		cs.enter();
	}
	~CriticalSectionLock() { m_critical_section.exit(); }

	CriticalSectionLock(const CriticalSectionLock&) = delete;
	void operator=(const CriticalSectionLock&) = delete;

private:
	CriticalSection& m_critical_section;
};


} // namespace MT
} // namespace Lumix
