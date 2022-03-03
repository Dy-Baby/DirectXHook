#include "DirectXHook.h"

// Static members
Logger DirectXHook::m_logger{ "DirectXHook" };
Renderer DirectXHook::m_renderer;
uintptr_t DirectXHook::m_originalPresentAddress = 0;
uintptr_t DirectXHook::m_originalResizeBuffersAddress = 0;
uintptr_t DirectXHook::m_originalExecuteCommandListsAddress = 0;
std::vector<std::vector<unsigned char>> DirectXHook::m_functionHeaders;
bool DirectXHook::m_firstPresent = true;
bool DirectXHook::m_firstResizeBuffers = true;
bool DirectXHook::m_firstExecuteCommandLists = true;

DirectXHook::DirectXHook()
{
	std::fstream terminalEnableFile;
	terminalEnableFile.open("hook_enable_terminal.txt", std::fstream::in);
	if (terminalEnableFile.is_open())
	{
		if (AllocConsole())
		{
			freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
			SetWindowText(GetConsoleWindow(), "DirectXHook");
		}
		terminalEnableFile.close();
	}

	SetFunctionHeaders();
}

void DirectXHook::Hook()
{
	m_logger.Log("Hooking...");
	m_logger.Log("OnPresent: %p", &OnPresent);
	m_logger.Log("OnResizeBuffers: %p", &OnResizeBuffers);

	// Let other hooks finish their business before we hook.
	// For some reason, sleeping causes MSI Afterburner to crash the application.
	Sleep(6000);

	if (IsDllLoaded("RTSSHooks64.dll"))
	{
		MessageBox(NULL, "DirectXHook is incompatible with MSI afterburner, please close it and restart.", "Incompatible overlay", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);
		return;
	}

	m_dummySwapChain = CreateDummySwapChain();
	HookSwapChainVmt(m_dummySwapChain, &m_originalPresentAddress, &m_originalResizeBuffersAddress, (uintptr_t)&OnPresent, (uintptr_t)&OnResizeBuffers);

	if (IsDllLoaded("d3d12.dll"))
	{
		m_dummyCommandQueue = CreateDummyCommandQueue();
		HookCommandQueueVmt(m_dummyCommandQueue, &m_originalExecuteCommandListsAddress, (uintptr_t)&OnExecuteCommandLists);
	}
}

void DirectXHook::DrawExamples(bool doDraw)
{
	m_renderer.DrawExamples(doDraw);
}

void DirectXHook::SetRenderCallback(IRenderCallback* object)
{
	m_renderer.SetRenderCallback(object);
}

// Reshade screws with our overlay code, for some reason...
// but not if we let it finish doing its business.
void DirectXHook::HandleReshade(bool reshadeLoaded)
{
	if (reshadeLoaded)
	{
		Sleep(20000);
	}
}

bool DirectXHook::IsDllLoaded(std::string dllName)
{
	std::vector<HMODULE> modules(0, 0);
	DWORD lpcbNeeded;
	EnumProcessModules(GetCurrentProcess(), &modules[0], modules.size(), &lpcbNeeded);
	modules.resize(lpcbNeeded, 0);
	EnumProcessModules(GetCurrentProcess(), &modules[0], modules.size(), &lpcbNeeded);

	std::string lpBaseName (dllName.length(), 'x');
	for (auto module : modules)
	{
		GetModuleBaseName(GetCurrentProcess(), module, &lpBaseName[0], lpBaseName.length() + 1);
		if (lpBaseName == dllName)
		{
			m_logger.Log("%s is loaded", dllName);
			return true;
		}
	}

	return false;
}

IDXGISwapChain* DirectXHook::CreateDummySwapChain()
{
	WNDCLASSEX wc { 0 };
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = DefWindowProc;
	wc.lpszClassName = TEXT("dummy class");
	RegisterClassExA(&wc);
	HWND hWnd = CreateWindow(wc.lpszClassName, TEXT(""), WS_DISABLED, 0, 0, 0, 0, NULL, NULL, NULL, nullptr);

	DXGI_SWAP_CHAIN_DESC desc{ 0 };
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 1;
	desc.OutputWindow = hWnd;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL featureLevel[] =
	{
		D3D_FEATURE_LEVEL_9_1,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_12_1
	};

	ID3D11Device* dummyDevice;
	IDXGISwapChain* dummySwapChain;
	HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, NULL, featureLevel, 1, D3D11_SDK_VERSION, &desc, &dummySwapChain, &dummyDevice, NULL, NULL);
	if (FAILED(result))
	{
		_com_error error(result);
		m_logger.Log("CreateDeviceAndSwapChain failed: %s", error.ErrorMessage());
		DestroyWindow(desc.OutputWindow);
		UnregisterClass(wc.lpszClassName, GetModuleHandle(nullptr));
		return nullptr;
	}

	dummyDevice->Release();
	DestroyWindow(desc.OutputWindow);
	UnregisterClass(wc.lpszClassName, GetModuleHandle(nullptr));

	m_logger.Log("CreateDeviceAndSwapChain succeeded");

	return dummySwapChain;
}

ID3D12CommandQueue* DirectXHook::CreateDummyCommandQueue()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ID3D12Device* d12Device = nullptr;
	D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), (void**)&d12Device);

	ID3D12CommandQueue* dummyCommandQueue;
	d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&dummyCommandQueue));

	m_logger.Log("Command queue: %p", dummyCommandQueue);

	return dummyCommandQueue;
}

// Hooks the functions that we need in the Virtual Method Table of IDXGISwapChain.
// A pointer to the VMT of an object exists in the first 4/8 bytes of the object (or the last bytes, depending on the compiler).
void DirectXHook::HookSwapChainVmt(IDXGISwapChain* dummySwapChain, uintptr_t* originalPresentAddress, uintptr_t* originalResizeBuffersAddress, uintptr_t newPresentAddress, uintptr_t newResizeBuffersAddress)
{
	int size = sizeof(size_t);

	uintptr_t vmtBaseAddress = (*(uintptr_t*)dummySwapChain);
	uintptr_t vmtPresentIndex = (vmtBaseAddress + (size * 8));
	uintptr_t vmtResizeBuffersIndex = (vmtBaseAddress + (size * 13));

	m_logger.Log("SwapChain VMT base address: %p", vmtBaseAddress);
	m_logger.Log("SwapChain VMT Present index: %p", vmtPresentIndex);
	m_logger.Log("SwapChain VMT ResizeBuffers index: %p", vmtResizeBuffersIndex);

	DWORD oldProtection;
	DWORD oldProtection2;

	VirtualProtect((void*)vmtPresentIndex, size, PAGE_EXECUTE_READWRITE, &oldProtection);
	VirtualProtect((void*)vmtResizeBuffersIndex, size, PAGE_EXECUTE_READWRITE, &oldProtection2);

	*originalPresentAddress = (*(uintptr_t*)vmtPresentIndex);
	*originalResizeBuffersAddress = (*(uintptr_t*)vmtResizeBuffersIndex);

	// This sets the VMT entries to point towards our functions instead.
	*(uintptr_t*)vmtPresentIndex = newPresentAddress;
	*(uintptr_t*)vmtResizeBuffersIndex = newResizeBuffersAddress;

	VirtualProtect((void*)vmtPresentIndex, size, oldProtection, &oldProtection);
	VirtualProtect((void*)vmtResizeBuffersIndex, size, oldProtection2, &oldProtection2);

	dummySwapChain->Release();

	m_logger.Log("Original Present address: %p", m_originalPresentAddress);
	m_logger.Log("Original ResizeBuffers address: %p", m_originalResizeBuffersAddress);
}

void DirectXHook::HookCommandQueueVmt(ID3D12CommandQueue* dummyCommandQueue, uintptr_t* originalExecuteCommandListsAddress, uintptr_t newExecuteCommandListsAddress)
{
	uintptr_t vmtBaseAddress = (*(uintptr_t*)dummyCommandQueue);
	uintptr_t vmtExecuteCommandListsIndex = (vmtBaseAddress + (8 * 10));

	m_logger.Log("CommandQueue VMT base address: %p", vmtBaseAddress);
	m_logger.Log("ExecuteCommandLists index: %p", vmtExecuteCommandListsIndex);

	DWORD oldProtection;

	VirtualProtect((void*)vmtExecuteCommandListsIndex, 8, PAGE_EXECUTE_READWRITE, &oldProtection);

	*originalExecuteCommandListsAddress = (*(uintptr_t*)vmtExecuteCommandListsIndex);
	*(uintptr_t*)vmtExecuteCommandListsIndex = newExecuteCommandListsAddress;

	VirtualProtect((void*)vmtExecuteCommandListsIndex, 8, oldProtection, &oldProtection);

	m_logger.Log("Original ExecuteCommandLists address: %p", m_originalExecuteCommandListsAddress);
}

// Used to fix bytes overwritten by the Steam overlay hook.
void DirectXHook::SetFunctionHeaders()
{
	m_functionHeaders.push_back({ 0x48, 0x89, 0x5C, 0x24, 0x10 }); // Present
	m_functionHeaders.push_back({ 0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54 }); // ResizeBuffers
	m_functionHeaders.push_back({ 0x48, 0x89, 0x5C, 0x24, 0x08 }); // ExecuteCommandLists
}

// This fixes an issue with the Steam overlay hooking in two places at the same time.
void DirectXHook::RemoveDoubleHook(uintptr_t trampolineAddress, uintptr_t originalFunctionAddress, std::vector<unsigned char> originalFunctionHeader)
{
	unsigned char firstByteAtTrampoline = *(unsigned char*)trampolineAddress;
	unsigned char firstByteAtOriginal = *(unsigned char*)originalFunctionAddress;

	DWORD oldProtection;
	VirtualProtect((void*)originalFunctionAddress, originalFunctionHeader.size(), PAGE_EXECUTE_READWRITE, &oldProtection);

	if (firstByteAtTrampoline == 0xE9 && firstByteAtOriginal == 0xE9)
	{
		m_logger.Log("Found double hook");
		uintptr_t trampolineDestination = FindTrampolineDestination(trampolineAddress);
		uintptr_t trampolineDestination2 = FindTrampolineDestination(originalFunctionAddress);

		if (trampolineDestination == trampolineDestination2)
		{
			memcpy((void*)originalFunctionAddress, &originalFunctionHeader[0], originalFunctionHeader.size());
			m_logger.Log("Removed double hook at %p", originalFunctionAddress);
		}
	}

	VirtualProtect((void*)originalFunctionAddress, originalFunctionHeader.size(), oldProtection, &oldProtection);
}

// Finds the final destination of a trampoline placed by other hooks (the Steam overlay, for example).
uintptr_t DirectXHook::FindTrampolineDestination(uintptr_t firstJmpAddr)
{
	int offset = 0;
	uintptr_t absolute = 0;
	uintptr_t destination = 0;

	memcpy(&offset, (void*)(firstJmpAddr + 1), 4);
	absolute = firstJmpAddr + offset + 5;

	if (*(unsigned char*)absolute == 0xFF)
	{
		memcpy(&destination, (void*)(absolute + 6), 8);
	} 
	else if (*(unsigned char*)absolute == 0xE9)
	{
		memcpy(&offset, (void*)(absolute + 1), 4);
		destination = absolute + offset + 5;
	}

	return destination;
}

/*
* The real Present will get hooked and then detour to this function.
* Present is part of the final rendering stage in DirectX.
* https://docs.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
*/
HRESULT __stdcall DirectXHook::OnPresent(IDXGISwapChain* pThis, UINT syncInterval, UINT flags)
{
	if (m_firstPresent)
	{
		RemoveDoubleHook((uintptr_t)&OnPresent, m_originalPresentAddress, m_functionHeaders[0]);
		m_firstPresent = false;
	}

	m_renderer.OnPresent(pThis, syncInterval, flags);
	return ((Present)m_originalPresentAddress)(pThis, syncInterval, flags);
}

/*
* The real ResizeBuffers will get hooked and then detour to this function.
* ResizeBuffers usually gets called when the window resizes.
* We need to hook this so we can release our reference to the render target when it's called.
* If we don't do this then the game will most likely crash.
* https://docs.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers
*/
HRESULT __stdcall DirectXHook::OnResizeBuffers(IDXGISwapChain* pThis, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags)
{
	if (m_firstResizeBuffers)
	{
		RemoveDoubleHook((uintptr_t)&OnResizeBuffers, m_originalResizeBuffersAddress, m_functionHeaders[1]);
		m_firstResizeBuffers = false;
	}

	m_renderer.OnResizeBuffers(pThis, bufferCount, width, height, newFormat, swapChainFlags);
	return ((ResizeBuffers)m_originalResizeBuffersAddress)(pThis, bufferCount, width, height, newFormat, swapChainFlags);
}

/* 
* The real ExecuteCommandLists will get hooked and then detour to this function.
* ExecuteCommandLists gets called when work is to be submitted to the GPU.
* We need to hook this to grab the command queue so we can use it to create the D3D11On12 device in DirectX 12 games.
*/
void __stdcall DirectXHook::OnExecuteCommandLists(ID3D12CommandQueue* pThis, UINT numCommandLists, const ID3D12CommandList** ppCommandLists)
{
	if (m_firstExecuteCommandLists)
	{
		RemoveDoubleHook((uintptr_t)&OnExecuteCommandLists, m_originalExecuteCommandListsAddress, m_functionHeaders[2]);
		m_firstExecuteCommandLists = false;
	}

	if (m_renderer.missingCommandQueue && pThis->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
		m_renderer.SetCommandQueue(pThis);
	}

	((ExecuteCommandLists)m_originalExecuteCommandListsAddress)(pThis, numCommandLists, ppCommandLists);
}
