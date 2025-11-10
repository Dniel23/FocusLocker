#define _WIN32_WINNT 0x0500 
#include <windows.h>
#include <wchar.h> 
#include <string.h>

// --- Variáveis Globais ---
#define IDT_STATUS_HIDE 1001         
#define IDT_FOCUS_ENFORCEMENT 1002   
#define OVERLAY_DURATION 2000        // 2 segundos (2000 ms)

HWND g_hMainWnd = NULL;             
HWND g_hOverlayWnd = NULL;          
HWND g_hTargetWnd = NULL;           
BOOL g_bFocusLockActive = FALSE;    
BOOL g_bContinuousFocusMode = FALSE; 

HHOOK g_hMouseHook = NULL;
HHOOK g_hKeyboardHook = NULL;

// Usando WCHAR (Wide Character) para strings de exibição
WCHAR g_szStatusText[32] = L"DESATIVADO";
WCHAR g_szTargetAppName[256] = L"Nenhum Aplicativo Alvo Selecionado";

// Cores para o display
COLORREF g_crStatusColor = RGB(255, 0, 0); 
COLORREF g_crBkgColor = RGB(30, 30, 30);   
COLORREF g_crTextColor = RGB(200, 200, 200); 

// --- Protótipos das Funções ---
void UpdateMainDisplay();
void UpdateOverlayStatus(const WCHAR* status, COLORREF color);
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
HWND CreateOverlayWindow(HINSTANCE hInstance);

// --- Funções de Overlay Visual ---

// Criação da Janela de Overlay
HWND CreateOverlayWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) }; 
    wc.lpfnWndProc = OverlayWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FocusLockOverlayClass"; 
    
    if (!RegisterClassExW(&wc)) {
        return NULL;
    }

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int windowWidth = 240; 
    int windowHeight = 35;
    int margin = 10;
    
    int x = screenWidth - windowWidth - margin;
    int y = margin;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 
        L"FocusLockOverlayClass", 
        L"Focus Lock Status", 
        WS_POPUP, 
        x, y, 
        windowWidth, windowHeight, 
        NULL, NULL, hInstance, NULL
    );

    if (hWnd) {
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, GetWindowLongPtr(hWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA); 
        ShowWindow(hWnd, SW_HIDE); 
    }
    
    return hWnd;
}

// Procedimento da Janela de Overlay
LRESULT CALLBACK OverlayWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rect;
            GetClientRect(hWnd, &rect);
            
            HBRUSH hBrushBkg = CreateSolidBrush(g_crBkgColor);
            FillRect(hdc, &rect, hBrushBkg);
            DeleteObject(hBrushBkg);

            SetBkMode(hdc, TRANSPARENT); 
            SetTextColor(hdc, g_crStatusColor); 
            
            HFONT hFont = CreateFontW(
                20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
            );
            
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            DrawTextW(hdc, g_szStatusText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
            
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == IDT_STATUS_HIDE) {
                KillTimer(hWnd, IDT_STATUS_HIDE); 
                ShowWindow(hWnd, SW_HIDE);         
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            KillTimer(hWnd, IDT_STATUS_HIDE); 
            break;
            
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


// Gerencia o display da Janela Principal (GUI)
void UpdateMainDisplay() {
    if (g_bFocusLockActive) {
        wcsncpy(g_szStatusText, L"BLOQUEIO ATIVADO (F9)", 31);
        g_crStatusColor = RGB(0, 200, 0); 
    } else {
        wcsncpy(g_szStatusText, L"DESATIVADO (F9)", 31);
        g_crStatusColor = RGB(255, 100, 0); 
    }
    g_szStatusText[31] = L'\0';
    
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

// Gerencia o display da Janela de Overlay (Notificação de 2 segundos)
void UpdateOverlayStatus(const WCHAR* status, COLORREF color) {
    wcsncpy(g_szStatusText, status, 31);
    g_szStatusText[31] = L'\0';
    g_crStatusColor = color; 

    ShowWindow(g_hOverlayWnd, SW_SHOWNA);
    InvalidateRect(g_hOverlayWnd, NULL, TRUE);

    KillTimer(g_hOverlayWnd, IDT_STATUS_HIDE);

    SetTimer(
        g_hOverlayWnd,          
        IDT_STATUS_HIDE,        
        OVERLAY_DURATION,       
        (TIMERPROC) NULL        
    );
}

// --- Procedimento da Janela Principal ---

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        
        case WM_CREATE:
            SetTimer(hWnd, IDT_FOCUS_ENFORCEMENT, 50, NULL);
            break;

        case WM_TIMER:
            if (wParam == IDT_FOCUS_ENFORCEMENT) {
                // LÓGICA DE FOCO: SÓ FORÇA SE F9 (Bloqueio) E F10 (Contínuo) ESTIVEREM ATIVOS
                if (g_bFocusLockActive && g_hTargetWnd != NULL && g_bContinuousFocusMode) { 
                    HWND g_hCurrentFocus = GetForegroundWindow();

                    if (g_hCurrentFocus != g_hTargetWnd) {
                        
                        // ✅ CORREÇÃO: Tenta restaurar a janela se estiver minimizada
                        if (IsIconic(g_hTargetWnd)) {
                            ShowWindow(g_hTargetWnd, SW_RESTORE);
                        }
                        
                        // Força o foco
                        DWORD dwTargetThread = GetWindowThreadProcessId(g_hTargetWnd, NULL);
                        DWORD dwCurrentThread = GetCurrentThreadId(); 

                        AttachThreadInput(dwCurrentThread, dwTargetThread, TRUE);
                        SetForegroundWindow(g_hTargetWnd);
                        SetFocus(g_hTargetWnd); 
                        AttachThreadInput(dwCurrentThread, dwTargetThread, FALSE);
                    }
                }
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            
            HBRUSH hBrushBkg = CreateSolidBrush(g_crBkgColor);
            FillRect(hdc, &clientRect, hBrushBkg);
            DeleteObject(hBrushBkg);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_crTextColor);
            
            // Definição das fontes
            HFONT hFontTitle = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HFONT hFontStatus = CreateFontW(40, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HFONT hFontNormal = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HFONT hFontMode = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            // --- Desenho na Janela (Usando DrawTextW e L"...") ---
            
            RECT rectTitle = {10, 10, clientRect.right, clientRect.bottom};
            SelectObject(hdc, hFontTitle);
            DrawTextW(hdc, L"🛡️ Focus Locker", -1, &rectTitle, DT_LEFT | DT_TOP);
            
            RECT rectStatus = {10, 50, clientRect.right - 10, clientRect.bottom};
            SetTextColor(hdc, g_crStatusColor);
            SelectObject(hdc, hFontStatus);
            DrawTextW(hdc, g_szStatusText, -1, &rectStatus, DT_LEFT | DT_TOP);
            
            RECT rectMode = {10, 100, clientRect.right - 10, clientRect.bottom};
            SelectObject(hdc, hFontMode);
            SetTextColor(hdc, g_bContinuousFocusMode ? RGB(0, 255, 255) : RGB(150, 150, 150)); 
            DrawTextW(hdc, g_bContinuousFocusMode ? L"MODO CONTÍNUO (F10): ATIVO" : L"MODO CONTÍNUO (F10): INATIVO", -1, &rectMode, DT_LEFT | DT_TOP);
            
            RECT rectTarget = {10, 130, clientRect.right - 10, clientRect.bottom};
            SetTextColor(hdc, g_crTextColor); 
            SelectObject(hdc, hFontNormal);
            DrawTextW(hdc, L"Alvo Atual:", -1, &rectTarget, DT_LEFT | DT_TOP);
            
            RECT rectAppName = {10, 150, clientRect.right - 10, clientRect.bottom};
            DrawTextW(hdc, g_szTargetAppName, -1, &rectAppName, DT_LEFT | DT_TOP);
            
            RECT rectInst = {10, clientRect.bottom - 40, clientRect.right - 10, clientRect.bottom - 10};
            DrawTextW(hdc, L"F9: ATIVAR/DESATIVAR foco | F10: ATIVAR/DESATIVAR MODO CONTÍNUO", -1, &rectInst, DT_LEFT | DT_BOTTOM);
            
            DeleteObject(hFontTitle);
            DeleteObject(hFontStatus);
            DeleteObject(hFontNormal);
            DeleteObject(hFontMode);
            
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hWnd, IDT_FOCUS_ENFORCEMENT);
            UnhookWindowsHookEx(g_hMouseHook); 
            UnhookWindowsHookEx(g_hKeyboardHook); 
            DestroyWindow(g_hOverlayWnd); 
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}


// --- Hooks Globais ---

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_LBUTTONDOWN) { 
        MSLLHOOKSTRUCT *pMouseStruct = (MSLLHOOKSTRUCT *)lParam;

        POINT pt = {pMouseStruct->pt.x, pMouseStruct->pt.y};
        HWND hClickedWnd = WindowFromPoint(pt);
        HWND hRootWnd = GetAncestor(hClickedWnd, GA_ROOT);
        
        if (hRootWnd != g_hTargetWnd && hRootWnd != g_hMainWnd && hRootWnd != g_hOverlayWnd) { 
            g_hTargetWnd = hRootWnd;
            
            int len = GetWindowTextW(g_hTargetWnd, g_szTargetAppName, 256);
            
            if (len == 0) {
                 len = (int)SendMessageW(g_hTargetWnd, WM_GETTEXT, 256, (LPARAM)g_szTargetAppName);
            }

            if (len == 0) {
                 wcsncpy(g_szTargetAppName, L"Aplicativo sem título (Alvo definido)", 255);
            }
            g_szTargetAppName[255] = L'\0'; 

            UpdateMainDisplay();
            
            UpdateOverlayStatus(L"ALVO DEFINIDO", RGB(255, 255, 0)); 
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT *pKeyStruct = (KBDLLHOOKSTRUCT *)lParam;
        
        if (g_hTargetWnd != NULL) {
            
            // F9: ATIVA/DESATIVA Bloqueio de Foco (Controle mestre)
            if (pKeyStruct->vkCode == VK_F9) {
                g_bFocusLockActive = !g_bFocusLockActive; 
                UpdateMainDisplay();
                
                if (g_bFocusLockActive) {
                    if (g_bContinuousFocusMode) {
                        UpdateOverlayStatus(L"BLOQUEIO ATIVADO (CONTÍNUO)", RGB(0, 200, 0));
                    } else {
                        UpdateOverlayStatus(L"BLOQUEIO ATIVADO (MANUAL)", RGB(0, 200, 0));
                    }
                } else {
                    UpdateOverlayStatus(L"BLOQUEIO DESATIVADO", RGB(255, 100, 0));
                }
            } 
            
            // F10: ATIVA/DESATIVA MODO CONTÍNUO (Opção extra para foco forçado)
            else if (pKeyStruct->vkCode == VK_F10) {
                g_bContinuousFocusMode = !g_bContinuousFocusMode;
                UpdateMainDisplay();
                
                if (g_bContinuousFocusMode) {
                    UpdateOverlayStatus(L"MODO CONTÍNUO ATIVADO (F10)", RGB(0, 255, 255));
                } else {
                    UpdateOverlayStatus(L"MODO CONTÍNUO DESATIVADO (F10)", RGB(150, 150, 150));
                }
            }
        } else {
            if (pKeyStruct->vkCode == VK_F9 || pKeyStruct->vkCode == VK_F10) {
                 UpdateOverlayStatus(L"⚠️ ERRO: Defina um alvo primeiro!", RGB(255, 0, 0));
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}


// --- Função de Entrada Principal (WinMain) ---

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    
    g_hOverlayWnd = CreateOverlayWindow(hInstance);
    if (g_hOverlayWnd == NULL) {
        return 1;
    }

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) }; 
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FocusLockerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    if (!RegisterClassExW(&wc)) {
        return 1;
    }
    
    g_hMainWnd = CreateWindowExW(
        0, 
        L"FocusLockerClass", 
        L"Focus Locker", 
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, 
        CW_USEDEFAULT, CW_USEDEFAULT, 
        450, 250, 
        NULL, NULL, hInstance, NULL
    );

    if (g_hMainWnd == NULL) {
        DestroyWindow(g_hOverlayWnd);
        return 1;
    }
    
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hInstance, 0);
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, hInstance, 0);
    
    if (g_hMouseHook == NULL || g_hKeyboardHook == NULL) {
        DestroyWindow(g_hMainWnd);
        DestroyWindow(g_hOverlayWnd);
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}