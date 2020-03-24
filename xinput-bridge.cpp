#define WIN32_LEAN_AND_MEAN

#include "targetver.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <combaseapi.h>
#include <xinput.h>
#include <stdio.h>
#include <stdint.h>
#include "resource.h"
#include "shared.h"

#define MAX_IP_ADDRESS 256

enum class Severity
{
	none,
	bad,
	pending,
	good,
};

struct GamepadInfo
{
	XINPUT_STATE prev_state;
	XINPUT_STATE state;
	bool prev_connected;
	bool connected;
};

HWND g_hdlg;
HANDLE g_thread;

SRWLOCK g_ip_lock = SRWLOCK_INIT;
WCHAR g_ip_address[MAX_IP_ADDRESS];
HANDLE g_ip_changed_event;

Severity g_resolve_sev;
Severity g_input_sev;
Severity g_connection_sev;

GamepadInfo g_gamepad[MAX_GAMEPADS];
GamepadInfo g_combined_gamepad;
DWORD g_gamepad_connected_mask;

decltype(XInputGetState)* xinput_get_state;

COLORREF GetSeverityColour(Severity sev)
{
	switch(sev)
	{
		case Severity::bad: return RGB(128, 0, 0);
		case Severity::good: return RGB(0, 128, 0);
	}

	return RGB(0, 0, 0);
}

void SetWindowIcon(Severity sev)
{
	static Severity prev_sev;

	if (prev_sev == sev)
		return;

	prev_sev = sev;

	static HANDLE bad     = LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_BAD), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	static HANDLE pending = LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_PENDING), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	static HANDLE good    = LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_CONNECTED), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);

	LPARAM param = (LPARAM)bad;

	switch(sev)
	{
		case Severity::pending: param = (LPARAM)pending; break;
		case Severity::good:    param = (LPARAM)good;    break;
	}

	SendMessage(g_hdlg, WM_SETICON, ICON_SMALL, param);
}

void SetStatus(int control_id, const WCHAR* msg, va_list ap)
{
	WCHAR buf[MAX_FORMAT_BUF];
	_vsnwprintf_s_l(buf, MAX_FORMAT_BUF, _TRUNCATE, msg, nullptr, ap);
	SetDlgItemTextW(g_hdlg, control_id, buf);
}

void SetResolveStatus(Severity sev, const WCHAR* msg, ...)
{
	g_resolve_sev = sev;

	va_list ap;
	va_start(ap, msg);
	SetStatus(ID_RESOLVE_STATUS, msg, ap);
	va_end(ap);
}

void SetInputStatus(Severity sev, const WCHAR* msg, ...)
{
	g_input_sev = sev;

	va_list ap;
	va_start(ap, msg);
	SetStatus(ID_INPUT_STATUS, msg, ap);
	va_end(ap);
}

void SetConnectionStatus(Severity sev, const WCHAR* msg, ...)
{
	g_connection_sev = sev;

	va_list ap;
	va_start(ap, msg);
	SetStatus(ID_CONNECTION_STATUS, msg, ap);
	va_end(ap);
}

void SetEmptyResolveStatus()
{
	SetResolveStatus(Severity::none, L"\u26A0 Enter the address of a target machine to connect to");
}

void SetInputConnected()
{
	static DWORD prev_mask = -1;

	if (prev_mask == g_gamepad_connected_mask)
		return;

	prev_mask = g_gamepad_connected_mask;

	int used[MAX_GAMEPADS];
	int used_count = 0;

	for(int i = 0; i < MAX_GAMEPADS; i++)
	{
		if (g_gamepad_connected_mask & (1 << i))
			used[used_count++] = 1 + i;
	}

	if (used_count > 0)
	{
		static const WCHAR* msgs[MAX_GAMEPADS] =
		{
			L"\u2714 Slot %i", L"\u2714 Combining slots %i+%i", L"\u2714 Combining slots %i+%i+%i", L"\u2714 Combining slots %i+%i+%i+%i"
		};

		SetInputStatus(Severity::good, msgs[used_count - 1], used[0], used[1], used[2], used[3]);
	}
	else
	{
		SetInputStatus(Severity::bad, L"\u274C No gamepads found");
	}
}

void SetEmptyConnectionStatus()
{
	SetConnectionStatus(Severity::bad, L"\u274C Not connected");
}

void MaxStick(SHORT* a, SHORT b)
{
	if (abs(*a) < abs(b))
		*a = b;
}

bool PollGamepads()
{
	bool changed = false;

	for(int i = 0; i < MAX_GAMEPADS; i++)
	{
		auto* pad = &g_gamepad[i];

		pad->prev_state = pad->state;
		pad->prev_connected = pad->connected;

		if (xinput_get_state(i, &pad->state) == ERROR_SUCCESS)
		{
			if (pad->state.dwPacketNumber != pad->prev_state.dwPacketNumber)
				changed = true;

			pad->connected = true;
		}
		else
		{
			if (pad->prev_connected)
				changed = true;

			pad->state = { };
			pad->connected = false;
		}
	}

	g_combined_gamepad.prev_state = g_combined_gamepad.state;
	g_combined_gamepad.prev_connected = g_combined_gamepad.connected;

	g_combined_gamepad.state = { };
	g_combined_gamepad.connected = false;

	g_gamepad_connected_mask = 0;

	for(int i = 0; i < MAX_GAMEPADS; i++)
	{
		auto* pad = &g_gamepad[i];
		auto* pad_state = &pad->state;

		g_combined_gamepad.connected |= pad->connected;

		g_gamepad_connected_mask |= (int)pad->connected << i;

		g_combined_gamepad.state.Gamepad.wButtons |= pad_state->Gamepad.wButtons;
		g_combined_gamepad.state.Gamepad.bLeftTrigger = max(g_combined_gamepad.state.Gamepad.bLeftTrigger, pad_state->Gamepad.bLeftTrigger);
		g_combined_gamepad.state.Gamepad.bRightTrigger = max(g_combined_gamepad.state.Gamepad.bLeftTrigger, pad_state->Gamepad.bRightTrigger);

		MaxStick(&g_combined_gamepad.state.Gamepad.sThumbLX, pad_state->Gamepad.sThumbLX);
		MaxStick(&g_combined_gamepad.state.Gamepad.sThumbLY, pad_state->Gamepad.sThumbLY);
		MaxStick(&g_combined_gamepad.state.Gamepad.sThumbRX, pad_state->Gamepad.sThumbRX);
		MaxStick(&g_combined_gamepad.state.Gamepad.sThumbRY, pad_state->Gamepad.sThumbRY);
	}

	g_combined_gamepad.state.dwPacketNumber = g_combined_gamepad.prev_state.dwPacketNumber + changed;

	return changed;
}

DWORD WINAPI BridgeThread(void*)
{
	ADDRINFOW* addr_info = nullptr;
	DWORD pkt_count = 0;
	DWORD reply_count = 0;
	ULONGLONG pkt_report_time = GetTickCount64();

	SOCKET sock4 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	SOCKET sock6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

	SOCKADDR_IN addr4 = { };
	SOCKADDR_IN6 addr6 = { };

	addr4.sin_family = AF_INET;
	addr4.sin_addr.s_addr = INADDR_ANY;
	addr4.sin_port = 0;

	addr6.sin6_family = AF_INET6;
	addr6.sin6_addr = IN6ADDR_ANY_INIT;
	addr6.sin6_port = 0;

	bind(sock4, (sockaddr*)&addr4, sizeof(addr4));
	bind(sock6, (sockaddr*)&addr6, sizeof(addr6));

	HANDLE event4 = WSACreateEvent();
	HANDLE event6 = WSACreateEvent();

	WSAEventSelect(sock4, event4, FD_READ);
	WSAEventSelect(sock6, event6, FD_READ);

	SetEmptyResolveStatus();
	SetInputConnected();
	SetEmptyConnectionStatus();

	for(;;)
	{
		// TODO: trigger via WM_INPUT instead of polling every 2ms?
		HANDLE wait_handles[3] = { g_ip_changed_event, event4, event6 };
		DWORD sleep_time = addr_info ? 2 : 1000;
		DWORD wait_result = WaitForMultipleObjects(3, wait_handles, FALSE, sleep_time);
		
		Packet packet4 = { };
		int addr4_len = sizeof(addr4);
		int len4 = recvfrom(sock4, (char*)&packet4, sizeof(packet4), 0, (sockaddr*)&addr4, &addr4_len);

		Packet packet6 = { };
		int addr6_len = sizeof(addr6);
		int len6 = recvfrom(sock6, (char*)&packet6, sizeof(packet6), 0, (sockaddr*)&addr6, &addr6_len);

		if ((len4 > 0) || (len6 > 0))
			reply_count++;

		if ((wait_result == WAIT_OBJECT_0) || !addr_info)
		{
			FreeAddrInfoW(addr_info);
			addr_info = nullptr;

			WCHAR local_ip_address[MAX_IP_ADDRESS];

 			AcquireSRWLockExclusive(&g_ip_lock);
			memcpy(local_ip_address, g_ip_address, sizeof(local_ip_address));
			ReleaseSRWLockExclusive(&g_ip_lock);

			SetEmptyConnectionStatus();
			SetWindowIcon(Severity::bad);

			if (*local_ip_address)
			{
				SetResolveStatus(Severity::pending, L"\u231A Resolving...");

				ADDRINFOW hints = { };
				hints.ai_family = AF_UNSPEC;
				hints.ai_socktype = SOCK_DGRAM;
				hints.ai_protocol = IPPROTO_UDP;

				INT resolve_result = GetAddrInfoW(local_ip_address, PORT_STR, nullptr, &addr_info);

				if (resolve_result == 0)
				{
					WCHAR ip_str[INET6_ADDRSTRLEN];
					DWORD ip_str_len = INET6_ADDRSTRLEN;

					WSAAddressToStringW(addr_info->ai_addr, (DWORD)addr_info->ai_addrlen, nullptr, ip_str, &ip_str_len);

					SetResolveStatus(Severity::good, L"\u2714 %s", ip_str);
				}
				else
				{
					if (resolve_result == WSAHOST_NOT_FOUND)
						SetResolveStatus(Severity::bad, L"\u274C The target address did not resolve to a valid IP");
					else
						SetResolveStatus(Severity::bad, L"\u274C %s", gai_strerrorW(resolve_result));
				}
			}
			else
			{
				SetEmptyResolveStatus();
			}
		}

		bool gamepads_changed = PollGamepads();

		SetInputConnected();

		if (addr_info && (gamepads_changed || g_combined_gamepad.connected))
		{
			ULONGLONG now = GetTickCount64();
			bool trigger_report = (now - pkt_report_time) >= 1000;

			if (gamepads_changed || trigger_report)
			{
				pkt_count++;

				for(ADDRINFOW* ai = addr_info; ai; ai = ai->ai_next)
				{
					Packet packet = { };
					int packet_len = sizeof(packet);

					packet.state = g_combined_gamepad.state;

					switch(ai->ai_family)
					{
						case AF_INET:
							sendto(sock4, (char*)&packet, packet_len, 0, ai->ai_addr, (int)ai->ai_addrlen);
						break;

						case AF_INET6:
							sendto(sock6, (char*)&packet, packet_len, 0, ai->ai_addr, (int)ai->ai_addrlen);
						break;
					}
				}
			}

			if (trigger_report)
			{
				SetWindowIcon((reply_count > 0) ? Severity::good : Severity::pending);

				if (reply_count > 0)
					SetConnectionStatus(Severity::good, L"\u2714 Connected (packets per second %i)", (int)((pkt_count * 1000.0f) / (float)(now - pkt_report_time)));
				else
					SetConnectionStatus(Severity::none, L"\u231A Attempting to connect... (packets per second %i)", (int)((pkt_count * 1000.0f) / (float)(now - pkt_report_time)));

				pkt_count = 0;
				reply_count = 0;
				pkt_report_time = now;
			}
		}
		else
		{
			SetWindowIcon(Severity::bad);
		}
	}

	FreeAddrInfoW(addr_info);
	closesocket(sock4);
	closesocket(sock6);
	WSACloseEvent(event4);
	WSACloseEvent(event6);

	return 0;
}

INT_PTR CALLBACK BridgeDlgProc(HWND hdlg, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
		case WM_INITDIALOG:
		{
			g_hdlg = hdlg;

			SetWindowIcon(Severity::bad);

			g_ip_changed_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			g_thread = CreateThread(nullptr, 0, BridgeThread, nullptr, 0, nullptr);

			if (__argc > 1)
				SetDlgItemTextW(hdlg, IDC_IP, __wargv[1]);
		}
        return (INT_PTR)TRUE;

		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSTATIC:
		{
			int control_id = GetDlgCtrlID((HWND)lparam);
			Severity sev = Severity::none;

			if (control_id == ID_RESOLVE_STATUS)
				sev = g_resolve_sev;
			else if (control_id == ID_INPUT_STATUS)
				sev = g_input_sev;
			else if (control_id == ID_CONNECTION_STATUS)
				sev = g_connection_sev;
 
			SetDCBrushColor((HDC)wparam, RGB(255, 255, 255));
			SetBkColor((HDC)wparam, RGB(255, 255, 255));

			SetTextColor((HDC)wparam, GetSeverityColour(sev));
		}
		return (INT_PTR)GetStockObject(DC_BRUSH);

		case WM_CLOSE:
			EndDialog(hdlg, 0);
			//ExitProcess(0);
		return (INT_PTR)TRUE;

		case WM_COMMAND:
		{
			if ((LOWORD(wparam) == IDC_IP) && (HIWORD(wparam) == EN_CHANGE))
			{
				WCHAR local_ip_address[MAX_IP_ADDRESS] = { };
				GetDlgItemTextW(hdlg, IDC_IP, local_ip_address, MAX_IP_ADDRESS);

				WCHAR* start = local_ip_address;

				while((*start == ' ') || (*start == '\t'))
					start++;

				WCHAR* end = start + lstrlenW(start);

				while((start < end) && ((end[-1] == ' ') || (end[-1] == '\t')))
					end--;

				size_t length = end - start;

				AcquireSRWLockExclusive(&g_ip_lock);
				memcpy(g_ip_address, start, length * sizeof(WCHAR));
				g_ip_address[length] = '\0';
				ReleaseSRWLockExclusive(&g_ip_lock);

				SetEvent(g_ip_changed_event);
			}
		}
        break;
    }

    return (INT_PTR)FALSE;
}

bool InitXInput()
{
	static const WCHAR* dlls[] =
	{
		L"\\xinput1_4.dll",
		L"\\xinput1_3.dll",
		L"\\xinput9_1_0.dll",
		L"\\xinput1_2.dll",
		L"\\xinput1_1.dll",
		nullptr,
	};

	WCHAR system[MAX_PATH + MAX_PATH];
	GetSystemDirectoryW(system, MAX_PATH);

	WCHAR* system_end = system + lstrlenW(system);

	for(int i = 0; dlls[i] != nullptr; i++)
	{
		lstrcpyW(system_end, dlls[i]);

		HMODULE xinput_dll = LoadLibraryW(system);

		if (xinput_dll)
		{
			xinput_get_state = (decltype(XInputGetState)*)GetProcAddress(xinput_dll, "XInputGetState");

			if (xinput_get_state)
				return true;

			FreeLibrary(xinput_dll);
		}
	}

	return false;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	CoInitializeEx(0, COINIT_MULTITHREADED);

	WSADATA wsad;
	WSAStartup(MAKEWORD(2, 2), &wsad);

	if (InitXInput())
		DialogBoxParamW(hInstance, MAKEINTRESOURCE(IDD_XINPUTBRIDGE_DIALOG), nullptr, BridgeDlgProc, 0);
	else
		MessageBoxW(nullptr, L"Unable to find an xinput dll in windows\\system32", L"xinput-bridge", MB_OK);

	ExitProcess(0);

	return 0;
}
