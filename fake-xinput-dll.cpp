#define WIN32_LEAN_AND_MEAN

#include "targetver.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include "resource.h"
#include "xinput-types.h"
#include "shared.h"

// TODO: local pass through of xinput?
// TODO: popup window to remind people that the dll has been overridden?

HANDLE g_thread;
HANDLE g_wake_event;
bool g_quit;

SRWLOCK g_state_lock = SRWLOCK_INIT;
XINPUT_STATE g_state;

void ProcessPacket(const Packet* packet)
{
	AcquireSRWLockExclusive(&g_state_lock);

	DWORD dist = packet->state.dwPacketNumber - g_state.dwPacketNumber;

	if ((dist > 0) && (dist < 0xFFFF'FF00))
		g_state = packet->state;

	ReleaseSRWLockExclusive(&g_state_lock);
}

DWORD WINAPI BridgeSinkThread(void*)
{
	SOCKET sock4 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	SOCKET sock6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

	SOCKADDR_IN addr4 = { };
	SOCKADDR_IN6 addr6 = { };

	addr4.sin_family = AF_INET;
	addr4.sin_addr.s_addr = INADDR_ANY;
	addr4.sin_port = htons(PORT);

	addr6.sin6_family = AF_INET6;
	addr6.sin6_addr = IN6ADDR_ANY_INIT;
	addr6.sin6_port = htons(PORT);

	bind(sock4, (sockaddr*)&addr4, sizeof(addr4));
	bind(sock6, (sockaddr*)&addr6, sizeof(addr6));

	HANDLE event4 = WSACreateEvent();
	HANDLE event6 = WSACreateEvent();

	WSAEventSelect(sock4, event4, FD_READ);
	WSAEventSelect(sock6, event6, FD_READ);

	ULONGLONG last_ack4 = 0;
	ULONGLONG last_ack6 = 0;

	while(!g_quit)
	{
		HANDLE wait_handles[3] = { event4, event6, g_wake_event };
		WaitForMultipleObjects(3, wait_handles, FALSE, INFINITE);

		Packet packet4 = { };
		int addr4_len = sizeof(addr4);
		int len4 = recvfrom(sock4, (char*)&packet4, sizeof(packet4), 0, (sockaddr*)&addr4, &addr4_len);

		Packet packet6 = { };
		int addr6_len = sizeof(addr6);
		int len6 = recvfrom(sock6, (char*)&packet6, sizeof(packet6), 0, (sockaddr*)&addr6, &addr6_len);

		ULONGLONG now = GetTickCount64();

		if (len4 > 0)
		{
			ProcessPacket(&packet4);

			if ((now - last_ack4) > 250)
			{
				last_ack4 = now;
				sendto(sock4, "okay", 4, 0, (sockaddr*)&addr4, addr4_len);
			}
		}

		if (len6 > 0)
		{
			ProcessPacket(&packet6);

			if ((now - last_ack6) > 250)
			{
				last_ack6 = now;
				sendto(sock6, "okay", 4, 0, (sockaddr*)&addr6, addr6_len);
			}
		}
	}

	WSACloseEvent(event4);
	WSACloseEvent(event6);

	return 0;
}

extern "C" __declspec(dllexport) void WINAPI XInputEnable(BOOL enable)
{
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
	if (dwUserIndex == 0)
	{
		AcquireSRWLockExclusive(&g_state_lock);
		*pState = g_state;
		ReleaseSRWLockExclusive(&g_state_lock);
		return ERROR_SUCCESS;
	}

	return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId, UINT* pRenderCount, LPWSTR pCaptureDeviceId, UINT* pCaptureCount)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE* pState)
{
	return XInputGetState(dwUserIndex, pState);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch(fdwReason)
	{
		case DLL_PROCESS_ATTACH:
		{
			WSADATA wsad;
			WSAStartup(MAKEWORD(2, 2), &wsad);

			g_wake_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			g_thread = CreateThread(nullptr, 0, BridgeSinkThread, nullptr, 0, nullptr);
		}
		break;

		case DLL_PROCESS_DETACH:
		{
			g_quit = true;
			SetEvent(g_wake_event);
			WaitForSingleObject(g_thread, 1000);
			WSACleanup();
		}
		break;
	}

	return TRUE;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	DllMain(nullptr, DLL_PROCESS_ATTACH, nullptr);
	Sleep(INFINITE);
	DllMain(nullptr, DLL_PROCESS_DETACH, nullptr);

	return 0;
}
