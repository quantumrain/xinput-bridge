#pragma once

#define PORT 6668
#define PORT_STR L"6668"

#define MAX_FORMAT_BUF 256
#define MAX_GAMEPADS 4

struct Packet
{
	XINPUT_STATE state;
};

void Debug(const WCHAR* msg, ...)
{
	WCHAR buf[MAX_FORMAT_BUF];

	va_list ap;
	va_start(ap, msg);
	_vsnwprintf_s_l(buf, MAX_FORMAT_BUF, _TRUNCATE, msg, nullptr, ap);
	va_end(ap);

	OutputDebugStringW(buf);
	OutputDebugStringW(L"\r\n");
}
