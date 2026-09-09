#include "Debug.h"

#include <Windows.h>
#include <stdio.h>

namespace
{
	void Emit(const char* where, const char* what, const HRESULT* hr)
	{
		char buf[512];
		if (hr != nullptr) {
			sprintf_s(buf, "[FAIL] %s : %s (hr=0x%08lX)\n",
				where, what, static_cast<unsigned long>(*hr));
		}
		else {
			sprintf_s(buf, "[FAIL] %s : %s\n", where, what);
		}
		::OutputDebugStringA(buf);
	}
}

bool DebugFail(const char* where, const char* what)
{
	Emit(where, what, nullptr);
	return false;
}

bool DebugFail(const char* where, const char* what, HRESULT hr)
{
	Emit(where, what, &hr);
	return false;
}

decltype(nullptr) DebugFailNull(const char* where, const char* what)
{
	Emit(where, what, nullptr);
	return nullptr;
}

decltype(nullptr) DebugFailNull(const char* where, const char* what, HRESULT hr)
{
	Emit(where, what, &hr);
	return nullptr;
}
