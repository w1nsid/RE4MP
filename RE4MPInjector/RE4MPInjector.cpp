#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <Windows.h>
#include <tlhelp32.h>
using namespace std;

static void pause_exit(int code) {
	cout << endl << "Press Enter to exit..." << endl;
	cin.get();
	exit(code);
}

static DWORD find_process(const char* exeName) {
	DWORD pid = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;
	PROCESSENTRY32 pe;
	pe.dwSize = sizeof(pe);
	if (Process32First(snap, &pe)) {
		do {
			if (_stricmp(pe.szExeFile, exeName) == 0) {
				pid = pe.th32ProcessID;
				break;
			}
		} while (Process32Next(snap, &pe));
	}
	CloseHandle(snap);
	return pid;
}

int main(int argc, char* argv[]) {
	cout << "========================================" << endl;
	cout << "  RE4MP Injector" << endl;
	cout << "========================================" << endl << endl;

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

	// Check DLL exists before doing anything
	DWORD fileAttr = GetFileAttributesA(DllPath);
	if (fileAttr == INVALID_FILE_ATTRIBUTES) {
		cerr << "Error: RE4MP.dll not found at:" << endl;
		cerr << "  " << DllPath << endl;
		cerr << "Make sure RE4MP.dll is in the same folder as this injector." << endl;
		pause_exit(1);
	}
	cout << "DLL: " << DllPath << endl;

	// Check re4mp.ini exists next to DLL
	char IniPath[MAX_PATH];
	strncpy_s(IniPath, MAX_PATH, DllPath, _TRUNCATE);
	char* iniSlash = strrchr(IniPath, '\\');
	if (iniSlash) *(iniSlash + 1) = '\0';
	strcat_s(IniPath, MAX_PATH, "re4mp.ini");
	if (GetFileAttributesA(IniPath) == INVALID_FILE_ATTRIBUTES) {
		cerr << "Warning: re4mp.ini not found at:" << endl;
		cerr << "  " << IniPath << endl;
		cerr << "The mod will use default settings (127.0.0.1:27015)." << endl << endl;
	}

	// Find the game - try window title first, then fall back to process name
	cout << "Looking for Resident Evil 4..." << endl;
	DWORD procID = 0;

	// Try common window titles
	const char* windowTitles[] = {
		"Resident Evil 4",
		"RESIDENT EVIL 4",
		"resident evil 4",
		"Biohazard 4",
		"BIOHAZARD 4",
		NULL
	};
	for (int i = 0; windowTitles[i] != NULL; i++) {
		HWND hwnd = FindWindowA(NULL, windowTitles[i]);
		if (hwnd != NULL) {
			GetWindowThreadProcessId(hwnd, &procID);
			if (procID != 0) {
				cout << "Found window: \"" << windowTitles[i] << "\"" << endl;
				break;
			}
		}
	}

	// Fallback: search by process name
	if (procID == 0) {
		const char* exeNames[] = { "bio4.exe", "Bio4.exe", "BIO4.EXE", NULL };
		for (int i = 0; exeNames[i] != NULL; i++) {
			procID = find_process(exeNames[i]);
			if (procID != 0) {
				cout << "Found process: " << exeNames[i] << endl;
				break;
			}
		}
	}

	if (procID == 0) {
		cerr << "Error: Resident Evil 4 not found!" << endl;
		cerr << "Make sure the game is running before launching the injector." << endl;
		pause_exit(1);
	}
	cout << "Process ID: " << procID << endl;

	HANDLE handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procID);
	if (handle == NULL) {
		DWORD err = GetLastError();
		cerr << "Error: Could not open process (error " << err << ")." << endl;
		if (err == 5) {
			cerr << "Access denied - right-click RE4MPInjector.exe and Run as Administrator." << endl;
		}
		pause_exit(1);
	}

	LPVOID pDllPath = VirtualAllocEx(handle, 0, strlen(DllPath) + 1, MEM_COMMIT, PAGE_READWRITE);
	if (pDllPath == NULL) {
		cerr << "Error: Could not allocate memory in target process (error " << GetLastError() << ")." << endl;
		CloseHandle(handle);
		pause_exit(1);
	}

	if (!WriteProcessMemory(handle, pDllPath, (LPVOID)DllPath, strlen(DllPath) + 1, 0)) {
		cerr << "Error: Could not write to target process memory (error " << GetLastError() << ")." << endl;
		VirtualFreeEx(handle, pDllPath, 0, MEM_RELEASE);
		CloseHandle(handle);
		pause_exit(1);
	}

	HANDLE hLoadThread = CreateRemoteThread(handle, 0, 0,
		(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "LoadLibraryA"), pDllPath, 0, 0);
	if (hLoadThread == NULL) {
		cerr << "Error: Could not create remote thread (error " << GetLastError() << ")." << endl;
		VirtualFreeEx(handle, pDllPath, 0, MEM_RELEASE);
		CloseHandle(handle);
		pause_exit(1);
	}

	WaitForSingleObject(hLoadThread, 10000);

	// Check if DLL actually loaded
	DWORD exitCode = 0;
	GetExitCodeThread(hLoadThread, &exitCode);
	if (exitCode == 0) {
		cerr << "Error: DLL injection failed - LoadLibrary returned NULL." << endl;
		cerr << "Possible causes:" << endl;
		cerr << "  - DLL built for wrong architecture (must be 32-bit/x86)" << endl;
		cerr << "  - Missing dependencies (try running in the game's directory)" << endl;
		CloseHandle(hLoadThread);
		VirtualFreeEx(handle, pDllPath, 0, MEM_RELEASE);
		CloseHandle(handle);
		pause_exit(1);
	}

	cout << endl;
	cout << "RE4MP.dll injected successfully!" << endl;
	cout << "DLL loaded at: 0x" << hex << exitCode << endl;

	CloseHandle(hLoadThread);
	VirtualFreeEx(handle, pDllPath, 0, MEM_RELEASE);
	CloseHandle(handle);

	pause_exit(0);
	return 0;
}