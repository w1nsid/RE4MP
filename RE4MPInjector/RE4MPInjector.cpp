#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <Windows.h>
#include <tlhelp32.h>
using namespace std;

int main(int argc, char* argv[]) {
	// Resolve DLL path relative to the injector executable
	char DllPath[MAX_PATH];
	GetModuleFileNameA(NULL, DllPath, MAX_PATH);
	// Strip executable name to get directory
	char* lastSlash = strrchr(DllPath, '\\');
	if (lastSlash) *(lastSlash + 1) = '\0';
	strcat_s(DllPath, MAX_PATH, "RE4MP.dll");

	// Allow override via command line argument
	if (argc > 1) {
		strncpy_s(DllPath, MAX_PATH, argv[1], _TRUNCATE);
	}

	HWND hwnd = FindWindowA(NULL, "Resident Evil 4");
	if (hwnd == NULL) {
		cerr << "Error: Resident Evil 4 window not found. Is the game running?" << endl;
		return 1;
	}

	DWORD procID = 0;
	GetWindowThreadProcessId(hwnd, &procID);
	if (procID == 0) {
		cerr << "Error: Could not get process ID." << endl;
		return 1;
	}

	HANDLE handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procID);
	if (handle == NULL) {
		cerr << "Error: Could not open process. Run as Administrator." << endl;
		return 1;
	}

	LPVOID pDllPath = VirtualAllocEx(handle, 0, strlen(DllPath) + 1, MEM_COMMIT, PAGE_READWRITE);
	if (pDllPath == NULL) {
		cerr << "Error: Could not allocate memory in target process." << endl;
		CloseHandle(handle);
		return 1;
	}

	WriteProcessMemory(handle, pDllPath, (LPVOID)DllPath, strlen(DllPath) + 1, 0);

	HANDLE hLoadThread = CreateRemoteThread(handle, 0, 0,
		(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "LoadLibraryA"), pDllPath, 0, 0);
	if (hLoadThread == NULL) {
		cerr << "Error: Could not create remote thread." << endl;
		VirtualFreeEx(handle, pDllPath, 0, MEM_RELEASE);
		CloseHandle(handle);
		return 1;
	}

	WaitForSingleObject(hLoadThread, INFINITE);

	cout << "RE4MP.dll injected successfully!" << endl;
	cout << "DLL path: " << DllPath << endl;
	cout << "DLL allocated at: 0x" << hex << pDllPath << endl;

	CloseHandle(hLoadThread);
	VirtualFreeEx(handle, pDllPath, 0, MEM_RELEASE);
	CloseHandle(handle);

	cout << "Press Enter to exit..." << endl;
	cin.get();

	return 0;
}