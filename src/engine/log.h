#pragma once


#include "engine/lumix.h"


#define LUMIX_FATAL(cond) Lumix::fatal((cond), #cond);


namespace Lumix
{


struct IAllocator;
struct Log;
class Path;
class String;
template <typename T> class DelegateList;

enum class LogLevel {
	INFO,
	WARNING,
	ERROR,

	COUNT
};

using LogCallback = DelegateList<void (LogLevel, const char*, const char*)>;


class LUMIX_ENGINE_API LogProxy {
	public:
		LogProxy(Log* log, const char* system);
		~LogProxy();

		LogProxy& operator <<(const char* message);
		LogProxy& operator <<(float message);
		LogProxy& operator <<(i32 message);
		LogProxy& operator <<(u32 message);
		LogProxy& operator <<(u64 message);
		LogProxy& operator <<(const String& path);
		LogProxy& operator <<(const Path& path);
			
	private:
		Log* log;
		const char* system;

		LogProxy(const LogProxy&) = delete;
		void operator = (const LogProxy&) = delete;
};


LUMIX_ENGINE_API void fatal(bool cond, const char* msg);
LUMIX_ENGINE_API LogProxy logInfo(const char* system);
LUMIX_ENGINE_API LogProxy logWarning(const char* system);
LUMIX_ENGINE_API LogProxy logError(const char* system);
LUMIX_ENGINE_API LogCallback& getLogCallback();


} // namespace Lumix


