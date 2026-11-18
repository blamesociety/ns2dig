// alsarc_gui.c - ns2dig: Win32 front end for ns2dig.py: extract, browse and search.
// Build (zig): zig cc -target x86_64-windows-gnu -O2 -municode -Wl,--subsystem,windows alsarc_gui.c app.rc
//              -o ns2dig.exe -lcomdlg32 -lshell32 -lshlwapi -lole32 -lcomctl32 -lgdi32 -luser32 -lkernel32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>
#include "ui_theme.h"
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define PATHBUF 2048

enum {
    ID_ROMFS = 101, ID_ROMFS_BTN, ID_OUT, ID_OUT_BTN, ID_SCRIPT, ID_SCRIPT_BTN,
    ID_EXTRACT, ID_LOAD, ID_SEARCH_EDIT, ID_SEARCH, ID_STOP, ID_NEXT, ID_CASE, ID_ALLFILES, ID_OPENDIR, ID_COPYRES, ID_SCOPE, ID_SCANLOG,
    ID_STATUS, ID_TREE, ID_RESULTS, ID_RESULTS_LBL, ID_VIEW, ID_VIEW_LBL, ID_GOPATH, ID_GOBTN, ID_VOX3D, ID_PLAYSND, ID_TYPES, ID_ZIPSTOP, ID_VIEWTEX, ID_VIEWLYT, ID_VIEWFNT, ID_VIEWLGT, ID_VIEWEVT, ID_OPENROM, ID_BOOKMARKS, ID_BULKZIP, ID_BMSTAR
};
#define WM_APP_LOG      (WM_APP + 1)
#define WM_APP_DONE     (WM_APP + 2)
#define WM_APP_RESULTS  (WM_APP + 3)
#define WM_APP_SSTAT    (WM_APP + 4)
#define WM_APP_SDONE    (WM_APP + 5)
#define WM_APP_PROGRESS (WM_APP + 6)
#define WM_APP_STOTAL   (WM_APP + 7)    // search: total files to look at (wp = total, lp = gen)
#define WM_APP_ZIPPROG  (WM_APP + 8)    // zip: wp = files done, lp = total
#define WM_APP_ZIPDONE  (WM_APP + 9)    // zip: lp = ZipJob* (finished)
#define WM_APP_RECENT   (WM_APP + 10)   // a recent search was picked from the list
#ifndef CB_SETCUEBANNER
#define CB_SETCUEBANNER 0x1703
#endif
#define MAX_RECENT 25

static HWND g_wnd, g_romfs, g_out, g_extract, g_load, g_sedit, g_search, g_stop,
            g_next, g_case, g_status, g_tree, g_results, g_reslbl, g_view, g_viewlbl;
static HWND g_lbl[4];
static HWND g_allfiles, g_opendir, g_copyres, g_scanlog, g_openrom;
static HWND g_gopath, g_gobtn, g_vox3d, g_playsnd, g_viewtex, g_viewlyt, g_viewfnt, g_viewlgt, g_viewevt;   // also: 3D view button          // "go to path" box above the tree
static wchar_t g_lastTerm[512];          // term of the most recent search
static BOOL g_lastMatchCase = FALSE;
static HWND g_scope;
static HWND g_progress;                  // extraction progress bar
static HWND g_sprog;                     // search progress bar
static int g_searchTotal = 0;            // files the current search will look at (0 = still counting)
static BOOL g_resultsCached = FALSE;     // results list came from the search cache
static wchar_t g_cachedWhen[64];         // when the cached results were made
static volatile LONG g_zipping = 0, g_zipCancel = 0;
static volatile LONG g_workers = 0;      // background threads still running
static HANDLE g_zipThread;               // so closing the app can wait for it to stop
static HWND g_zipstop;                   // "Stop ZIP" button (shown while a ZIP is being made)
static BOOL g_recentOpen = FALSE, g_recentPicked = FALSE;   // search drop-down state
static void Layout(int w, int h);
static int UiIconForName(const wchar_t *name, BOOL isDir);
static void HdrSet(const wchar_t *path, BOOL isDir, const wchar_t *name, const wchar_t *tag);
static void UiMenuPrepare(HMENU m);
void UiSkinWindow(HWND top);
void UiStyleDropList(HWND combo);
BOOL UiSkinComboRow(DRAWITEMSTRUCT *di);
static void UiSetViewJson(BOOL on);
static void UiMenuFree(HMENU m);
static RECT g_sbRect;                     // status bar (bottom of the window)
static RECT g_sepRect;                    // line between the scan section and the search section
static HFONT g_titleFont, g_smallFont;   // file name in the header; small print
static BOOL g_uiDarkMenus;               // Windows draws dark menus for us
static void UpdateScanLogButton(void);
static void ZipTreePath(const wchar_t *path, BOOL isDir);
static void ZipSelectedResults(void);
static void StopZip(void);
static void StartSearch(BOOL force);
static void ShowDirTree(HTREEITEM it);
static void RootLabel(wchar_t *out, size_t n);
void Vox3DOpen(HWND owner, const wchar_t *partDir, const wchar_t *geomJson, const wchar_t *title, HFONT font, int dpi);
static BOOL VoxObjOutDir(const wchar_t *path, BOOL isDir, wchar_t *outDir, wchar_t *geomJson);
static void OpenVoxObj(const wchar_t *outDir, const wchar_t *geomJson);
static BOOL Open3DFor(const wchar_t *path, BOOL isDir, BOOL doOpen);
static BOOL IsPhiveOut(const wchar_t *path, BOOL isDir);
static BOOL IsTerrainHeightOut(const wchar_t *path, BOOL isDir);
static BOOL IsTerrainSceneOut(const wchar_t *path, BOOL isDir);
static BOOL IsTerrainLayerOut(const wchar_t *path);
static BOOL IsClothOut(const wchar_t *path, BOOL isDir);
static BOOL IsBfresModelOut(const wchar_t *path, BOOL isDir);
void SndPlayerOpen(HWND owner, const wchar_t *dumpFile, const wchar_t *title, HFONT font, int dpi);
void TexViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi);
void FontViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi);
void LgtViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi);
void EvtViewOpen(HWND owner, const wchar_t *dumpFile, const wchar_t *title, HFONT font, int dpi);
void VidViewOpen(HWND owner, const wchar_t *dumpFile, const wchar_t *title, HFONT font, int dpi);
void LytViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi);
static BOOL IsLytOut(const wchar_t *path, BOOL isDir);
static void OpenLyt(const wchar_t *outFile);
static BOOL IsTexOut(const wchar_t *path, BOOL isDir);
static BOOL IsFontOut(const wchar_t *path, BOOL isDir);
static BOOL IsLgtOut(const wchar_t *path, BOOL isDir);
static BOOL IsEvtOut(const wchar_t *path);
static void OpenEvt(const wchar_t *path);
static void OpenLgt(const wchar_t *path, BOOL isDir);
static void OpenFont(const wchar_t *outFile);
static void OpenTex(const wchar_t *outFile);
static BOOL IsSoundOut(const wchar_t *path);
static void OpenSound(const wchar_t *outFile);
static BOOL VoxPartOutDir(const wchar_t *path, BOOL isDir, wchar_t *outDir);
static void OpenVox3D(const wchar_t *outDir);
static void UpdateVox3DButton(void);
static void UpdateBmStar(void);
static void ScanLogPath(const wchar_t *scanDir, wchar_t *out, size_t n);
static void ScanNameOf(const wchar_t *scanRoot, wchar_t *out, size_t n);
static DWORD g_extractStart;
static HANDLE g_job;                     // job object holding Python (and the py launcher's child)
static wchar_t g_stopFile[MAX_PATH];     // created to ask the script to stop
static volatile LONG g_stopRequested = 0;
#define TIMER_OPEN_VIEWER 2                    // double-click: open the viewer once the click has finished
static wchar_t g_dblPath[PATHBUF];
#define TIMER_FORCE_STOP 1                     // "Only in selected folder" checkbox
static wchar_t g_lastScope[PATHBUF];     // folder (within scan) the last search covered; empty = whole scan
static wchar_t g_curDir[PATHBUF];        // folder currently shown as a text tree (empty if none)
static HTREEITEM g_dirItem = NULL;      // folder currently shown as a text tree
static BOOL g_suppressDirView = FALSE;
static HFONT g_font, g_mono;
static HFONT g_boldFont;                 // menu headings
static volatile LONG g_running = 0;      // extraction running
static volatile LONG g_searchGen = 0;    // bumps to cancel a search
static volatile LONG g_searchLowMem = 0; // the last search stopped early because the computer was running low on memory
static volatile LONG g_searching = 0;
static int g_split = 340, g_dragging = 0;   // g_split is in 96-DPI units
static int g_dpi = 96;
#define S(x) MulDiv((x), g_dpi, 96)

typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
typedef BOOL (WINAPI *PFN_SPIForDpi)(UINT, UINT, PVOID, UINT, UINT);

static int QueryDpi(HWND h) {
    PFN_GetDpiForWindow f = (PFN_GetDpiForWindow)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (f && h) { UINT d = f(h); if (d) return (int)d; }
    HDC dc = GetDC(NULL); int d = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc);
    return d ? d : 96;
}
static wchar_t g_root[PATHBUF];          // output folder currently shown in the tree
static wchar_t g_cur[PATHBUF];           // file currently in the viewer
static wchar_t g_ini[PATHBUF];
static wchar_t g_exeDir[PATHBUF];        // folder containing the EXE
static wchar_t g_scriptPath[PATHBUF];    // <exe dir>\ns2dig.py
static int g_resultCount = 0, g_resultFiles = 0;

/* ------------------------------------------------------------------ helpers */
/* ---- crash report: if the program crashes, write what and where to ns2dig_crash.txt next to the EXE
   (the addresses map back to the source with the build's .pdb) and say so, instead of vanishing silently ---- */
static const char *volatile g_stage = "starting";       // what the program was doing (startup steps, closing...)
static DWORD g_mainTid;
#ifndef BUILD_STAMP
#define BUILD_STAMP "unknown"
#endif
static void CrashAddr(char *o, size_t n, ULONG_PTR a) {
    HMODULE mod = NULL; wchar_t mp[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &mod) && mod) {
        GetModuleFileNameW(mod, mp, MAX_PATH);
        const wchar_t *b = wcsrchr(mp, L'\\');
        snprintf(o, n, "%ls+0x%llX", b ? b + 1 : mp, (unsigned long long)(a - (ULONG_PTR)mod));
    } else snprintf(o, n, "0x%llX", (unsigned long long)a);
}
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_EXECUTE_HANDLER;
    static char t[16384]; size_t k = 0;
    SYSTEMTIME st; GetLocalTime(&st);
    char where[400]; CrashAddr(where, sizeof where, (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress);
    k += snprintf(t + k, sizeof t - k,
                  "=== ns2dig crash %04d-%02d-%02d %02d:%02d:%02d ===\r\nbuild: %s\r\n"
                  "exception: 0x%08lX at %s\r\nthread: %s (id %lu)\r\nstage: %s\r\n",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, BUILD_STAMP,
                  ep->ExceptionRecord->ExceptionCode, where,
                  GetCurrentThreadId() == g_mainTid ? "main window" : "background", GetCurrentThreadId(), g_stage);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        k += snprintf(t + k, sizeof t - k, "access: %s 0x%llX\r\n", ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                      (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    void *fr[62]; USHORT nf = RtlCaptureStackBackTrace(0, 62, fr, NULL);
    k += snprintf(t + k, sizeof t - k, "call stack:\r\n");
    for (USHORT i = 0; i < nf && k < sizeof t - 600; i++) { CrashAddr(where, sizeof where, (ULONG_PTR)fr[i]); k += snprintf(t + k, sizeof t - k, "  %s\r\n", where); }
    k += snprintf(t + k, sizeof t - k, "\r\n");
    wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *bs = wcsrchr(path, L'\\'); if (bs) wcscpy(bs + 1, L"ns2dig_crash.txt");
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, t, (DWORD)k, &w, NULL); CloseHandle(h); }
    wchar_t msg[MAX_PATH + 300];
    swprintf(msg, MAX_PATH + 300, L"ns2dig has crashed (while: %hs).\n\nA report was saved to:\n%ls\n\nPlease send that file so the bug can be fixed.", g_stage, path);
    MessageBoxW(NULL, msg, L"ns2dig", MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void CrashGuardThread(void) { ULONG g = 64 * 1024; SetThreadStackGuarantee(&g); }   // room to report a stack overflow

static void *xalloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void xfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
static wchar_t *xwcsdup(const wchar_t *s) {
    size_t n = wcslen(s) + 1; wchar_t *d = (wchar_t *)xalloc(n * sizeof(wchar_t));
    if (d) memcpy(d, s, n * sizeof(wchar_t)); return d;
}
static void GetText(HWND h, wchar_t *buf, int n) { GetWindowTextW(h, buf, n); }

// \\?\ prefix so paths longer than 260 characters still work
static void LongPath(const wchar_t *in, wchar_t *out, size_t n) {
    if (wcsncmp(in, L"\\\\?\\", 4) == 0) swprintf(out, n, L"%ls", in);
    else if (wcsncmp(in, L"\\\\", 2) == 0) swprintf(out, n, L"\\\\?\\UNC\\%ls", in + 2);
    else swprintf(out, n, L"\\\\?\\%ls", in);
}

static BOOL EndsWithI(const wchar_t *s, const wchar_t *suf) {
    size_t a = wcslen(s), b = wcslen(suf);
    return a >= b && _wcsicmp(s + a - b, suf) == 0;
}

// UTF-8 -> wide, converting lone \n to \r\n for the edit control
static wchar_t *Utf8ToEditText(const char *s, int len) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    wchar_t *w = (wchar_t *)xalloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
    size_t extra = 0;
    for (int i = 0; i < n; i++) if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) extra++;
    if (!extra) return w;
    wchar_t *t = (wchar_t *)xalloc(((size_t)n + extra + 1) * sizeof(wchar_t));
    if (!t) { xfree(w); return NULL; }
    size_t j = 0;
    for (int i = 0; i < n; i++) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) t[j++] = L'\r';
        t[j++] = w[i];
    }
    xfree(w);
    return t;
}

// the Stop button shows while a search runs; "In File" (find next in the open file, F3) takes its place otherwise
static void StopOrNext(BOOL searching) {
    EnableWindow(g_stop, searching);
    ShowWindow(g_stop, searching ? SW_SHOW : SW_HIDE);
    ShowWindow(g_next, searching ? SW_HIDE : SW_SHOW);
}
static void SetStatus(const wchar_t *s) { SetWindowTextW(g_status, s); if (g_sbRect.bottom > g_sbRect.top) InvalidateRect(g_wnd, &g_sbRect, FALSE); }

static void CreateFonts(void) {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    PFN_SPIForDpi f = (PFN_SPIForDpi)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi");
    if (!(f && f(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, (UINT)g_dpi))) {
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    }
    if (g_boldFont) { DeleteObject(g_boldFont); g_boldFont = NULL; }
    if (g_font) DeleteObject(g_font);
    if (g_mono) DeleteObject(g_mono);
    if (g_titleFont) { DeleteObject(g_titleFont); g_titleFont = NULL; }
    if (g_smallFont) { DeleteObject(g_smallFont); g_smallFont = NULL; }
    // Windows 11's Segoe UI Variable when it's there, else the system's message font (Segoe UI)
    LOGFONTW base = ncm.lfMessageFont; base.lfQuality = CLEARTYPE_QUALITY;
    int px = -MulDiv(10, g_dpi, 72);
    g_font = UiFontTry(&base, L"Segoe UI Variable Text", px, FW_NORMAL);
    if (!g_font) { LOGFONTW lf0 = base; lf0.lfHeight = px; g_font = CreateFontIndirectW(&lf0); }
    g_titleFont = UiFontTry(&base, L"Segoe UI Variable Display", -MulDiv(15, g_dpi, 72), FW_SEMIBOLD);
    if (!g_titleFont) { LOGFONTW lt = base; lt.lfHeight = -MulDiv(15, g_dpi, 72); lt.lfWeight = FW_SEMIBOLD; g_titleFont = CreateFontIndirectW(&lt); }
    g_smallFont = UiFontTry(&base, L"Segoe UI Variable Small", -MulDiv(9, g_dpi, 72), FW_NORMAL);
    if (!g_smallFont) { LOGFONTW ls = base; ls.lfHeight = -MulDiv(9, g_dpi, 72); g_smallFont = CreateFontIndirectW(&ls); }
    // monospace for file contents: Cascadia Mono (Windows 11 / Terminal), else Consolas
    g_mono = UiFontTry(&base, L"Cascadia Mono", -MulDiv(10, g_dpi, 72), FW_NORMAL);
    if (!g_mono) { LOGFONTW lf = base; wcscpy(lf.lfFaceName, L"Consolas"); lf.lfHeight = -MulDiv(10, g_dpi, 72); g_mono = CreateFontIndirectW(&lf); }
}

static int g_rowH = 24;   // control row height in pixels, from font metrics

// width in pixels of a control's text in the UI font
static int TextW(const wchar_t *t) {
    HDC dc = GetDC(g_wnd);
    HGDIOBJ old = SelectObject(dc, g_font);
    SIZE sz = { 0 }; GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    SelectObject(dc, old); ReleaseDC(g_wnd, dc);
    return sz.cx;
}

static void MeasureRow(void) {
    HDC dc = GetDC(g_wnd);
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    SelectObject(dc, old); ReleaseDC(g_wnd, dc);
    g_rowH = tm.tmHeight + tm.tmExternalLeading + S(10);
    if (g_rowH < S(24)) g_rowH = S(24);
}

static void LoadFilterSettings(void);
static void UpdateTypesButton(void);
/* ------------------------------------------------------------------ settings */
static void LoadSettings(void) {
    wchar_t buf[PATHBUF];
    GetPrivateProfileStringW(L"paths", L"romfs", L"", buf, PATHBUF, g_ini); SetWindowTextW(g_romfs, buf);
    GetPrivateProfileStringW(L"paths", L"scan", L"", buf, PATHBUF, g_ini); SetWindowTextW(g_out, buf);
    int sp = GetPrivateProfileIntW(L"ui", L"split", 340, g_ini);
    if (sp >= 150 && sp <= 2000) g_split = sp;
    LoadFilterSettings();
}
static void SaveSettings(void) {
    wchar_t buf[PATHBUF];
    GetText(g_romfs, buf, PATHBUF); WritePrivateProfileStringW(L"paths", L"romfs", buf, g_ini);
    GetText(g_out, buf, PATHBUF);   WritePrivateProfileStringW(L"paths", L"scan", buf, g_ini);
    swprintf(buf, 32, L"%d", g_split); WritePrivateProfileStringW(L"ui", L"split", buf, g_ini);
}

/* ------------------------------------------------------------------ recent searches (search box drop-down) */
static wchar_t *g_recent[MAX_RECENT];
static BOOL g_recentMc[MAX_RECENT];                 // each recent search remembers Match case...
static wchar_t *g_recentScope[MAX_RECENT];          // ...and the folder it covered (within the scan, "/" separators; "" = whole scan)
static int g_nrecent = 0;
static BOOL g_ovrOn = FALSE, g_ovrMc = FALSE;       // StartSearch: use these settings instead of the check boxes
static wchar_t g_ovrScope[PATHBUF];

static int g_pickIdx = -1;                          // recent search last highlighted in the list (until the text is edited)
static void RecentExtra(int i, wchar_t *ex, size_t n) {   // "in folder/, match case" (drawn dim after the term in the list only)
    ex[0] = 0;
    if (i < 0 || i >= g_nrecent) return;
    if (g_recentScope[i][0]) swprintf(ex, n, L"in %ls/", g_recentScope[i]);
    if (g_recentMc[i]) wcsncat(ex, ex[0] ? L", match case" : L"match case", n - 1 - wcslen(ex));
}
static void FillRecentList(void) {
    wchar_t keep[512]; GetText(g_sedit, keep, 512);
    DWORD sel = (DWORD)SendMessageW(g_sedit, CB_GETEDITSEL, 0, 0);
    SendMessageW(g_sedit, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_nrecent; i++) SendMessageW(g_sedit, CB_ADDSTRING, 0, (LPARAM)g_recent[i]);
    g_pickIdx = -1;
    SetWindowTextW(g_sedit, keep);
    SendMessageW(g_sedit, CB_SETEDITSEL, 0, (LPARAM)sel);
}

static void SaveRecent(void) {
    WritePrivateProfileSectionW(L"recent", L"", g_ini);        // clear the old list
    for (int i = 0; i < g_nrecent; i++) {
        wchar_t k[16]; swprintf(k, 16, L"s%d", i);
        // wrap in quotes so leading/trailing spaces survive the INI file
        wchar_t v[PATHBUF + 8]; swprintf(v, 600, L"\"%ls\"", g_recent[i]);
        WritePrivateProfileStringW(L"recent", k, v, g_ini);
        swprintf(k, 16, L"m%d", i); WritePrivateProfileStringW(L"recent", k, g_recentMc[i] ? L"1" : L"0", g_ini);
        swprintf(k, 16, L"f%d", i); swprintf(v, PATHBUF + 8, L"\"%ls\"", g_recentScope[i]); WritePrivateProfileStringW(L"recent", k, v, g_ini);
    }
}

static void LoadRecent(void) {
    for (int i = 0; i < MAX_RECENT; i++) {
        wchar_t k[16], v[PATHBUF]; swprintf(k, 16, L"s%d", i);
        GetPrivateProfileStringW(L"recent", k, L"", v, 600, g_ini);
        if (!v[0]) break;
        g_recent[g_nrecent] = xwcsdup(v);
        swprintf(k, 16, L"m%d", i); g_recentMc[g_nrecent] = GetPrivateProfileIntW(L"recent", k, 0, g_ini) != 0;
        swprintf(k, 16, L"f%d", i); GetPrivateProfileStringW(L"recent", k, L"", v, PATHBUF, g_ini); g_recentScope[g_nrecent] = xwcsdup(v);
        g_nrecent++;
    }
    FillRecentList();
}

static int FindRecent(const wchar_t *term, BOOL mc, const wchar_t *scope) {
    for (int i = 0; i < g_nrecent; i++)
        if (!wcscmp(g_recent[i], term) && g_recentMc[i] == mc && !_wcsicmp(g_recentScope[i], scope)) return i;
    return -1;
}
static void DropRecent(int i) {
    xfree(g_recent[i]); xfree(g_recentScope[i]);
    memmove(&g_recent[i], &g_recent[i + 1], (g_nrecent - i - 1) * sizeof(wchar_t *));
    memmove(&g_recentScope[i], &g_recentScope[i + 1], (g_nrecent - i - 1) * sizeof(wchar_t *));
    memmove(&g_recentMc[i], &g_recentMc[i + 1], (g_nrecent - i - 1) * sizeof(BOOL));
    g_nrecent--;
}
// move (or add) a search to the top of the recent list
static void AddRecent(const wchar_t *term, BOOL mc, const wchar_t *scope) {
    int at = FindRecent(term, mc, scope);
    if (at == 0) return;
    if (at > 0) DropRecent(at);
    else if (g_nrecent == MAX_RECENT) DropRecent(g_nrecent - 1);
    memmove(&g_recent[1], &g_recent[0], g_nrecent * sizeof(wchar_t *));
    memmove(&g_recentScope[1], &g_recentScope[0], g_nrecent * sizeof(wchar_t *));
    memmove(&g_recentMc[1], &g_recentMc[0], g_nrecent * sizeof(BOOL));
    g_nrecent++;
    g_recent[0] = xwcsdup(term); g_recentScope[0] = xwcsdup(scope); g_recentMc[0] = mc;
    FillRecentList();
    SaveRecent();
}

/* ------------------------------------------------------------------ scans (<exe dir>\output\<name>) */
static void OutputRoot(wchar_t *out, size_t n) { swprintf(out, n, L"%ls\\output", g_exeDir); }

// trims the name in place; returns an error message or NULL if it is a usable folder name
static const wchar_t *CheckScanName(wchar_t *name) {
    size_t l = wcslen(name);
    while (l && (name[l - 1] == L' ' || name[l - 1] == L'.')) name[--l] = 0;
    wchar_t *p = name; while (*p == L' ') p++;
    if (p != name) memmove(name, p, (wcslen(p) + 1) * sizeof(wchar_t));
    if (!name[0]) return L"Type a scan name (the subfolder to save this scan in).";
    if (wcspbrk(name, L"\\/:*?\"<>|")) return L"The scan name can't contain any of these characters:  \\ / : * ? \" < > |";
    return NULL;
}

static void ScanDir(const wchar_t *name, wchar_t *out, size_t n) { swprintf(out, n, L"%ls\\output\\%ls", g_exeDir, name); }

static void RefreshScans(void) {
    wchar_t keep[PATHBUF]; GetText(g_out, keep, PATHBUF);
    SendMessageW(g_out, CB_RESETCONTENT, 0, 0);
    wchar_t pat[PATHBUF + 8]; OutputRoot(pat, PATHBUF); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..") &&
                wcsncmp(fd.cFileName, L"_deleting_", 10))
                SendMessageW(g_out, CB_ADDSTRING, 0, (LPARAM)fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetWindowTextW(g_out, keep);
}

// suggest a scan name from the RomFS path: its folder name, or the parent's if it is just "romfs"
static void SuggestScanName(const wchar_t *path) {
    wchar_t tmp[PATHBUF]; swprintf(tmp, PATHBUF, L"%ls", path);
    size_t l = wcslen(tmp); while (l > 3 && tmp[l - 1] == L'\\') tmp[--l] = 0;
    wchar_t *last = wcsrchr(tmp, L'\\'); const wchar_t *name = last ? last + 1 : tmp;
    wchar_t sug[PATHBUF];
    if (!_wcsicmp(name, L"romfs") && last) {
        *last = 0; wchar_t *prev = wcsrchr(tmp, L'\\');
        swprintf(sug, PATHBUF, L"%ls", prev ? prev + 1 : tmp);
    } else swprintf(sug, PATHBUF, L"%ls", name);
    for (wchar_t *q = sug; *q; q++) if (wcschr(L"\\/:*?\"<>|", *q)) *q = L'_';
    if (sug[0]) SetWindowTextW(g_out, sug);
}

/* ------------------------------------------------------------------ viewer */
static void AppendView(const wchar_t *s) {
    int end = GetWindowTextLengthW(g_view);
    SendMessageW(g_view, EM_SETSEL, end, end);
    SendMessageW(g_view, EM_REPLACESEL, FALSE, (LPARAM)s);
}

static void ShowFile(const wchar_t *path) {
    wchar_t lp[PATHBUF + 8];
    LongPath(path, lp, PATHBUF + 8);
    HANDLE h = CreateFileW(lp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    const wchar_t *rel = path;
    size_t rl = wcslen(g_root);
    if (rl && _wcsnicmp(path, g_root, rl) == 0) rel = path + rl + (path[rl] == L'\\');
    if (h == INVALID_HANDLE_VALUE) { SetWindowTextW(g_view, L"(could not open file)"); return; }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 200LL * 1024 * 1024) {
        CloseHandle(h); SetWindowTextW(g_view, L"(file is over 200 MB - open it in a text editor instead)"); return;
    }
    char *buf = (char *)xalloc((size_t)sz.QuadPart + 1);
    DWORD got = 0;
    if (buf) ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL);
    CloseHandle(h);
    wchar_t *w = buf ? Utf8ToEditText(buf, (int)got) : NULL;
    xfree(buf);
    SetWindowTextW(g_view, w ? w : L"");
    { const wchar_t *q = w; while (q && (*q == L' ' || *q == L'\r' || *q == L'\n' || *q == L'\t' || *q == 0xFEFF)) q++;
      UiSetViewJson(q && (*q == L'{' || *q == L'[')); }
    xfree(w);
    swprintf(g_cur, PATHBUF, L"%ls", path);
    g_curDir[0] = 0;
    EnableWindow(g_opendir, TRUE); EnableWindow(g_openrom, TRUE);
    wchar_t lbl[PATHBUF + 32];
    swprintf(lbl, PATHBUF + 32, L"%lld bytes of JSON", (long long)sz.QuadPart);
    SetWindowTextW(g_gopath, rel);
    SetWindowTextW(g_viewlbl, lbl);
    UpdateVox3DButton();
    HdrSet(path, FALSE, NULL, NULL);
}

// case-insensitive (or not) wide substring search
static BOOL WSegAtW(const wchar_t *s, const wchar_t *seg, size_t sl, BOOL mc, const wchar_t **end) {
    for (size_t k = 0; k < sl; k++, s++) {
        if (!*s || *s == L'\r' || *s == L'\n') return FALSE;
        if (seg[k] == L'?') continue;
        if (mc ? *s != seg[k] : towlower(*s) != towlower(seg[k])) return FALSE;
    }
    *end = s; return TRUE;
}
// wildcard find in the viewer text (matches stay within one line). Returns the start, *len = match length.
static const wchar_t *FindWild(const wchar_t *hay, const wchar_t *pat, BOOL mc, int *len) {
    const wchar_t *first = pat; while (*first == L'*') first++;
    if (!*first) return NULL;
    size_t fl = 0; while (first[fl] && first[fl] != L'*') fl++;
    for (const wchar_t *st = hay; *st; st++) {
        const wchar_t *e;
        if (!WSegAtW(st, first, fl, mc, &e)) continue;
        const wchar_t *p = e, *seg = first + fl; BOOL ok = TRUE;
        while (*seg) {
            if (*seg == L'*') { seg++; continue; }
            size_t sl = 0; while (seg[sl] && seg[sl] != L'*') sl++;
            BOOL found = FALSE;
            for (const wchar_t *q = p; *q && *q != L'\r' && *q != L'\n'; q++) if (WSegAtW(q, seg, sl, mc, &e)) { found = TRUE; break; }
            if (!found) { ok = FALSE; break; }
            p = e; seg += sl;
        }
        if (ok) { *len = (int)(p - st); return st; }
    }
    return NULL;
}
static BOOL HasWildW(const wchar_t *t) { return wcspbrk(t, L"*?") != NULL; }
static const wchar_t *FindW(const wchar_t *hay, const wchar_t *needle, BOOL mc) {
    size_t n = wcslen(needle);
    if (!n) return NULL;
    for (const wchar_t *p = hay; *p; p++) {
        size_t i = 0;
        if (mc) { while (i < n && p[i] == needle[i]) i++; }
        else    { while (i < n && p[i] && towlower(p[i]) == towlower(needle[i])) i++; }
        if (i == n) return p;
    }
    return NULL;
}

static void ScrollToLine(int line) {
    int first = (int)SendMessageW(g_view, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(g_view, EM_LINESCROLL, 0, line - first - 5);
}

// select the first occurrence of the search term on a given line (line < 0: just top)
static void HighlightLine(int line) {
    wchar_t term[512]; GetText(g_sedit, term, 512);
    BOOL mc = SendMessageW(g_case, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (line < 0) { SendMessageW(g_view, EM_SETSEL, 0, 0); SendMessageW(g_view, EM_SCROLLCARET, 0, 0); return; }
    int idx = (int)SendMessageW(g_view, EM_LINEINDEX, line, 0);
    if (idx < 0) return;
    int len = (int)SendMessageW(g_view, EM_LINELENGTH, idx, 0);
    wchar_t *lt = (wchar_t *)xalloc(((size_t)len + 8) * sizeof(wchar_t));
    if (!lt) return;
    *(WORD *)lt = (WORD)(len + 1);
    int got = (int)SendMessageW(g_view, EM_GETLINE, line, (LPARAM)lt);
    lt[got] = 0;
    int ml = (int)wcslen(term);
    const wchar_t *f = HasWildW(term) ? FindWild(lt, term, mc, &ml) : FindW(lt, term, mc);
    int s = idx + (f ? (int)(f - lt) : 0), e = f ? s + ml : idx + len;
    xfree(lt);
    SendMessageW(g_view, EM_SETSEL, s, e);
    ScrollToLine(line);
}

// F3 / "Next in file": find next occurrence after the current selection, wrapping
static void FindNextInView(void) {
    wchar_t term[512]; GetText(g_sedit, term, 512);
    if (!term[0]) { SetFocus(g_sedit); return; }
    BOOL mc = SendMessageW(g_case, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int n = GetWindowTextLengthW(g_view);
    wchar_t *t = (wchar_t *)xalloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!t) return;
    GetWindowTextW(g_view, t, n + 1);
    DWORD ss = 0, se = 0;
    SendMessageW(g_view, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
    size_t from = (se > ss) ? ss + 1 : se;
    if (from > (size_t)n) from = 0;
    int ml = (int)wcslen(term);
    BOOL wild = HasWildW(term);
    const wchar_t *f = wild ? FindWild(t + from, term, mc, &ml) : FindW(t + from, term, mc);
    if (!f) f = wild ? FindWild(t, term, mc, &ml) : FindW(t, term, mc);
    if (f) {
        int s = (int)(f - t);
        SendMessageW(g_view, EM_SETSEL, s, s + ml);
        ScrollToLine((int)SendMessageW(g_view, EM_LINEFROMCHAR, s, 0));
    } else MessageBeep(MB_ICONASTERISK);
    xfree(t);
}

/* ------------------------------------------------------------------ file-type filter: types and the type index */
// A file's type comes from its name: the extension (".zs" ignored), or "voxel terrain" for the files of a terrain part.
// The type index records, for every folder of the scan, which types are anywhere below it, so folders without any
// of the chosen types can be hidden. Built in the background, cached in search_cache\<scan>\types.idx.
#define MAXTYPES 256
#define TYPELEN 40
typedef struct {
    wchar_t root[PATHBUF];
    int nt; wchar_t name[MAXTYPES][TYPELEN]; unsigned cnt[MAXTYPES];
    size_t cap, nd; wchar_t **key; uint64_t (*mask)[4];
    unsigned files; DWORD lastProg;
} TypeIdx;
static TypeIdx *g_idx;                       // index for g_root (NULL = none yet)
static volatile LONG g_idxBuilding, g_idxCancel;
static BOOL g_filterOn;
static wchar_t g_sel[MAXTYPES][TYPELEN]; static int g_nsel;
static uint64_t g_selMask[4];                // g_sel as bits of g_idx
static HWND g_typesbtn, g_typesDlg;
static HWND g_bmBtn, g_bmStar, g_bulkBtn;   // "Bookmarks" menu button; star (bookmark what is shown); "Bulk ZIP"
static BOOL g_bmOn;                      // what is shown is bookmarked
static void CacheDirFor(const wchar_t *scanName, wchar_t *out, size_t n);

static void TypeOfName(const wchar_t *name, wchar_t *out) {
    wchar_t n[MAX_PATH * 2]; swprintf(n, MAX_PATH * 2, L"%ls", name); _wcslwr(n);
    size_t l = wcslen(n);
    if (l > 5 && !wcscmp(n + l - 5, L".json")) n[l -= 5] = 0;
    static const wchar_t *vt[] = { L"info.bin", L"vhead.bin", L"vbody.bin", L"ehead.bin", L"ebody.bin", L"materialtable.bin" };
    for (int i = 0; i < 6; i++) if (!wcscmp(n, vt[i])) { wcscpy(out, L"voxel terrain"); return; }
    if (l > 3 && !wcscmp(n + l - 3, L".zs")) n[l -= 3] = 0;
    wchar_t *d = wcsrchr(n, L'.');
    if (d && !wcscmp(d, L".listing")) { wcscpy(out, L"listing"); return; }   // ".listing" files
    if (!d && (!wcscmp(n, L"main") || !wcscmp(n, L"rtld") || !wcscmp(n, L"sdk") ||          // exefs programs (NSO)
               !wcsncmp(n, L"subsdk", 6) || !wcsncmp(n, L"compat", 6))) { wcscpy(out, L"executable"); return; }
    if (!d || d == n || !d[1]) { wcscpy(out, L"(no extension)"); return; }
    swprintf(out, TYPELEN, L"%.39ls", d + 1);
}
static BOOL SelHas(const wchar_t *t) { for (int i = 0; i < g_nsel; i++) if (!wcscmp(g_sel[i], t)) return TRUE; return FALSE; }
static BOOL FileShown(const wchar_t *name) {
    if (!g_filterOn) return TRUE;
    wchar_t t[TYPELEN]; TypeOfName(name, t); return SelHas(t);
}

static size_t IdxHash(const wchar_t *s) { unsigned long long h = 1469598103934665603ULL; for (; *s; s++) { h ^= (unsigned)*s; h *= 1099511628211ULL; } return (size_t)h; }
static void IdxFree(TypeIdx *x) {
    if (!x) return;
    for (size_t i = 0; i < x->cap; i++) xfree(x->key[i]);
    xfree(x->key); xfree(x->mask); xfree(x);
}
static BOOL IdxPut(TypeIdx *x, const wchar_t *k, const uint64_t m[4]) {
    if ((x->nd + 1) * 2 > x->cap) {
        size_t nc = x->cap ? x->cap * 2 : 4096;
        wchar_t **nk = (wchar_t **)xalloc(nc * sizeof(wchar_t *));
        uint64_t (*nm)[4] = (uint64_t (*)[4])xalloc(nc * 32);
        if (!nk || !nm) { xfree(nk); xfree(nm); return FALSE; }
        for (size_t i = 0; i < x->cap; i++) if (x->key[i]) {
            size_t j = IdxHash(x->key[i]) & (nc - 1);
            while (nk[j]) j = (j + 1) & (nc - 1);
            nk[j] = x->key[i]; memcpy(nm[j], x->mask[i], 32);
        }
        xfree(x->key); xfree(x->mask); x->key = nk; x->mask = nm; x->cap = nc;
    }
    size_t i = IdxHash(k) & (x->cap - 1);
    while (x->key[i]) {
        if (!wcscmp(x->key[i], k)) { for (int b = 0; b < 4; b++) x->mask[i][b] |= m[b]; return TRUE; }
        i = (i + 1) & (x->cap - 1);
    }
    x->key[i] = xwcsdup(k); memcpy(x->mask[i], m, 32); x->nd++;
    return TRUE;
}
static const uint64_t *IdxGet(const TypeIdx *x, const wchar_t *k) {
    if (!x || !x->cap) return NULL;
    size_t i = IdxHash(k) & (x->cap - 1);
    while (x->key[i]) { if (!wcscmp(x->key[i], k)) return x->mask[i]; i = (i + 1) & (x->cap - 1); }
    return NULL;
}
static int IdxTypeId(TypeIdx *x, const wchar_t *t, BOOL add) {
    for (int i = 0; i < x->nt; i++) if (!wcscmp(x->name[i], t)) return i;
    if (!add) return -1;
    if (x->nt == MAXTYPES - 1) { wcscpy(x->name[x->nt], L"(other)"); return x->nt++; }
    if (x->nt >= MAXTYPES) return MAXTYPES - 1;
    swprintf(x->name[x->nt], TYPELEN, L"%ls", t); return x->nt++;
}
static void ComputeSelMask(void) {
    memset(g_selMask, 0, sizeof g_selMask);
    if (!g_idx) return;
    for (int i = 0; i < g_nsel; i++) { int id = IdxTypeId(g_idx, g_sel[i], FALSE); if (id >= 0) g_selMask[id >> 6] |= 1ULL << (id & 63); }
}
// folder (full path) has any chosen type below it? (unknown folders are shown)
static BOOL DirShown(const wchar_t *full) {
    if (!g_filterOn || !g_idx) return TRUE;
    size_t rl = wcslen(g_root);
    if (_wcsnicmp(full, g_root, rl)) return TRUE;
    wchar_t k[PATHBUF]; swprintf(k, PATHBUF, L"%ls", full + rl); _wcslwr(k);
    const uint64_t *m = IdxGet(g_idx, k);
    if (!m) return TRUE;
    return ((m[0] & g_selMask[0]) | (m[1] & g_selMask[1]) | (m[2] & g_selMask[2]) | (m[3] & g_selMask[3])) != 0;
}

#define WM_APP_TYPEIDX  (WM_APP + 11)   // lp = TypeIdx* (or NULL if cancelled)
#define WM_APP_TYPEPROG (WM_APP + 12)   // wp = files indexed so far
static void IdxWalk(TypeIdx *x, const wchar_t *dir, size_t rootLen, uint64_t out[4]) {
    uint64_t m[4] = { 0, 0, 0, 0 };
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (g_idxCancel) break;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                wchar_t sub[PATHBUF]; swprintf(sub, PATHBUF, L"%ls\\%ls", dir, fd.cFileName);
                IdxWalk(x, sub, rootLen, m);
            } else if (EndsWithI(fd.cFileName, L".json")) {
                wchar_t t[TYPELEN]; TypeOfName(fd.cFileName, t);
                int id = IdxTypeId(x, t, TRUE);
                m[id >> 6] |= 1ULL << (id & 63); x->cnt[id]++; x->files++;
                DWORD now = GetTickCount();
                if (now - x->lastProg > 250) { x->lastProg = now; PostMessageW(g_wnd, WM_APP_TYPEPROG, x->files, 0); }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    wchar_t k[PATHBUF]; swprintf(k, PATHBUF, L"%ls", dir + rootLen); _wcslwr(k);
    IdxPut(x, k, m);
    for (int b = 0; b < 4; b++) out[b] |= m[b];
}
static DWORD IdxWorkerBody(LPVOID p);
static DWORD WINAPI IdxWorker(LPVOID p) {          // counted, so closing the app can wait for it to finish
    InterlockedIncrement(&g_workers); CrashGuardThread();
    DWORD r = IdxWorkerBody(p);
    InterlockedDecrement(&g_workers);
    return r;
}
static DWORD IdxWorkerBody(LPVOID p) {
    TypeIdx *x = (TypeIdx *)p; uint64_t m[4] = { 0, 0, 0, 0 };
    IdxWalk(x, x->root, wcslen(x->root), m);
    if (g_idxCancel) { IdxFree(x); x = NULL; }
    PostMessageW(g_wnd, WM_APP_TYPEIDX, 0, (LPARAM)x);
    return 0;
}
static void StartTypeIndex(void) {
    if (g_idxBuilding || !g_root[0]) return;
    TypeIdx *x = (TypeIdx *)xalloc(sizeof(TypeIdx));
    if (!x) return;
    swprintf(x->root, PATHBUF, L"%ls", g_root);
    g_idxCancel = 0; g_idxBuilding = 1;
    HANDLE t = CreateThread(NULL, 0, IdxWorker, x, 0, NULL);
    if (t) CloseHandle(t); else { g_idxBuilding = 0; xfree(x); }
}
static void IdxFile(const wchar_t *root, wchar_t *out, size_t n) {
    wchar_t scan[PATHBUF], dir[PATHBUF]; ScanNameOf(root, scan, PATHBUF); CacheDirFor(scan, dir, PATHBUF);
    swprintf(out, n, L"%ls\\types.idx", dir);
}
static void IdxSave(const TypeIdx *x) {
    wchar_t f[PATHBUF + 32]; IdxFile(x->root, f, PATHBUF + 32);
    wchar_t d[PATHBUF + 32]; swprintf(d, PATHBUF + 32, L"%ls", f); *wcsrchr(d, L'\\') = 0;
    { wchar_t up[PATHBUF + 32]; swprintf(up, PATHBUF + 32, L"%ls", d); *wcsrchr(up, L'\\') = 0; CreateDirectoryW(up, NULL); }
    CreateDirectoryW(d, NULL);
    HANDLE h = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w; DWORD hd[3] = { 0x33584954 /* TIX3 */, (DWORD)x->nt, (DWORD)x->nd };
    WriteFile(h, hd, 12, &w, NULL);
    WriteFile(h, x->name, sizeof(x->name[0]) * x->nt, &w, NULL);
    WriteFile(h, x->cnt, 4 * x->nt, &w, NULL);
    for (size_t i = 0; i < x->cap; i++) if (x->key[i]) {
        unsigned short l = (unsigned short)wcslen(x->key[i]);
        WriteFile(h, &l, 2, &w, NULL); WriteFile(h, x->key[i], l * 2, &w, NULL); WriteFile(h, x->mask[i], 32, &w, NULL);
    }
    CloseHandle(h);
}
static TypeIdx *IdxLoad(const wchar_t *root) {
    wchar_t f[PATHBUF + 32]; IdxFile(root, f, PATHBUF + 32);
    HANDLE h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    unsigned char *b = sz.QuadPart > 12 && sz.QuadPart < 512LL * 1024 * 1024 ? (unsigned char *)xalloc((size_t)sz.QuadPart) : NULL;
    DWORD got = 0;
    BOOL ok = b && ReadFile(h, b, (DWORD)sz.QuadPart, &got, NULL) && got == (DWORD)sz.QuadPart;
    CloseHandle(h);
    if (!ok) { xfree(b); return NULL; }
    TypeIdx *x = (TypeIdx *)xalloc(sizeof(TypeIdx));
    DWORD *hd = (DWORD *)b; size_t p = 12, n = got;
    if (!x || hd[0] != 0x33584954 || hd[1] > MAXTYPES) { xfree(b); xfree(x); return NULL; }
    swprintf(x->root, PATHBUF, L"%ls", root);
    x->nt = (int)hd[1];
    size_t nb = sizeof(x->name[0]) * x->nt;
    if (p + nb + 4 * x->nt > n) { xfree(b); xfree(x); return NULL; }
    memcpy(x->name, b + p, nb); p += nb; memcpy(x->cnt, b + p, 4 * x->nt); p += 4 * x->nt;
    for (int i = 0; i < x->nt; i++) { x->name[i][TYPELEN - 1] = 0; x->files += x->cnt[i]; }
    for (DWORD i = 0; i < hd[2]; i++) {
        if (p + 2 > n) break;
        unsigned short l = *(unsigned short *)(b + p); p += 2;
        if (l >= PATHBUF || p + l * 2 + 32 > n) break;
        wchar_t k[PATHBUF]; memcpy(k, b + p, l * 2); k[l] = 0; p += l * 2;
        uint64_t m[4]; memcpy(m, b + p, 32); p += 32;
        IdxPut(x, k, m);
    }
    DWORD want = hd[2];                  // read it before freeing b (hd points into b)
    xfree(b);
    if (x->nd != want) { IdxFree(x); return NULL; }
    return x;
}
// call when g_root may have changed (scan opened / re-extracted)
static void TypeIndexForRoot(void) {
    if (g_idx && _wcsicmp(g_idx->root, g_root)) { IdxFree(g_idx); g_idx = NULL; }
    if (!g_idx && g_root[0]) g_idx = IdxLoad(g_root);
    if (g_idxBuilding) g_idxCancel = 1;          // an index for another scan; the result is dropped
    ComputeSelMask();
    if (!g_idx && g_filterOn && !g_idxBuilding) StartTypeIndex();
}
static void LoadFilterSettings(void) {
    wchar_t buf[MAXTYPES * TYPELEN];
    g_filterOn = GetPrivateProfileIntW(L"filter", L"on", 0, g_ini) != 0;
    GetPrivateProfileStringW(L"filter", L"types", L"", buf, MAXTYPES * TYPELEN, g_ini);
    g_nsel = 0;
    wchar_t *ctx = NULL;
    for (wchar_t *t = wcstok(buf, L"|", &ctx); t && g_nsel < MAXTYPES; t = wcstok(NULL, L"|", &ctx))
        swprintf(g_sel[g_nsel++], TYPELEN, L"%ls", t);
    if (!g_nsel) g_filterOn = FALSE;
}
static void SaveFilterSettings(void) {
    wchar_t buf[MAXTYPES * TYPELEN] = L"";
    for (int i = 0; i < g_nsel; i++) { if (i) wcscat(buf, L"|"); wcscat(buf, g_sel[i]); }
    WritePrivateProfileStringW(L"filter", L"on", g_filterOn ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"filter", L"types", buf, g_ini);
}
// "bars, bwav" (shortened)
static void FilterSummary(wchar_t *out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < g_nsel; i++) {
        if (wcslen(out) > 60) { wcsncat(out, L", \x2026", n - wcslen(out) - 1); break; }
        if (i) wcsncat(out, L", ", n - wcslen(out) - 1);
        wcsncat(out, g_sel[i], n - wcslen(out) - 1);
    }
}

/* ------------------------------------------------------------------ tree */
// lParam: 0 = file, 1 = folder not yet loaded, 2 = folder loaded
typedef struct { wchar_t *name; int dir; } Ent;
static int CmpEnt(const void *a, const void *b) {
    const Ent *x = (const Ent *)a, *y = (const Ent *)b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return StrCmpLogicalW(x->name, y->name);
}

static void ItemPath(HTREEITEM it, wchar_t *out, size_t n) {
    // collect names up to (excluding) the root, then join onto g_root
    wchar_t *parts[256]; int np = 0;
    HTREEITEM cur = it;
    while (cur && np < 256) {
        HTREEITEM par = TreeView_GetParent(g_tree, cur);
        if (!par) break;   // cur is the root
        wchar_t buf[MAX_PATH * 2];
        TVITEMW tv = { 0 }; tv.mask = TVIF_TEXT | TVIF_PARAM; tv.hItem = cur;
        tv.pszText = buf; tv.cchTextMax = MAX_PATH * 2;
        TreeView_GetItem(g_tree, &tv);
        if (tv.lParam == 0) wcscat(buf, L".json");
        parts[np++] = xwcsdup(buf);
        cur = par;
    }
    swprintf(out, n, L"%ls", g_root);
    for (int i = np - 1; i >= 0; i--) {
        wcsncat(out, L"\\", n - wcslen(out) - 1);
        wcsncat(out, parts[i], n - wcslen(out) - 1);
        xfree(parts[i]);
    }
}

static HTREEITEM AddItem(HTREEITEM parent, const wchar_t *text, int kind) {
    TVINSERTSTRUCTW ins = { 0 };
    ins.hParent = parent; ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    ins.item.pszText = (LPWSTR)text; ins.item.lParam = kind;
    ins.item.iImage = ins.item.iSelectedImage = parent == TVI_ROOT ? UI_IC_FOLDER : UiIconForName(text, kind != 0);
    ins.item.cChildren = kind ? 1 : 0;
    return (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
}

static void FillFolder(HTREEITEM it) {
    TVITEMW tv = { 0 }; tv.mask = TVIF_PARAM; tv.hItem = it;
    TreeView_GetItem(g_tree, &tv);
    if (tv.lParam != 1) return;
    tv.lParam = 2; TreeView_SetItem(g_tree, &tv);

    wchar_t dir[PATHBUF], pat[PATHBUF + 16];
    ItemPath(it, dir, PATHBUF);
    LongPath(dir, pat, PATHBUF + 8);
    wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    int cap = 256, n = 0;
    Ent *e = (Ent *)xalloc(cap * sizeof(Ent));
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!isdir && !EndsWithI(fd.cFileName, L".json")) continue;
            if (g_filterOn) {
                if (!isdir && !FileShown(fd.cFileName)) continue;
                if (isdir) { wchar_t sub[PATHBUF]; swprintf(sub, PATHBUF, L"%ls\\%ls", dir, fd.cFileName); if (!DirShown(sub)) continue; }
            }
            if (n == cap) { cap *= 2; Ent *ne = (Ent *)xalloc(cap * sizeof(Ent)); memcpy(ne, e, n * sizeof(Ent)); xfree(e); e = ne; }
            e[n].name = xwcsdup(fd.cFileName);
            if (!isdir) e[n].name[wcslen(e[n].name) - 5] = 0;   // hide .json
            e[n].dir = isdir; n++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    qsort(e, n, sizeof(Ent), CmpEnt);
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    for (int i = 0; i < n; i++) { AddItem(it, e[i].name, e[i].dir ? 1 : 0); xfree(e[i].name); }
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    if (!n) { tv.mask = TVIF_CHILDREN; tv.cChildren = 0; TreeView_SetItem(g_tree, &tv); }
    xfree(e);
}

typedef struct { wchar_t *p; size_t n, cap; } WBuf;
static void WAdd(WBuf *b, const wchar_t *s) {
    size_t l = wcslen(s);
    if (b->n + l + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 65536; while (nc < b->n + l + 1) nc *= 2;
        wchar_t *np = (wchar_t *)xalloc(nc * sizeof(wchar_t));
        if (!np) return;
        if (b->p) { memcpy(np, b->p, b->n * sizeof(wchar_t)); xfree(b->p); }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, s, l * sizeof(wchar_t)); b->n += l; b->p[b->n] = 0;
}


/* ------------------------------------------------------------------ copy path / explorer */
static void CopyText(const wchar_t *t) {
    size_t n = (wcslen(t) + 1) * sizeof(wchar_t);
    if (!OpenClipboard(g_wnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (h) {
        void *p = GlobalLock(h); memcpy(p, t, n); GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    }
    CloseClipboard();
}

// open Explorer: select the file, or open the folder
static void ShowInExplorer(const wchar_t *path, BOOL isDir) {
    wchar_t arg[PATHBUF + 32];
    if (isDir) ShellExecuteW(g_wnd, L"open", path, NULL, NULL, SW_SHOWNORMAL);
    else { swprintf(arg, PATHBUF + 32, L"/select,\"%ls\"", path); ShellExecuteW(g_wnd, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL); }
}

// path inside the scan, as the game names it: forward slashes, no ".json"
static void ScanRelPath(const wchar_t *path, BOOL isDir, wchar_t *out, size_t n) {
    size_t rl = wcslen(g_root);
    const wchar_t *rel = (rl && !_wcsnicmp(path, g_root, rl)) ? path + rl + (path[rl] == L'\\') : path;
    swprintf(out, n, L"%ls", rel);
    if (!isDir && EndsWithI(out, L".json")) out[wcslen(out) - 5] = 0;
    for (wchar_t *q = out; *q; q++) if (*q == L'\\') *q = L'/';
    if (!out[0]) { const wchar_t *bs = wcsrchr(g_root, L'\\'); swprintf(out, n, L"%ls", bs ? bs + 1 : g_root); }
}

static void CopyAllResults(void) {
    int n = (int)SendMessageW(g_results, LB_GETCOUNT, 0, 0);
    if (n <= 0) { SetStatus(L"No results to copy."); return; }
    WBuf b = { 0 };
    wchar_t head[1024], scan[PATHBUF];
    const wchar_t *bs = wcsrchr(g_root, L'\\'); swprintf(scan, PATHBUF, L"%ls", bs ? bs + 1 : g_root);
    wchar_t lbl[256]; GetWindowTextW(g_reslbl, lbl, 256);
    wchar_t scope[PATHBUF + 32] = L"";
    if (g_lastScope[0]) swprintf(scope, PATHBUF + 32, L", folder \"%ls/\"", g_lastScope);
    swprintf(head, 1024, L"Search \"%ls\" in scan \"%ls\"%ls%ls - %ls\r\n(format: path within scan  :line   matching text)\r\n\r\n",
             g_lastTerm, scan, scope, g_lastMatchCase ? L" (match case)" : L"", lbl);
    WAdd(&b, head);
    for (int i = 0; i < n; i++) {
        int len = (int)SendMessageW(g_results, LB_GETTEXTLEN, i, 0);
        if (len < 0) continue;
        wchar_t *t = (wchar_t *)xalloc(((size_t)len + 3) * sizeof(wchar_t));
        if (!t) continue;
        SendMessageW(g_results, LB_GETTEXT, i, (LPARAM)t);
        // path part uses forward slashes, like "Copy path within scan"
        for (wchar_t *q = t; *q && !(q[0] == L' ' && q[1] == L' '); q++) if (*q == L'\\') *q = L'/';
        wcscat(t, L"\r\n");
        WAdd(&b, t);
        xfree(t);
    }
    CopyText(b.p ? b.p : L"");
    xfree(b.p);
    wchar_t st[96]; swprintf(st, 96, L"Copied %d results.", n); SetStatus(st);
}

static int DumpOrigNoAsk(const wchar_t *outPath, BOOL isDir, wchar_t *out, BOOL *srcKnown);
static void AskDumpFolder(void);
static void TreeClearFast(void);
static void LockUi(BOOL lock);
static void UpdateVox3DButton(void);
// menu section headings (owner-drawn, not selectable)
static void AppendHeading(HMENU m, const wchar_t *text) {
    AppendMenuW(m, MF_OWNERDRAW | MF_DISABLED, 0, (LPCWSTR)text);
}
static void TreeMenuDump(HMENU m, const wchar_t *path, BOOL isDir, wchar_t *dumpPath, int *dumpKind, wchar_t *head, size_t hn, int nsel) {
    BOOL known = FALSE;
    *dumpKind = DumpOrigNoAsk(path, isDir, dumpPath, &known);
    const wchar_t *c = L"Copy full path", *e = L"Show in Explorer", *z = L"Create ZIP of this file...";
    wchar_t zt[600];
    swprintf(head, hn, L"ROM");
    if (*dumpKind == 2) {
        c = L"Copy full folder path"; e = L"Open folder in Explorer";
        if (g_filterOn) { wchar_t fs[400]; FilterSummary(fs, 400); swprintf(zt, 600, L"Create ZIP of this folder (only %ls files)...", fs); z = zt; }
        else z = L"Create ZIP of this folder...";
    } else if (*dumpKind == 3) {
        const wchar_t *bs = wcsrchr(dumpPath, L'\\');
        swprintf(head, hn, L"ROM  (inside %ls)", bs ? bs + 1 : dumpPath);
        c = L"Copy full path of the archive"; e = L"Show the archive in Explorer"; z = L"Create ZIP of the archive...";
    } else if (*dumpKind == 0) {
        swprintf(head, hn, known ? L"ROM  (not found in the game dump)" : L"ROM  (game dump folder not known)");
    }
    AppendHeading(m, head);
    UINT on = *dumpKind ? MF_STRING : (MF_STRING | MF_GRAYED);
    AppendMenuW(m, on, 11, c);
    AppendMenuW(m, on, 12, e);
    if (nsel > 1) {                      // several search results: ZIP all their originals
        swprintf(zt, 600, L"Create ZIP of the selected results' originals (%d results)...", nsel);
        AppendMenuW(m, known && !g_zipping ? MF_STRING : (MF_STRING | MF_GRAYED), 15, zt);
    } else AppendMenuW(m, *dumpKind && !g_zipping ? MF_STRING : (MF_STRING | MF_GRAYED), 13, z);
    if (*dumpKind == 0) AppendMenuW(m, MF_STRING, 14, known ? L"Choose a different game dump folder..." : L"Locate the game dump folder...");
}
static void ZipSelectedDump(void);
static void ForgetSearch(void);
static void CopyAllResults(void);
// right-click on the results list away from any result (e.g. a search with no matches): the list-wide commands
static void ResultsBlankMenu(POINT pt) {
    int n = (int)SendMessageW(g_results, LB_GETCOUNT, 0, 0);
    HMENU m = CreatePopupMenu();
    AppendHeading(m, L"Search results");
    AppendMenuW(m, MF_STRING | (n ? 0 : MF_GRAYED), 4, L"Copy all results");
    AppendMenuW(m, MF_STRING | (n ? 0 : MF_GRAYED), 6, L"Select all results\tCtrl+A");
    AppendMenuW(m, MF_STRING | (g_lastTerm[0] ? 0 : MF_GRAYED), 7, L"Search again (don't use saved results)");
    AppendMenuW(m, MF_STRING | (g_lastTerm[0] || n ? 0 : MF_GRAYED), 21, L"Clear results and forget this search");
    UiMenuPrepare(m);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, NULL);
    UiMenuFree(m); DestroyMenu(m);
    if (cmd == 4) CopyAllResults();
    else if (cmd == 6) SendMessageW(g_results, LB_SETSEL, TRUE, -1);
    else if (cmd == 7) { SetWindowTextW(g_sedit, g_lastTerm); g_ovrOn = TRUE; g_ovrMc = g_lastMatchCase; swprintf(g_ovrScope, PATHBUF, L"%ls", g_lastScope); StartSearch(TRUE); }
    else if (cmd == 21) ForgetSearch();
}
static void PathMenu(const wchar_t *path, BOOL isDir, POINT pt, BOOL isResult) {
    HMENU m = CreatePopupMenu();
    wchar_t buf0[PATHBUF];
    int nsel = isResult ? (int)SendMessageW(g_results, LB_GETSELCOUNT, 0, 0) : 0;
    wchar_t dumpPath[PATHBUF] = L"", dumpHead[PATHBUF + 64]; int dumpKind = 0;
    if (isResult) {
        AppendHeading(m, L"JSON");
        AppendMenuW(m, MF_STRING, 1, L"Copy full path");
        AppendMenuW(m, MF_STRING, 2, L"Copy path within scan");
        AppendMenuW(m, MF_STRING, 3, L"Show in Explorer");
        wchar_t t[128];
        swprintf(t, 128, nsel > 1 ? L"Create ZIP of the selected results' files (%d results)..." : L"Create ZIP of this file...", nsel);
        AppendMenuW(m, MF_STRING | (g_zipping ? MF_GRAYED : 0), 5, t);
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        TreeMenuDump(m, path, FALSE, dumpPath, &dumpKind, dumpHead, PATHBUF + 64, nsel);
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, 4, L"Copy all results");
        AppendMenuW(m, MF_STRING, 6, L"Select all results\tCtrl+A");
        AppendMenuW(m, MF_STRING | (g_lastTerm[0] ? 0 : MF_GRAYED), 7, L"Search again (don't use saved results)");
        AppendMenuW(m, MF_STRING, 21, L"Clear results and forget this search");
        if (IsSoundOut(path)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 9, EndsWithI(path, L".webm.json") ? L"Play video" : L"Play sound"); }
        if (IsTexOut(path, FALSE)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 16, L"View textures"); }
        if (IsFontOut(path, FALSE)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 18, L"View font"); }
        if (IsLgtOut(path, FALSE)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 19, L"View lighting"); }
        if (IsEvtOut(path)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 20, L"View event script"); }
        if (IsLytOut(path, FALSE)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 17, EndsWithI(path, L".bflan.json") ? L"Play animation in the layout viewer" : L"View layout"); }
        if (Open3DFor(path, FALSE, FALSE)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view"); }
    } else {
        AppendHeading(m, L"JSON");
        AppendMenuW(m, MF_STRING, 1, isDir ? L"Copy full folder path" : L"Copy full path");
        AppendMenuW(m, MF_STRING, 2, L"Copy path within scan");
        AppendMenuW(m, MF_STRING, 3, isDir ? L"Open folder in Explorer" : L"Show in Explorer");
        {
            wchar_t zt[600] = L"Create ZIP of this file...";
            if (isDir && g_filterOn) { wchar_t fs[400]; FilterSummary(fs, 400); swprintf(zt, 600, L"Create ZIP of this folder (only %ls files)...", fs); }
            else if (isDir) swprintf(zt, 600, L"Create ZIP of this folder...");
            AppendMenuW(m, MF_STRING | (g_zipping ? MF_GRAYED : 0), 5, zt);
        }
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        TreeMenuDump(m, path, isDir, dumpPath, &dumpKind, dumpHead, PATHBUF + 64, 0);
        if (VoxPartOutDir(path, isDir, buf0)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view of this terrain part"); }
        else if (VoxObjOutDir(path, isDir, NULL, NULL)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view of this voxel object"); }
        else if (IsPhiveOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view of this collision shape"); }
        else if (IsTerrainHeightOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view of this terrain tile"); }
        else if (IsTerrainSceneOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view of the whole terrain (all tiles stitched)"); }
        else if (IsClothOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view of this cloth (with its model)"); }
        else if (IsBfresModelOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 8, L"Open 3D view of this model"); }
        if (!isDir && IsSoundOut(path)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 9, EndsWithI(path, L".webm.json") ? L"Play video" : L"Play sound"); }
        if (IsTexOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 16, L"View textures"); }
        if (IsFontOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 18, L"View font"); }
        if (IsLgtOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 19, L"View lighting"); }
        if (!isDir && IsEvtOut(path)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 20, L"View event script"); }
        if (IsLytOut(path, isDir)) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, 17, EndsWithI(path, L".bflan.json") ? L"Play animation in the layout viewer" : L"View layout"); }
    }
    if (g_zipping) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING | (g_zipCancel ? MF_GRAYED : 0), 10, L"Stop making ZIP"); }
    UiMenuPrepare(m);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, NULL);
    UiMenuFree(m);
    DestroyMenu(m);
    wchar_t buf[PATHBUF];
    if (cmd == 1) { CopyText(path); SetStatus(L"Full path copied."); }
    else if (cmd == 2) { ScanRelPath(path, isDir, buf, PATHBUF); CopyText(buf); SetStatus(L"Path within scan copied."); }
    else if (cmd == 3) ShowInExplorer(path, isDir);
    else if (cmd == 4) CopyAllResults();
    else if (cmd == 5) { if (isResult) ZipSelectedResults(); else ZipTreePath(path, isDir); }
    else if (cmd == 10) StopZip();
    else if (cmd == 11) { CopyText(dumpPath); SetStatus(L"ROM path copied."); }
    else if (cmd == 12) ShowInExplorer(dumpPath, dumpKind == 2);
    else if (cmd == 13) ZipTreePath(dumpPath, dumpKind == 2);
    else if (cmd == 14) AskDumpFolder();
    else if (cmd == 15) ZipSelectedDump();
    else if (cmd == 16) OpenTex(path);
    else if (cmd == 17) OpenLyt(path);
    else if (cmd == 18) OpenFont(path);
    else if (cmd == 19) OpenLgt(path, isDir);
    else if (cmd == 20) OpenEvt(path);
    else if (cmd == 6) SendMessageW(g_results, LB_SETSEL, TRUE, -1);
    else if (cmd == 8) Open3DFor(path, isDir, TRUE);
    else if (cmd == 9) OpenSound(path);
    else if (cmd == 21) ForgetSearch();
    else if (cmd == 7) { SetWindowTextW(g_sedit, g_lastTerm); g_ovrOn = TRUE; g_ovrMc = g_lastMatchCase; swprintf(g_ovrScope, PATHBUF, L"%ls", g_lastScope); StartSearch(TRUE); }
}

// "Open ROM": the original of what's shown, in the game dump, selected in Explorer
static void OpenRomFolder(void) {
    const wchar_t *p = g_cur[0] ? g_cur : g_curDir[0] ? g_curDir : g_root; BOOL isDir = !g_cur[0];
    if (!p[0]) return;
    wchar_t dump[PATHBUF]; BOOL known;
    int k = DumpOrigNoAsk(p, isDir, dump, &known);
    if (!k && !known) { AskDumpFolder(); k = DumpOrigNoAsk(p, isDir, dump, &known); }
    if (!k) { MessageBoxW(g_wnd, known ? L"This wasn't found in the game dump." : L"The game dump folder for this scan isn't known.", L"ns2dig", MB_ICONINFORMATION); return; }
    ShowInExplorer(dump, k == 2);
}
static void OpenCurrentFolder(void) {
    if (g_cur[0]) ShowInExplorer(g_cur, FALSE);
    else if (g_curDir[0]) ShowInExplorer(g_curDir, TRUE);
    else if (g_root[0]) ShowInExplorer(g_root, TRUE);
}

/* ------------------------------------------------------------------ folder as plain-text tree */
#define DIRVIEW_FILES_PER_FOLDER 12
#define DIRVIEW_MAX_LINES 300000

typedef struct { int nd, nf, lines; BOOL all, truncated; } DirStats;

// extension used in summaries: last one, or last two when the last is ".zs"
static const wchar_t *ExtOf(const wchar_t *name, wchar_t *buf, size_t n) {
    const wchar_t *d = wcsrchr(name, L'.');
    if (!d || d == name) { swprintf(buf, n, L"(no extension)"); return buf; }
    if (!_wcsicmp(d, L".zs")) {
        const wchar_t *d2 = d; while (d2 > name && *(d2 - 1) != L'.') d2--;
        if (d2 > name + 1) d = d2 - 1;
    }
    swprintf(buf, n, L"%ls", d); return buf;
}

static void DirTreeRec(WBuf *b, const wchar_t *dir, const wchar_t *prefix, DirStats *st) {
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    int cap = 64, n = 0;
    Ent *e = (Ent *)xalloc(cap * sizeof(Ent));
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            int isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!isdir && !EndsWithI(fd.cFileName, L".json")) continue;
            if (g_filterOn) {
                if (!isdir && !FileShown(fd.cFileName)) continue;
                if (isdir) { wchar_t sub[PATHBUF]; swprintf(sub, PATHBUF, L"%ls\\%ls", dir, fd.cFileName); if (!DirShown(sub)) continue; }
            }
            if (n == cap) { cap *= 2; Ent *ne = (Ent *)xalloc(cap * sizeof(Ent)); memcpy(ne, e, n * sizeof(Ent)); xfree(e); e = ne; }
            e[n].name = xwcsdup(fd.cFileName);
            if (!isdir) e[n].name[wcslen(e[n].name) - 5] = 0;
            e[n].dir = isdir; n++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    qsort(e, n, sizeof(Ent), CmpEnt);
    int ndirs = 0; while (ndirs < n && e[ndirs].dir) ndirs++;
    int nfiles = n - ndirs;
    st->nf += nfiles; st->nd += ndirs;
    int showFiles = (st->all || nfiles <= DIRVIEW_FILES_PER_FOLDER + 2) ? nfiles : DIRVIEW_FILES_PER_FOLDER;
    int hidden = nfiles - showFiles;
    int shown = ndirs + showFiles;

    for (int i = 0; i < shown; i++) {
        BOOL last = (i == shown - 1) && !hidden;
        if (st->lines >= DIRVIEW_MAX_LINES) { st->truncated = TRUE; break; }
        wchar_t line[PATHBUF];
        swprintf(line, PATHBUF, L"%ls%ls%ls%ls\r\n", prefix, last ? L"\x2514\x2500\x2500 " : L"\x251C\x2500\x2500 ",
                 e[i].name, e[i].dir ? L"/" : L"");
        WAdd(b, line); st->lines++;
        if (e[i].dir) {
            wchar_t sub[PATHBUF], np[PATHBUF];
            swprintf(sub, PATHBUF, L"%ls\\%ls", dir, e[i].name);
            swprintf(np, PATHBUF, L"%ls%ls", prefix, last ? L"    " : L"\x2502   ");
            DirTreeRec(b, sub, np, st);
        }
    }
    if (hidden && st->lines < DIRVIEW_MAX_LINES) {
        // summarise the hidden files by extension
        wchar_t *exts[16]; int cnt[16]; int ne = 0, other = 0;
        for (int i = ndirs + showFiles; i < n; i++) {
            wchar_t eb[MAX_PATH]; ExtOf(e[i].name, eb, MAX_PATH);
            int k = 0; while (k < ne && _wcsicmp(exts[k], eb)) k++;
            if (k < ne) cnt[k]++;
            else if (ne < 16) { exts[ne] = xwcsdup(eb); cnt[ne++] = 1; }
            else other++;
        }
        for (int i = 0; i < ne; i++) for (int j = i + 1; j < ne; j++)
            if (cnt[j] > cnt[i]) { int t = cnt[i]; cnt[i] = cnt[j]; cnt[j] = t; wchar_t *x = exts[i]; exts[i] = exts[j]; exts[j] = x; }
        wchar_t line[PATHBUF * 2], part[MAX_PATH + 32];
        swprintf(line, PATHBUF * 2, L"%ls\x2514\x2500\x2500 \x2026 %d more files (", prefix, hidden);
        for (int i = 0; i < ne; i++) {
            swprintf(part, MAX_PATH + 32, L"%ls%ls \x00D7%d", i ? L", " : L"", exts[i], cnt[i]);
            wcsncat(line, part, PATHBUF * 2 - wcslen(line) - 4);
            xfree(exts[i]);
        }
        if (other) { swprintf(part, MAX_PATH + 32, L", other \x00D7%d", other); wcsncat(line, part, PATHBUF * 2 - wcslen(line) - 4); }
        wcsncat(line, L")\r\n", PATHBUF * 2 - wcslen(line) - 1);
        WAdd(b, line); st->lines++;
    }
    for (int i = 0; i < n; i++) xfree(e[i].name);
    xfree(e);
}

static void ShowDirTree(HTREEITEM it) {
    wchar_t dir[PATHBUF]; ItemPath(it, dir, PATHBUF);
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    DirStats st = { 0 };
    st.all = SendMessageW(g_allfiles, BM_GETCHECK, 0, 0) == BST_CHECKED;
    WBuf body = { 0 };
    DirTreeRec(&body, dir, L"", &st);

    // header: path relative to the output folder (starts with the scan name)
    size_t rl = wcslen(g_root);
    const wchar_t *rel = dir + rl;
    wchar_t scan[PATHBUF]; const wchar_t *bs = wcsrchr(g_root, L'\\');
    swprintf(scan, PATHBUF, L"%ls%ls", bs ? bs + 1 : g_root, rel);
    for (wchar_t *q = scan; *q; q++) if (*q == L'\\') *q = L'/';
    WBuf out = { 0 };
    wchar_t head[PATHBUF + 256];
    swprintf(head, PATHBUF + 256, L"%ls/   (%d folder%ls, %d file%ls%ls)\r\n", scan, st.nd, st.nd == 1 ? L"" : L"s",
             st.nf, st.nf == 1 ? L"" : L"s",
             st.all ? L"" : L"; long file lists shortened - tick \"List every file\" to show all");
    WAdd(&out, head);
    if (g_filterOn) {
        wchar_t fs[400], fl[500]; FilterSummary(fs, 400);
        swprintf(fl, 500, L"(only showing file types: %ls - change with \"File types\" above the tree)\r\n", fs);
        WAdd(&out, fl);
    }
    if (body.p) WAdd(&out, body.p);
    if (st.truncated) WAdd(&out, L"\r\n(stopped after 300,000 lines)\r\n");
    SetWindowTextW(g_view, out.p ? out.p : L"");
    xfree(body.p); xfree(out.p);
    SetCursor(old);
    g_cur[0] = 0;
    g_dirItem = it;
    swprintf(g_curDir, PATHBUF, L"%ls", dir);
    EnableWindow(g_opendir, TRUE); EnableWindow(g_openrom, TRUE);
    UpdateVox3DButton();
    wchar_t lbl[PATHBUF + 64];
    { wchar_t relw[PATHBUF]; swprintf(relw, PATHBUF, L"%ls", dir + rl + (dir[rl] == L'\\')); SetWindowTextW(g_gopath, relw); }
    swprintf(lbl, PATHBUF + 64, L"%.0ls%d folder%ls, %d file%ls - click the text, Ctrl+A, Ctrl+C to copy", scan,
             st.nd, st.nd == 1 ? L"" : L"s", st.nf, st.nf == 1 ? L"" : L"s");
    SetWindowTextW(g_viewlbl, lbl);
    HdrSet(dir, TRUE, NULL, NULL);
}

static void LoadTree(BOOL quiet) {
    wchar_t name[PATHBUF], full[PATHBUF];
    GetText(g_out, name, PATHBUF);
    const wchar_t *err = CheckScanName(name);
    if (err) { if (quiet) SetStatus(err); else MessageBoxW(g_wnd, err, L"ns2dig", MB_ICONWARNING); return; }
    SetWindowTextW(g_out, name);
    ScanDir(name, full, PATHBUF);
    DWORD at = GetFileAttributesW(full);
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) {
        if (quiet) {
            wchar_t m[PATHBUF + 96];
            swprintf(m, PATHBUF + 96, L"New scan \"%ls\" - choose the folder to extract and click Extract.", name);
            SetStatus(m);
            return;
        }
        wchar_t m[PATHBUF + 128];
        swprintf(m, PATHBUF + 128, L"There is no scan called \"%ls\" yet.\n\nChoose a RomFS folder and click Extract to create it.", name);
        MessageBoxW(g_wnd, m, L"ns2dig", MB_ICONINFORMATION);
        return;
    }
    swprintf(g_root, PATHBUF, L"%ls", full);
    const char *prevStage = g_stage;
    g_stage = "opening a scan: file-type index";
    TypeIndexForRoot();
    g_stage = "opening a scan: building the tree";
    TreeClearFast();
    wchar_t rlbl[PATHBUF + 500]; RootLabel(rlbl, PATHBUF + 500);
    HTREEITEM root = AddItem(TVI_ROOT, rlbl, 1);
    TreeView_Expand(g_tree, root, TVE_EXPAND);
    g_suppressDirView = TRUE;
    TreeView_SelectItem(g_tree, root);
    g_suppressDirView = FALSE;
    g_dirItem = NULL;
    SetWindowTextW(g_viewlbl, L"Click a file in the tree to view it.");
    g_cur[0] = 0; g_curDir[0] = 0; EnableWindow(g_opendir, TRUE); EnableWindow(g_openrom, TRUE);   // button opens the scan folder
    HdrSet(g_root, TRUE, NULL, NULL);
    SetWindowTextW(g_gopath, L"");
    UpdateScanLogButton();
    SaveSettings();
    g_stage = prevStage;
}

// select the tree item for a full file path, expanding folders on the way
static BOOL SelectPathInTree(const wchar_t *path) {
    size_t rl = wcslen(g_root);
    if (!rl || _wcsnicmp(path, g_root, rl) != 0) return FALSE;
    wchar_t rel[PATHBUF]; swprintf(rel, PATHBUF, L"%ls", path + rl + (path[rl] == L'\\'));
    HTREEITEM cur = TreeView_GetRoot(g_tree);
    wchar_t *ctx = NULL, *tok = wcstok(rel, L"\\", &ctx);
    while (tok && cur) {
        wchar_t *next = wcstok(NULL, L"\\", &ctx);
        if (!next && EndsWithI(tok, L".json")) tok[wcslen(tok) - 5] = 0;
        FillFolder(cur);
        TreeView_Expand(g_tree, cur, TVE_EXPAND);
        HTREEITEM c = TreeView_GetChild(g_tree, cur), found = NULL;
        while (c) {
            wchar_t buf[MAX_PATH * 2];
            TVITEMW tv = { 0 }; tv.mask = TVIF_TEXT; tv.hItem = c; tv.pszText = buf; tv.cchTextMax = MAX_PATH * 2;
            TreeView_GetItem(g_tree, &tv);
            if (!_wcsicmp(buf, tok)) { found = c; break; }
            c = TreeView_GetNextSibling(g_tree, c);
        }
        cur = found; tok = next;
    }
    if (!cur) return FALSE;
    TreeView_SelectItem(g_tree, cur);
    TreeView_EnsureVisible(g_tree, cur);
    return TRUE;
}

/* ------------------------------------------------------------------ go to a pasted path */
static BOOL PathExists(const wchar_t *p, BOOL *isDir) {
    wchar_t lp[PATHBUF + 16]; LongPath(p, lp, PATHBUF + 16);
    DWORD a = GetFileAttributesW(lp);
    if (a == INVALID_FILE_ATTRIBUTES) return FALSE;
    if (isDir) *isDir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return TRUE;
}

// rel (backslashes, no leading \) under base -> full path of a folder or .json file; *depth = parts matched
static BOOL ResolveUnder(const wchar_t *base, const wchar_t *rel, wchar_t *out, size_t n, int *depth, wchar_t *deepest) {
    wchar_t tmp[PATHBUF]; swprintf(tmp, PATHBUF, L"%ls", rel);
    wchar_t cur[PATHBUF]; swprintf(cur, PATHBUF, L"%ls", base);
    swprintf(deepest, PATHBUF, L"%ls", base);
    *depth = 0;
    wchar_t *ctx = NULL, *tok = wcstok(tmp, L"\\", &ctx);
    while (tok) {
        wchar_t *next = wcstok(NULL, L"\\", &ctx);
        wchar_t cand[PATHBUF]; BOOL isDir = FALSE, ok = FALSE;
        const wchar_t *tries[4]; wchar_t alt[3][MAX_PATH * 2]; int nt = 0;
        tries[nt++] = tok;
        if (next && !wcschr(tok, L'.')) {              // game paths name archives without their extension
            swprintf(alt[0], MAX_PATH * 2, L"%ls.alsarc.zs", tok); tries[nt++] = alt[0];
            swprintf(alt[1], MAX_PATH * 2, L"%ls.pack.zs", tok);   tries[nt++] = alt[1];
            swprintf(alt[2], MAX_PATH * 2, L"%ls.sarc.zs", tok);   tries[nt++] = alt[2];
        }
        if (!next) {                                   // last part: may be a file shown without .json
            swprintf(alt[0], MAX_PATH * 2, L"%ls.json", tok); tries[nt++] = alt[0];
            size_t l = wcslen(tok);
            if (l > 5 && !_wcsicmp(tok + l - 5, L".gyml")) {        // game path names .gyml, files are .bgyml
                swprintf(alt[1], MAX_PATH * 2, L"%.*lsbgyml.json", (int)(l - 4), tok); tries[nt++] = alt[1];
            }
        }
        for (int i = 0; i < nt && !ok; i++) {
            swprintf(cand, PATHBUF, L"%ls\\%ls", cur, tries[i]);
            if (PathExists(cand, &isDir) && (next ? isDir : TRUE)) ok = TRUE;
        }
        if (!ok) return FALSE;
        swprintf(cur, PATHBUF, L"%ls", cand);
        (*depth)++;
        if (isDir) swprintf(deepest, PATHBUF, L"%ls", cur);
        tok = next;
    }
    swprintf(out, n, L"%ls", cur);
    return TRUE;
}

// every "romfs" folder (and the scan root) within a few levels: places a game path can start
static void FindRomfsRoots(const wchar_t *dir, int level, wchar_t roots[][PATHBUF], int *nr, int max) {
    if (level > 5 || *nr >= max) return;
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (wcsstr(fd.cFileName, L".") && level > 0) continue;          // archives (.pack.zs etc.) can't hold a romfs
        wchar_t p[PATHBUF]; swprintf(p, PATHBUF, L"%ls\\%ls", dir, fd.cFileName);
        if (!_wcsicmp(fd.cFileName, L"romfs")) { if (*nr < max) swprintf(roots[(*nr)++], PATHBUF, L"%ls", p); }
        else FindRomfsRoots(p, level + 1, roots, nr, max);
    } while (FindNextFileW(h, &fd) && *nr < max);
    FindClose(h);
}

/* ------------------------------------------------------------------ bookmarks
   A "Bookmarks" menu: per viewer kind (3D, video, audio...), a few highlights of the game plus the user's own
   (the star button next to Open JSON). Kept in the ini per GAME (detected from the scan's contents - the scan
   name can be anything), so they survive re-extracting. Paths are kept relative to the romfs folder when the
   item is inside one (so a scan of the dump's top folder and one of its romfs folder share them).
   Ini section [bookmarks_<game>]: n, 1..n = "kind|label|path"; seeded = version of the built-in list added. */
enum { BK_3D, BK_VIDEO, BK_AUDIO, BK_TEX, BK_LYT, BK_FONT, BK_LGT, BK_EVT, BK_OTHER, BK_COUNT };
static const wchar_t *kBmKindName[BK_COUNT] = { L"3D", L"Video", L"Audio", L"Textures", L"Layouts", L"Fonts", L"Lighting", L"Events", L"Other" };
typedef struct { const char *game; int ver, kind; const wchar_t *label, *path; } BmDefault;
// built-in highlights: paths relative to romfs, as written by the scanner (".json" added); '*' = first match
static const BmDefault kBmDefaults[] = {
    { "MKW", 1, BK_3D,    L"World map (whole terrain)",      L"TerrainArc\\Game03_Proc.tscb.json" },
    { "MKW", 1, BK_3D,    L"Course model: Lava",             L"Model\\Stage_Lava.Stage_Lava_A.bfres.zs.json" },
    { "MKW", 1, BK_3D,    L"Lava lake",                       L"Model\\CmnObj_SpecialSurfaceLavaLake000.CmnObj_SpecialSurfaceLavaLake000_F.bfres.zs.json" },
    { "MKW", 1, BK_3D,    L"Volcano surface",                 L"Model\\CmnObj_VolcanoSurface000.CmnObj_VolcanoSurface000_A.bfres.zs.json" },
    { "MKW", 1, BK_3D,    L"Dash ring",                       L"Model\\CmnObj_AccelRing000.CmnObj_AccelRing000_A.bfres.zs.json" },
    { "MKW", 1, BK_AUDIO, L"Music: Lava course",              L"Sound\\Resource\\Stream\\BGM_Stage_Lava.bwav.json" },
    { "MKW", 1, BK_AUDIO, L"Music: Lava course (final lap)",  L"Sound\\Resource\\Stream\\BGM_Stage_Lava_Fast.bwav.json" },
    { "MKW", 1, BK_AUDIO, L"Music: Lava course (bridge)",     L"Sound\\Resource\\Stream\\BGM_Stage_Lava_Bridge.bwav.json" },
    { "MKW", 1, BK_TEX,   L"Ground textures (46 layers)",     L"Tex\\CmnTex_Tera_Product_Proc_Alb.bntx.zs.json" },
    { "MKW", 1, BK_TEX,   L"Lava texture",                    L"Tex\\CmnTex_Special_Lava_Alb.bntx.zs.json" },
    { "MKW", 1, BK_TEX,   L"Lava course banner",              L"Layout\\C_StageL_Lava_00.Nin_Ounce_NVN2.bntx.zs.json" },
    { "MKW", 1, BK_TEX,   L"Lava course map",                 L"Layout\\MapStage_Lava_00.Nin_Ounce_NVN2.bntx.zs.json" },
    { "MKW", 1, BK_FONT,  L"Main font",                       L"Font\\Font.Nin_Ounce_NVN2.bfarc.zs\\scft\\tt1154c_.bfttf.json" },
    // v2 (from the MKW folder listings)
    { "MKW", 2, BK_3D,    L"Mario",                           L"Model\\Driver_Mario.Driver_Mario.bfres.zs.json" },
    { "MKW", 2, BK_3D,    L"Standard Kart",                   L"Model\\Vehicle_Stk.Vehicle_Stk.bfres.zs.json" },
    { "MKW", 2, BK_3D,    L"Course model: Mario Bros. Circuit", L"Model\\Stage_FirstCircuit.Stage_FirstCircuit_A.bfres.zs.json" },
    { "MKW", 2, BK_3D,    L"Course model: Rainbow Road",      L"Model\\Stage_RainbowSpaceStation.Stage_RainbowSpaceStation_A.bfres.zs.json" },
    { "MKW", 2, BK_VIDEO, L"Credits (the longest video)",     L"Movie\\StaffRoll_00.webm.json" },
    { "MKW", 2, BK_VIDEO, L"Rainbow Road appears",            L"Movie\\RainbowAppears.webm.json" },
    { "MKW", 2, BK_VIDEO, L"Mirror mode unlocked",            L"Movie\\MirrorModeOpen.webm.json" },
    { "MKW", 2, BK_VIDEO, L"Mushroom Cup intro",              L"Movie\\KinokoOp.webm.json" },
    { "MKW", 2, BK_VIDEO, L"How to drift",                    L"Movie\\Manual_Drift.webm.json" },
    { "MKW", 2, BK_AUDIO, L"Music: credits",                  L"Sound\\Resource\\Stream\\BGM_Demo_Staffroll.bwav.json" },
    { "MKW", 2, BK_AUDIO, L"Music: Rainbow Road",             L"Sound\\Resource\\Stream\\BGM_Rainbow_Area1_Melody.bwav.json" },
    { "MKW", 2, BK_AUDIO, L"Music: Mario Kart 64 title (jukebox)", L"Sound\\Resource\\Stream\\BGM_JukeBox_M334_MKN64_Title_OutroFull.bwav.json" },
    { "MKW", 2, BK_AUDIO, L"Music: battle",                   L"Sound\\Resource\\Stream\\BGM_Battle_02.bwav.json" },
    { "MKW", 2, BK_TEX,   L"Rainbow Road banner",             L"Layout\\C_StageL_Rainbow_00.Nin_Ounce_NVN2.bntx.zs.json" },
    { "MKW", 2, BK_TEX,   L"Rainbow Road map",                L"Layout\\MapStage_Rainbow_00.Nin_Ounce_NVN2.bntx.zs.json" },
    { "MKW", 2, BK_TEX,   L"Water patterns",                  L"Tex\\CmnTex_Tera_Water_Emm.bntx.zs.json" },
    { "MKW", 2, BK_LYT,   L"World map screen",                L"Layout\\Map_00.Nin_Ounce_NVN2.blarc.zs" },
    { "DEMO", 1, BK_3D,   L"Island (whole terrain)",          L"Terrain\\DemoIsland.tscb.json" },
    { "DEMO", 1, BK_TEX,  L"Ground textures",                 L"Tex\\DemoIsland_Ground.bntx.zs.json" },
    { "DEMO", 1, BK_TEX,  L"Water texture",                   L"Tex\\DemoIsland_Water.bntx.zs.json" },
    { "DEMO", 2, BK_3D,   L"Player model",                    L"Model\\DemoPlayer.DemoPlayer.bfres.zs.json" },
    { "DEMO", 3, BK_3D,   L"Crate model",                     L"Model\\DemoCrate.DemoCrate.bfres.zs.json" },
    { "DEMO", 4, BK_VIDEO, L"Seagulls (video)",               L"Movie\\DemoSeagulls.webm.json" },
    { "DEMO", 4, BK_AUDIO, L"Music: Golden Tides",            L"Sound\\Stream\\GoldenTides.bwav.json" },
    { "DEMO", 9, BK_LYT,   L"Title screen (intro)",           L"Layout\\DemoTitle.blarc.zs\\anim\\DemoTitle_Intro.bflan.json" },
    { "DEMO", 9, BK_LYT,   L"Title screen (idle)",            L"Layout\\DemoTitle.blarc.zs\\anim\\DemoTitle_Idle.bflan.json" },
    { "DEMO", 9, BK_LYT,   L"Button (select)",                L"Layout\\DemoTitle.blarc.zs\\anim\\DemoButton_Select.bflan.json" },
    { "DEMO", 9, BK_LYT,   L"Title screen (layout)",          L"Layout\\DemoTitle.blarc.zs\\blyt\\DemoTitle.bflyt.json" },
    { "DEMO", 5, BK_FONT,  L"Font: DemoBeads",                L"Font\\Font.100.bfarc.zs\\font\\DemoBeads.bffnt.json" },
    { "DEMO", 9, BK_FONT,  L"Font: DemoBlocks",               L"Font\\Font.100.bfarc.zs\\font\\DemoBlocks.bffnt.json" },
    { "DEMO", 6, BK_LGT,   L"Lighting (midday, sunset, night, storm)", L"Env\\DemoIsland.genvb.zs" },
    { "DEMO", 7, BK_EVT,   L"Event: talking to the islander",  L"Event\\EventFlow\\DemoIsland_Islander.bfevfl.zs.json" },
    { "DEMO", 7, BK_EVT,   L"Event: arrival cutscene",         L"Event\\EventFlow\\DemoIsland_Arrival.bfevfl.zs.json" },
    { "DEMO", 8, BK_TEX,   L"Texture formats (Switch 2: BC, ASTC, mips, cube)", L"Tex\\DemoFormats.bntx.zs.json" },
    { "DEMO", 8, BK_TEX,   L"Texture formats (Switch 1 tiling)", L"Tex\\DemoFormats_NX.bntx.zs.json" },
    { "DEMO", 8, BK_3D,    L"Collision: the whole map",        L"Phive\\Shape\\DemoIsland.bphsh.zs.json" },
    { "DEMO", 8, BK_3D,    L"Collision: the crate",            L"Phive\\Shape\\DemoCrate.bphsh.zs.json" },
    { "DEMO", 8, BK_3D,    L"Flag (cloth)",                    L"Model\\DemoFlag.DemoFlag.bfres.zs.json" },
    { "DEMO", 8, BK_3D,    L"Cloth on its own: the flag",      L"Phive\\Cloth\\DemoFlag.bphcl.zs.json" },
    { "DEMO", 8, BK_3D,    L"Hat (attached to the player)",    L"Model\\DemoHat.DemoHat.bfres.zs.json" },
    { "DEMO", 8, BK_3D,    L"Voxel rock arch",                 L"Voxel\\DemoRocks\\Arch\\Info.bin.json" },
    { "DEMO", 8, BK_AUDIO, L"Sound effects (BARS)",            L"Sound\\DemoSounds.bars.zs.json" },
    { "DEMO", 8, BK_OTHER, L"Game data (dictionary-compressed)", L"GameData\\DemoItems.byml.zs.json" },
    { "DEMO", 8, BK_OTHER, L"Resource size table",             L"System\\Resource\\ResourceSizeTable.Product.100.rsizetable.zs.json" },
    { "DKB", 1, BK_3D,    L"Donkey Kong",                     L"Model\\DonkeyKong.DonkeyKong.bfres.zs.json" },
    { "DKB", 1, BK_3D,    L"Donkey Kong (no clothes)",        L"Model\\DonkeyKongNaked.DonkeyKongNaked.bfres.zs.json" },
    { "DKB", 1, BK_3D,    L"Donkey Kong's helmet",            L"Model\\DonkeyKongHelmet.DonkeyKongHelmet.bfres.zs.json" },
    { "DKB", 1, BK_3D,    L"Donkey Kong (rock)",              L"Model\\DonkeyKongRock.DonkeyKongRock.bfres.zs.json" },
    { "DKB", 1, BK_TEX,   L"Donkey Kong's fur",               L"Tex\\DonkeyKongBody_Alb.0.bntx.zs.json" },
};
// built-in ones replaced by a later version: taken out once (only if the user still has them)
typedef struct { const char *game; int ver; const wchar_t *path; } BmRetired;
static const BmRetired kBmRetired[] = {
    { "DEMO", 9, L"Layout\\DemoTitle.blarc.zs" },                // (the folder: now its layout and animations one by one)
    { "MKW", 2, L"Model\\Stage_Lava.Stage_Lava_A.bfres.zs.json" },
    { "MKW", 2, L"Model\\CmnObj_SpecialSurfaceLavaLake000.CmnObj_SpecialSurfaceLavaLake000_F.bfres.zs.json" },
    { "MKW", 2, L"Model\\CmnObj_VolcanoSurface000.CmnObj_VolcanoSurface000_A.bfres.zs.json" },
    { "MKW", 2, L"Model\\CmnObj_AccelRing000.CmnObj_AccelRing000_A.bfres.zs.json" },
    { "MKW", 2, L"Sound\\Resource\\Stream\\BGM_Stage_Lava_Fast.bwav.json" },
    { "MKW", 2, L"Sound\\Resource\\Stream\\BGM_Stage_Lava_Bridge.bwav.json" },
    { "MKW", 2, L"Layout\\C_StageL_Lava_00.Nin_Ounce_NVN2.bntx.zs.json" },
    { "MKW", 2, L"Layout\\MapStage_Lava_00.Nin_Ounce_NVN2.bntx.zs.json" },
};
// which game a scan is: known games by tell-tale files; others by the program id in exefs\main.npdm
typedef struct { const char *key; const wchar_t *name; const wchar_t *signs[3]; } BmGame;
static const BmGame kBmGames[] = {
    { "DEMO", L"ns2dig demo",         { L"Terrain\\DemoIsland.tscb.json", NULL, NULL } },   // (the made-up demo romfs: first, so it wins)
    { "MKW", L"Mario Kart World",     { L"TerrainArc\\Game03_Proc.tscb.json", L"StaffGhost", NULL } },
    { "DKB", L"Donkey Kong Bananza",  { L"Model\\DonkeyKong.DonkeyKong.bfres.zs.json", L"Voxel", NULL } },
};
static wchar_t g_bmRootFor[PATHBUF], g_bmGameKey[96], g_bmGameName[128];
static wchar_t g_bmRoots[16][PATHBUF]; static int g_bmNRoots;
typedef struct { int kind; wchar_t label[160]; wchar_t path[PATHBUF]; } BmEntry;
static BmEntry *g_bm; static int g_nbm;

static BOOL BmExists(const wchar_t *p) { wchar_t lp[PATHBUF + 16]; LongPath(p, lp, PATHBUF + 8); return GetFileAttributesW(lp) != INVALID_FILE_ATTRIBUTES; }
// a stored path -> the file / folder in this scan ("R:" = relative to a romfs folder; '*' = first match, not .anim.)
static BOOL BmResolve(const wchar_t *stored, wchar_t *out, size_t n) {
    BOOL rom = !wcsncmp(stored, L"R:", 2);
    const wchar_t *rel = rom ? stored + 2 : stored;
    int nb = rom ? g_bmNRoots : 1;
    for (int i = 0; i < nb; i++) {
        wchar_t full[PATHBUF]; swprintf(full, PATHBUF, L"%ls\\%ls", rom ? g_bmRoots[i] : g_root, rel);
        if (!wcschr(rel, L'*')) {
            if (BmExists(full)) { swprintf(out, n, L"%ls", full); return TRUE; }
            // a top-level folder named differently (e.g. Movie): the same rest under any top-level folder
            const wchar_t *rest = wcschr(rel, L'\\');
            if (!rom || !rest) continue;
            wchar_t pat[PATHBUF + 16]; swprintf(full, PATHBUF, L"%ls\\*", g_bmRoots[i]); LongPath(full, pat, PATHBUF + 8);
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); BOOL got = FALSE;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                    swprintf(full, PATHBUF, L"%ls\\%ls%ls", g_bmRoots[i], fd.cFileName, rest);
                    if (BmExists(full)) { swprintf(out, n, L"%ls", full); got = TRUE; }
                } while (!got && FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (got) return TRUE;
            continue;
        }
        wchar_t pat[PATHBUF + 16]; LongPath(full, pat, PATHBUF + 8);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        wchar_t *bs = wcsrchr(full, L'\\'); BOOL got = FALSE;
        do {
            if (wcsstr(fd.cFileName, L".anim.")) continue;
            if (bs) { bs[1] = 0; swprintf(out, n, L"%ls%ls", full, fd.cFileName); got = TRUE; }
        } while (!got && FindNextFileW(h, &fd));
        FindClose(h);
        if (got) return TRUE;
    }
    return FALSE;
}
// the stored form of a path in this scan
static void BmStoreForm(const wchar_t *full, wchar_t *out, size_t n) {
    for (int i = 0; i < g_bmNRoots; i++) {
        size_t l = wcslen(g_bmRoots[i]);
        if (!_wcsnicmp(full, g_bmRoots[i], l) && full[l] == L'\\') { swprintf(out, n, L"R:%ls", full + l + 1); return; }
    }
    size_t rl = wcslen(g_root);
    swprintf(out, n, L"%ls", (!_wcsnicmp(full, g_root, rl) && full[rl] == L'\\') ? full + rl + 1 : full);
}
static void BmSection(wchar_t *sec, size_t n) { swprintf(sec, n, L"bookmarks_%ls", g_bmGameKey); }
static void BmSave(void) {
    wchar_t sec[160]; BmSection(sec, 160);
    wchar_t seeded[32]; GetPrivateProfileStringW(sec, L"seeded", L"0", seeded, 32, g_ini);
    WritePrivateProfileStringW(sec, NULL, NULL, g_ini);                // rewrite the whole section
    WritePrivateProfileStringW(sec, L"seeded", seeded, g_ini);
    wchar_t k[16], v[PATHBUF + 220];
    swprintf(v, 32, L"%d", g_nbm); WritePrivateProfileStringW(sec, L"n", v, g_ini);
    for (int i = 0; i < g_nbm; i++) {
        swprintf(k, 16, L"%d", i + 1);
        swprintf(v, PATHBUF + 220, L"%d|%ls|%ls", g_bm[i].kind, g_bm[i].label, g_bm[i].path);
        WritePrivateProfileStringW(sec, k, v, g_ini);
    }
    swprintf(v, 128, L"%ls", g_bmGameName); WritePrivateProfileStringW(sec, L"game", v, g_ini);
}
static void BmAdd(int kind, const wchar_t *label, const wchar_t *path) {
    BmEntry *nb = (BmEntry *)realloc(g_bm, sizeof(BmEntry) * (g_nbm + 1)); if (!nb) return;
    g_bm = nb; BmEntry *e = &g_bm[g_nbm++];
    e->kind = kind < 0 || kind >= BK_COUNT ? BK_OTHER : kind;
    swprintf(e->label, 160, L"%ls", label); swprintf(e->path, PATHBUF, L"%ls", path);
}
// (re)load the bookmarks for the scan that is open: detect the game, add new built-in ones
static void BmLoad(void) {
    if (!g_root[0]) { g_nbm = 0; g_bmRootFor[0] = 0; return; }
    if (!_wcsicmp(g_bmRootFor, g_root)) return;
    swprintf(g_bmRootFor, PATHBUF, L"%ls", g_root);
    g_bmNRoots = 0; FindRomfsRoots(g_root, 0, g_bmRoots, &g_bmNRoots, 16);
    // which game?
    g_bmGameKey[0] = 0; g_bmGameName[0] = 0;
    for (size_t gi = 0; gi < sizeof kBmGames / sizeof kBmGames[0] && !g_bmGameKey[0]; gi++)
        for (int si = 0; si < 3 && kBmGames[gi].signs[si] && !g_bmGameKey[0]; si++)
            for (int r = 0; r < g_bmNRoots; r++) {
                wchar_t f[PATHBUF]; swprintf(f, PATHBUF, L"%ls\\%ls", g_bmRoots[r], kBmGames[gi].signs[si]);
                if (BmExists(f)) { swprintf(g_bmGameKey, 96, L"%hs", kBmGames[gi].key); swprintf(g_bmGameName, 128, L"%ls", kBmGames[gi].name); break; }
            }
    if (!g_bmGameKey[0]) {                                        // another game: its program id
        wchar_t f[PATHBUF]; swprintf(f, PATHBUF, L"%ls\\base\\exefs\\main.npdm.json", g_root);
        char *t = NULL;
        HANDLE fh = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (fh != INVALID_HANDLE_VALUE) {
            t = (char *)calloc(1, 65537); DWORD got = 0;
            if (t) ReadFile(fh, t, 65536, &got, NULL);
            CloseHandle(fh);
        }
        const char *q = t ? strstr(t, "\"program_id\": \"") : NULL;
        if (q) { q += 15; char id[20] = { 0 }; for (int i = 0; i < 16 && q[i] && q[i] != '"'; i++) id[i] = q[i];
                 swprintf(g_bmGameKey, 96, L"%hs", id); swprintf(g_bmGameName, 128, L"Game %hs", id); }
        free(t);
    }
    if (!g_bmGameKey[0]) {                                        // nothing to go on: this scan alone
        wchar_t scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF);
        swprintf(g_bmGameKey, 96, L"scan_%.80ls", scan); swprintf(g_bmGameName, 128, L"%.100ls", scan);
    }
    // stored bookmarks
    free(g_bm); g_bm = NULL; g_nbm = 0;
    wchar_t sec[160]; BmSection(sec, 160);
    int n = GetPrivateProfileIntW(sec, L"n", 0, g_ini);
    for (int i = 1; i <= n && i <= 2000; i++) {
        wchar_t k[16], v[PATHBUF + 220]; swprintf(k, 16, L"%d", i);
        GetPrivateProfileStringW(sec, k, L"", v, PATHBUF + 220, g_ini);
        wchar_t *b1 = wcschr(v, L'|'); if (!b1) continue; *b1 = 0;
        wchar_t *b2 = wcschr(b1 + 1, L'|'); if (!b2) continue; *b2 = 0;
        BmAdd(_wtoi(v), b1 + 1, b2 + 1);
    }
    // built-in highlights not added yet (each once: removing one keeps it removed)
    int seeded = GetPrivateProfileIntW(sec, L"seeded", 0, g_ini), top = seeded;
    char gk[96]; WideCharToMultiByte(CP_UTF8, 0, g_bmGameKey, -1, gk, sizeof gk, NULL, NULL);
    for (size_t i = 0; i < sizeof kBmDefaults / sizeof kBmDefaults[0]; i++) {
        const BmDefault *d = &kBmDefaults[i];
        if (strcmp(d->game, gk) || d->ver <= seeded) continue;
        wchar_t p[PATHBUF]; swprintf(p, PATHBUF, L"R:%ls", d->path);
        BmAdd(d->kind, d->label, p);
        if (d->ver > top) top = d->ver;
    }
    for (size_t i = 0; i < sizeof kBmRetired / sizeof kBmRetired[0]; i++) {
        const BmRetired *d = &kBmRetired[i];
        if (strcmp(d->game, gk) || d->ver <= seeded) continue;
        wchar_t p[PATHBUF]; swprintf(p, PATHBUF, L"R:%ls", d->path);
        for (int j = 0; j < g_nbm; j++)
            if (!_wcsicmp(g_bm[j].path, p)) { memmove(&g_bm[j], &g_bm[j + 1], sizeof(BmEntry) * (g_nbm - j - 1)); g_nbm--; break; }
        if (d->ver > top) top = d->ver;
    }
    if (top != seeded) {
        wchar_t v[16]; swprintf(v, 16, L"%d", top);
        WritePrivateProfileStringW(sec, L"seeded", v, g_ini);
        BmSave();
    }
}
// what kind of thing is shown (decides the menu it goes in)
static int BmKindOf(const wchar_t *path, BOOL isDir) {
    if (Open3DFor(path, isDir, FALSE)) return BK_3D;
    if (!isDir && EndsWithI(path, L".webm.json")) return BK_VIDEO;
    if (!isDir && IsSoundOut(path)) return BK_AUDIO;
    if (IsTexOut(path, isDir)) return BK_TEX;
    if (IsLytOut(path, isDir)) return BK_LYT;
    if (IsFontOut(path, isDir)) return BK_FONT;
    if (IsLgtOut(path, isDir)) return BK_LGT;
    if (!isDir && IsEvtOut(path)) return BK_EVT;
    return BK_OTHER;
}
static int BmFind(const wchar_t *full) {
    for (int i = 0; i < g_nbm; i++) {
        wchar_t r[PATHBUF];
        if (BmResolve(g_bm[i].path, r, PATHBUF) && !_wcsicmp(r, full)) return i;
    }
    return -1;
}
static void UpdateBmStar(void) {
    const wchar_t *shown = g_cur[0] ? g_cur : g_curDir[0] ? g_curDir : NULL;
    if (shown && !_wcsicmp(shown, g_root)) shown = NULL;
    BmLoad();
    BOOL on = shown && BmFind(shown) >= 0;
    EnableWindow(g_bmStar, shown != NULL && !g_running);
    if (on != g_bmOn) { g_bmOn = on; }
    InvalidateRect(g_bmStar, NULL, FALSE);
}
static void ToggleBookmark(void) {
    const wchar_t *shown = g_cur[0] ? g_cur : g_curDir[0] ? g_curDir : NULL;
    if (!shown || !g_root[0]) return;
    BmLoad();
    int i = BmFind(shown);
    wchar_t st[400];
    if (i >= 0) {
        swprintf(st, 400, L"Removed \"%ls\" from the bookmarks.", g_bm[i].label);
        memmove(&g_bm[i], &g_bm[i + 1], sizeof(BmEntry) * (g_nbm - i - 1)); g_nbm--;
    } else {
        BOOL isDir = !g_cur[0];
        int kind = BmKindOf(shown, isDir);
        const wchar_t *leaf = wcsrchr(shown, L'\\'); leaf = leaf ? leaf + 1 : shown;
        wchar_t label[160]; swprintf(label, 160, L"%ls", leaf);
        if (!isDir && EndsWithI(label, L".json")) label[wcslen(label) - 5] = 0;
        wchar_t p[PATHBUF]; BmStoreForm(shown, p, PATHBUF);
        BmAdd(kind, label, p);
        swprintf(st, 400, L"Bookmarked \"%ls\" (Bookmarks > %ls).", label, kBmKindName[kind]);
    }
    BmSave();
    SetStatus(st);
    UpdateBmStar();
}
static void ShowBookmarksMenu(void) {
    if (!g_root[0]) { SetStatus(L"Open a scan first."); return; }
    BmLoad();
    HMENU m = CreatePopupMenu();
    wchar_t head[200]; swprintf(head, 200, L"Bookmarks - %ls", g_bmGameName);
    AppendHeading(m, head);
    int any = 0;
    for (int k = 0; k < BK_COUNT; k++) {
        HMENU sub = CreatePopupMenu(); int cnt = 0;
        for (int i = 0; i < g_nbm; i++) {
            if (g_bm[i].kind != k) continue;
            wchar_t r[PATHBUF], t[260];
            BOOL ok = BmResolve(g_bm[i].path, r, PATHBUF);
            swprintf(t, 260, L"%ls%ls", g_bm[i].label, ok ? L"" : L"  (not in this scan)");
            AppendMenuW(sub, MF_STRING | (ok ? 0 : MF_GRAYED), 9000 + i, t);
            cnt++;
        }
        if (!cnt) { DestroyMenu(sub); if (k != BK_OTHER) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, kBmKindName[k]); continue; }
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, kBmKindName[k]);
        any += cnt;
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, any ? L"Add your own with the star next to Open JSON" : L"No bookmarks yet - use the star next to Open JSON");
    UiMenuPrepare(m);
    RECT rc; GetWindowRect(g_bmBtn, &rc);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON, rc.left, rc.bottom, 0, g_wnd, NULL);
    UiMenuFree(m); DestroyMenu(m);
    if (cmd >= 9000 && cmd < 9000 + g_nbm) {
        wchar_t r[PATHBUF];
        if (!BmResolve(g_bm[cmd - 9000].path, r, PATHBUF)) { SetStatus(L"That bookmark isn't in this scan."); return; }
        if (!SelectPathInTree(r)) { SetStatus(L"Found the bookmark, but couldn't show it in the tree."); return; }
        wchar_t st[400]; swprintf(st, 400, L"Bookmark: %ls", g_bm[cmd - 9000].label); SetStatus(st);
        SetFocus(g_tree);
    }
}

// last resort: look for the file name anywhere in the scan
typedef struct { const wchar_t *a, *b; wchar_t first[PATHBUF]; int count; } NameHunt;
static void HuntName(NameHunt *nh, const wchar_t *dir, int depth) {
    if (depth > 40) return;
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        BOOL hit = !_wcsicmp(fd.cFileName, nh->a) || (nh->b && !_wcsicmp(fd.cFileName, nh->b));
        wchar_t p[PATHBUF]; swprintf(p, PATHBUF, L"%ls\\%ls", dir, fd.cFileName);
        if (hit) { if (!nh->count++) swprintf(nh->first, PATHBUF, L"%ls", p); }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) HuntName(nh, p, depth + 1);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void GoToPath(void) {
    if (!g_root[0]) { SetStatus(L"Open a scan first."); return; }
    wchar_t in[PATHBUF]; GetText(g_gopath, in, PATHBUF);
    // tidy up: quotes, spaces, and anything after the path in a copied search result or folder listing
    wchar_t *s = in; while (*s == L' ' || *s == L'\t' || *s == L'"' || *s == L'\'' || (*s >= 0x2500 && *s <= 0x257F)) s++;   // also tree-drawing characters from a folder listing
    for (wchar_t *q = s; *q; q++) {
        if ((q[0] == L' ' && q[1] == L' ') || *q == L'\t' || *q == L'\r' || *q == L'\n') { *q = 0; break; }
    }
    size_t l = wcslen(s);
    while (l && (s[l - 1] == L' ' || s[l - 1] == L'"' || s[l - 1] == L'\'' || s[l - 1] == L'/' || s[l - 1] == L'\\')) s[--l] = 0;
    if (!l) { SetFocus(g_gopath); return; }
    for (wchar_t *q = s; *q; q++) if (*q == L'/') *q = L'\\';
    if (!wcsncmp(s, L"\\\\?\\", 4)) s += 4;

    wchar_t rel[PATHBUF], scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF);
    size_t rl = wcslen(g_root), sl = wcslen(scan);
    if (!_wcsnicmp(s, g_root, rl) && (s[rl] == 0 || s[rl] == L'\\')) swprintf(rel, PATHBUF, L"%ls", s + rl + (s[rl] == L'\\'));
    else if (!_wcsnicmp(s, scan, sl) && (s[sl] == 0 || s[sl] == L'\\')) swprintf(rel, PATHBUF, L"%ls", s + sl + (s[sl] == L'\\'));
    else if (s[1] == L':' || (s[0] == L'\\' && s[1] == L'\\')) {
        // a full path somewhere else: try its tail inside this scan (e.g. ...\romfs\Pack\...)
        const wchar_t *r = NULL;
        for (const wchar_t *q = s; (q = wcsstr(q, L"\\")) != NULL; q++) if (!_wcsnicmp(q, L"\\romfs\\", 7)) r = q + 7;
        swprintf(rel, PATHBUF, L"%ls", r ? r : (wcsrchr(s, L'\\') + 1));
    }
    else swprintf(rel, PATHBUF, L"%ls", s);
    if (!_wcsnicmp(rel, L"Work\\", 5)) memmove(rel, rel + 5, (wcslen(rel + 5) + 1) * sizeof(wchar_t));   // game-internal paths

    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    wchar_t found[PATHBUF] = L"", deepest[PATHBUF] = L"", bestDeep[PATHBUF] = L"", note[PATHBUF + 128] = L"";
    int depth = 0, bestDepth = -1;
    if (!rel[0]) swprintf(found, PATHBUF, L"%ls", g_root);
    else if (ResolveUnder(g_root, rel, found, PATHBUF, &depth, deepest)) { }
    else {
        bestDepth = depth; swprintf(bestDeep, PATHBUF, L"%ls", deepest);
        // relative to a romfs folder (base, update or DLC)?
        static wchar_t roots[32][PATHBUF]; int nr = 0, hits = 0;
        FindRomfsRoots(g_root, 0, roots, &nr, 32);
        wchar_t also[PATHBUF] = L"";
        for (int i = 0; i < nr; i++) {
            wchar_t f[PATHBUF];
            if (ResolveUnder(roots[i], rel, f, PATHBUF, &depth, deepest)) {
                if (!hits++) swprintf(found, PATHBUF, L"%ls", f);
                else if (hits == 2) { ScanRelPath(roots[i], TRUE, also, PATHBUF); }
            } else if (depth > bestDepth) { bestDepth = depth; swprintf(bestDeep, PATHBUF, L"%ls", deepest); }
        }
        if (hits > 1) swprintf(note, PATHBUF + 128, L" (also found under %ls%ls)", also, hits > 2 ? L" and others" : L"");
        if (!found[0]) {
            // last part as a file name anywhere in the scan
            const wchar_t *leaf = wcsrchr(rel, L'\\'); leaf = leaf ? leaf + 1 : rel;
            wchar_t a[MAX_PATH * 2], b[MAX_PATH * 2]; size_t ll = wcslen(leaf);
            swprintf(a, MAX_PATH * 2, L"%ls%ls", leaf, EndsWithI(leaf, L".json") ? L"" : L".json");
            NameHunt nh = { 0 }; nh.a = a;
            if (ll > 5 && !_wcsicmp(leaf + ll - 5, L".gyml")) { swprintf(b, MAX_PATH * 2, L"%.*lsbgyml.json", (int)(ll - 4), leaf); nh.b = b; }
            else { swprintf(b, MAX_PATH * 2, L"%ls", leaf); nh.b = b; }   // also a folder with that name
            HuntName(&nh, g_root, 0);
            if (nh.count) {
                swprintf(found, PATHBUF, L"%ls", nh.first);
                if (nh.count > 1) swprintf(note, PATHBUF + 128, L" - found by name; %d files or folders have that name, showing the first", nh.count);
                else swprintf(note, PATHBUF + 128, L" - found by name");
            }
        }
    }
    BOOL partial = FALSE;
    if (!found[0] && bestDepth > 0) { swprintf(found, PATHBUF, L"%ls", bestDeep); partial = TRUE; }
    SetCursor(old);
    if (!found[0]) {
        SetStatus(L"Path not found in this scan.");
        MessageBeep(MB_ICONASTERISK);
        SetFocus(g_gopath);
        return;
    }
    if (!_wcsicmp(found, g_root)) {
        HTREEITEM r = TreeView_GetRoot(g_tree);
        if (TreeView_GetSelection(g_tree) == r) ShowDirTree(r); else TreeView_SelectItem(g_tree, r);
        TreeView_EnsureVisible(g_tree, r);
    } else if (!SelectPathInTree(found)) { SetStatus(L"Found the path, but couldn't show it in the tree."); return; }
    wchar_t within[PATHBUF], st[PATHBUF * 2 + 200];
    ScanRelPath(found, FALSE, within, PATHBUF);
    if (partial) swprintf(st, PATHBUF * 2 + 200, L"Couldn't find the whole path - went to the nearest folder that exists: %ls", within);
    else swprintf(st, PATHBUF * 2 + 200, L"Went to %ls%ls", within, note);
    SetStatus(st);
    SetFocus(g_tree);
}

/* ------------------------------------------------------------------ file-type filter: window and applying it */
static void RootLabel(wchar_t *out, size_t n) {
    wchar_t scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF);
    if (!g_filterOn) { swprintf(out, n, L"%ls", scan); return; }
    wchar_t fs[400]; FilterSummary(fs, 400);
    swprintf(out, n, L"%ls   [only: %ls]", scan, fs);
}
static void UpdateTypesButton(void) {
    wchar_t t[500], fs[400];
    if (g_filterOn) { FilterSummary(fs, 400); swprintf(t, 500, L"File types: %ls", fs); }
    else swprintf(t, 500, L"File types: all");
    SetWindowTextW(g_typesbtn, t);
    RECT rc; GetClientRect(g_wnd, &rc); Layout(rc.right, rc.bottom);
}
// rebuild the tree with the current filter, keeping the selection if it is still shown
static void RefreshTreeForFilter(void) {
    if (!g_root[0]) return;
    wchar_t keep[PATHBUF] = L"";
    HTREEITEM s = TreeView_GetSelection(g_tree);
    if (s && TreeView_GetParent(g_tree, s)) ItemPath(s, keep, PATHBUF);
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    g_suppressDirView = TRUE;
    TreeClearFast();
    wchar_t lbl[PATHBUF + 500]; RootLabel(lbl, PATHBUF + 500);
    HTREEITEM root = AddItem(TVI_ROOT, lbl, 1);
    TreeView_Expand(g_tree, root, TVE_EXPAND);
    TreeView_SelectItem(g_tree, root);
    g_suppressDirView = FALSE;
    g_dirItem = NULL;
    BOOL kept = keep[0] && SelectPathInTree(keep);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, NULL, TRUE);
    if (!kept) ShowDirTree(root);
}
static void ApplyFilterChange(void) {
    ComputeSelMask();
    SaveFilterSettings();
    UpdateTypesButton();
    if (g_filterOn && !g_idx) StartTypeIndex();     // folders are hidden once the index is ready
    RefreshTreeForFilter();
    if (g_lastTerm[0] && g_root[0]) {               // redo the last search with the new filter (saved results if any)
        SetWindowTextW(g_sedit, g_lastTerm);
        StartSearch(FALSE);
    }
}

enum { TD_LIST = 3001, TD_INFO, TD_SOUNDS, TD_3D, TD_TEXT, TD_ALL, TD_NONE, TD_OFF };
static const wchar_t *TypeDesc(const wchar_t *t) {
    static const wchar_t *d[][2] = {
        { L"bars", L"Sound bank (several sounds) - Play" }, { L"bwav", L"Sound - Play" },
        { L"vgeob", L"Voxel object shape - 3D view" }, { L"hgeob", L"Voxel object surface detail" },
        { L"voxel terrain", L"Terrain part files - 3D view" }, { L"msbt", L"Text messages" },
        { L"byml", L"Data (BYML)" }, { L"byaml", L"Data (BYML - the same format as .byml)" }, { L"changelist", L"Build note from Nintendo's tools: the source shader database (.fsdb) a shader archive was built from, and its change number" }, { L"bgyml", L"Game parameters (BYML)" }, { L"sarc", L"Archive" },
        { L"bnvib", L"Rumble (HD Rumble) pattern" }, { L"bfttf", L"Font" }, { L"bfotf", L"Font" }, { L"otf", L"Font (plain OpenType)" }, { L"bffnt", L"Font" }, { L"bfcpx", L"Font" },
        { L"bfres", L"3D model / animation - 3D view (models)" }, { L"bntx", L"Textures - Textures button, PNG export" },
        { L"glsl", L"Shader source (text)" }, { L"txt", L"Text" }, { L"bushvt", L"UI shader table" }, { L"webm", L"Video - Play" }, { L"bnsh", L"Compiled GPU shaders (UI) - inputs, textures, buffers listed" }, { L"sharcb", L"Engine shader archive: programs, options, inputs, textures, buffers listed" }, { L"bfsha", L"Material shader archive: shading models, options, textures, uniforms, programs listed" }, { L"gsh", L"Wii U shader (left over): attributes, uniforms, textures listed" }, { L"pck", L"GPU pack (NDSR): shaders and data buffers listed" },
        { L"ainb", L"AI / logic node graph" },
        { L"bprojgfxbase", L"Lighting: sun, moon, stars, fog, sky, wind - Lighting button" },
        { L"bssprojgfxbase", L"Lighting: extra screen-space settings - Lighting button" },
        { L"baglblm", L"Lighting: bloom (glow) - Lighting button" }, { L"bagldof", L"Lighting: depth of field (camera blur) - Lighting button" },
        { L"baglccr", L"Lighting: colour correction - Lighting button" }, { L"baglenv", L"Lighting: lights and fogs - Lighting button" },
        { L"baglenvset", L"Lighting: light / fog presets - Lighting button" }, { L"baglsky", L"Lighting: sky - Lighting button" },
        { L"baglcube", L"Lighting: reflection cubemap capture points - Lighting button" },
        { L"baglfila", L"Lighting: anti-aliasing (FXAA) settings - Lighting button" }, { L"bofx", L"Lighting: lens flare (textures + settings) - Lighting button" }, { L"baglvolm", L"Lighting: light shafts (volumetric light) - Lighting button" }, { L"baglatex", L"Lighting: auto exposure (eye adaptation to bright and dark) - Lighting button" }, { L"bagllmap", L"Lighting: light maps - shading ramps for each lighting model (Lambert, Half-Lambert, Hemisphere...) - Lighting button" }, { L"bgapkginfo", L"Shader package info (name, number of shader archives)" }, { L"bgapkglist", L"Shader package file list" }, { L"bgmsconf", L"Renderer setup: which graphics features each setup uses" }, { L"bptclconf", L"Particle system setup (buffer sizes)" }, { L"belnk", L"Effect links: which effects each actor plays on which action" }, { L"bslnk", L"Sound links: which sounds each actor plays on which action" }, { L"bagst", L"Sound groups: mixer tree, volumes, duckers, pause groups" }, { L"listing", L"Folder listing made by the dumping tool (not a game file)" }, { L"rsizetable", L"Resource size table: memory size of every file in the game" }, { L"raw", L"Raw data (agl arrow.raw: the 32x32 mouse pointer image - Textures button)" }, { L"bin", L"Data (sead primitive_renderer: debug-shape shaders with readable assembly / SPIR-V)" }, { L"bgsdw", L"Lighting: shadow light, projected shadow, ambient occlusion - Lighting button" }, { L"baglmf", L"UI blur filter chain (shrink, blur, enlarge, colour) - settings listed" },
        { L"bphhb", L"Helper bones: which bones the game moves automatically from other bones' rotation (JSON)" },
        { L"msbp", L"Message project: text colours used by the game's messages (JSON)" },
        { L"baatn", L"Sound attenuation preset (which distance curves a sound uses)" }, { L"baroc", L"Sound roll-off curve (volume by distance)" },
        { L"baudc", L"Sound log falloff curve (the ...Log presets)" }, { L"baacl", L"Sound distance low-pass / cut-off curve" },
        { L"baglshpp", L"Lighting: shadow settings - Lighting button" }, { L"bgenv", L"Lighting: list of a level's environment files - Lighting button" },
        { L"genvres", L"Lighting: environment textures - Textures button" },
        { L"genvb", L"Lighting archive (a level's environment settings) - Lighting button" },
        { L"bfevfl", L"Event script: conversation / cutscene / shop - Event button shows it with the dialogue" },
        { L"asb", L"Animation state graph (animation names)" }, { L"baev", L"Animation events (animation names)" },
        { L"listing", L".listing file (list of files)" },
        { L"bphsh", L"Collision shape (Phive/Havok) - 3D view" },
        { L"bphsc", L"Level static collision (Phive compound) - 3D view; some are empty or a water grid (JSON only)" },
        { L"bphcl", L"Cloth / hair physics (Phive/Havok Cloth) - 3D view" },
        { L"bflan", L"UI layout animation - Layout button plays it" }, { L"bflyt", L"UI screen layout - Layout button shows it" },

        { L"executable", L"Program code (NSO): main = the game, sdk = Nintendo SDK, rtld = loader" },
        { L"npdm", L"Program settings and permissions (META)" },
        { L"tscb", L"Terrain scene: every terrain square, its layers and materials - 3D view of the whole terrain (Mario Kart World)" },
        { L"hght", L"Terrain tile heights - Textures button (picture), 3D view" },
        { L"norm", L"Terrain tile surface normals - Textures button" },
        { L"mate", L"Terrain tile ground materials - Textures button" },
        { L"extm", L"Terrain tile extra layer (colour, baked light, water, slow-down...) - Textures button" },
        { L"dlpl", L"Baked light probes (list)" }, { L"dlpg", L"Baked light probe grid" }, { L"dlpa", L"Baked light probe area" },
        { L"dlpr", L"Baked light probes (whole world)" }, { L"dalp", L"Baked light probes for one object" },
        { L"bkls", L"Baked light sources: point and spot lights" }, { L"dcgm", L"Baked light data (empty)" },
        { L"bcul", L"Visibility: objects that can be seen from this cell" }, { L"bbox", L"Visibility: every object's box" },
        { L"bgrd", L"Visibility grid (on/off cells, drawn as # and .)" }, { L"pccg", L"Visibility grid data (object IDs + cells)" },
        { L"dapg", L"Sound reflection (acoustic probe) grid" }, { L"ghost", L"Staff ghost replay (recording date shown)" },
        { L"ainb", L"AI / game-flow node graph" }, { L"xtx", L"Plain NVN texture (sead debug font) - Textures button" },
        { L"baatarc", L"Sound attenuation curves (archive)" }, { L"sharc", L"agl shader source archive (GLSL text)" },
        // Nintendo Switch Sports Resort (NSSR)
        { L"bwr", L"Bowling replay (a recorded throw): frames, pin flags, key frames (the movement isn't decoded yet)" },
        { L"gswg", L"Golf swing replay: two values per frame (their meaning isn't fully known)" },
        { L"bkprmb", L"Baked light probes: a scene's probe grid (corner, block size, blocks)" },
        { L"bkprb", L"Baked light probes: one block's cells (the light values aren't decoded yet)" },
        { L"bkprcb", L"Baked light probes: the scene's coarse probes kept loaded (cells; values not decoded yet)" },
        { L"dat", L"Mii character (Mii\\*.dat: name and every face / body setting); Lib\\NintendoSDK\\IconBody.dat = Mii icon bodies" },
        { L"(no extension)", L"No extension: in NSSR, VoiceText speech-engine data (dictionaries, word lists, hash tables)" },
        { L"rs", L"VoiceText speech engine: compiled pronunciation dictionary (identified, not decoded)" },
        { L"tree3", L"VoiceText speech engine: decision tree (identified, not decoded)" },
        { L"win", L"VoiceText speech engine: window table (numbers listed)" },
        { L"txt2", L"VoiceText speech engine: word list (lightly scrambled - decoded in full)" },
        { L"csv", L"VoiceText user dictionary: how the game wants names and words said (decoded in full)" },
        { L"ini", L"VoiceText voice setup (text): each voice's folder, language, code page, gender and language code" },
        { L"lst", L"VoiceText voice: phoneme names (bp.lst) or accent / stress marks (ba.lst) - lightly scrambled, decoded" },
        { L"pat", L"VoiceText voice: phoneme pattern rules (l.pat) - lightly scrambled, decoded" },
        { L"dic", L"VoiceText user dictionary (Japanese): written form, reading, word kind - lightly scrambled, decoded" },
        { L"bvege", L"Grass / plant settings for a scene (AAMP parameters)" },
        { L"lex", L"VoiceText: how the game's button icons in text are read aloud (e.g. the A-button icon = \"A button\") - text" },
    };
    for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) if (!wcscmp(d[i][0], t)) return d[i][1];
    return L"";
}
static int g_tdSortCol = 1, g_tdSortDesc = 1;     // File types window: sort column (0 type, 1 files, 2 what it is)
static int CALLBACK TypesDlgCmp(LPARAM a, LPARAM b, LPARAM lv) {
    wchar_t ta[300], tb[300];
    ListView_GetItemText((HWND)lv, (int)a, g_tdSortCol, ta, 300);
    ListView_GetItemText((HWND)lv, (int)b, g_tdSortCol, tb, 300);
    int r;
    if (g_tdSortCol == 1) { long long x = _wtoi64(ta), y = _wtoi64(tb); r = x < y ? -1 : x > y; }
    else if (!ta[0] != !tb[0]) return ta[0] ? -1 : 1;      // empty descriptions always last
    else r = _wcsicmp(ta, tb);
    if (!r) {                                               // ties: by type name
        ListView_GetItemText((HWND)lv, (int)a, 0, ta, 300); ListView_GetItemText((HWND)lv, (int)b, 0, tb, 300);
        return _wcsicmp(ta, tb);
    }
    return g_tdSortDesc ? -r : r;
}
static void TypesDlgSort(HWND dlg) {
    HWND lv = GetDlgItem(dlg, TD_LIST);
    ListView_SortItemsEx(lv, TypesDlgCmp, (LPARAM)lv);
    HWND hd = ListView_GetHeader(lv);
    for (int i = 0; i < 3; i++) {
        HDITEMW it = { 0 }; it.mask = HDI_FORMAT;
        SendMessageW(hd, HDM_GETITEMW, i, (LPARAM)&it);
        it.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g_tdSortCol) it.fmt |= g_tdSortDesc ? HDF_SORTDOWN : HDF_SORTUP;
        SendMessageW(hd, HDM_SETITEMW, i, (LPARAM)&it);
    }
}
static void TypesDlgFill(HWND dlg) {
    HWND lv = GetDlgItem(dlg, TD_LIST), info = GetDlgItem(dlg, TD_INFO);
    ListView_DeleteAllItems(lv);
    BOOL ready = g_idx != NULL;
    for (int id = TD_SOUNDS; id <= TD_NONE; id++) EnableWindow(GetDlgItem(dlg, id), ready);
    EnableWindow(GetDlgItem(dlg, IDOK), ready);
    if (!ready) {
        wchar_t t[200]; swprintf(t, 200, L"Finding the file types in this scan... (this is saved, so it only happens once per scan)");
        SetWindowTextW(info, t); return;
    }
    wchar_t t[300];
    swprintf(t, 300, L"%u files, %d types. Tick the types to show - the tree, folder view and search results will only "
                     L"show those files, and folders without any of them are hidden.", g_idx->files, g_idx->nt);
    SetWindowTextW(info, t);
    int ord[MAXTYPES]; int n = g_idx->nt;
    for (int i = 0; i < n; i++) ord[i] = i;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (g_idx->cnt[ord[j]] > g_idx->cnt[ord[i]]) { int x = ord[i]; ord[i] = ord[j]; ord[j] = x; }
    for (int k = 0; k < n; k++) {
        int id = ord[k];
        LVITEMW it = { 0 }; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = k; it.pszText = g_idx->name[id]; it.lParam = id;
        int row = (int)SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        wchar_t c[32]; swprintf(c, 32, L"%u", g_idx->cnt[id]);
        LVITEMW s1 = { 0 }; s1.iSubItem = 1; s1.pszText = c; SendMessageW(lv, LVM_SETITEMTEXTW, row, (LPARAM)&s1);
        LVITEMW s2 = { 0 }; s2.iSubItem = 2; s2.pszText = (LPWSTR)TypeDesc(g_idx->name[id]); SendMessageW(lv, LVM_SETITEMTEXTW, row, (LPARAM)&s2);
        ListView_SetCheckState(lv, row, g_filterOn && SelHas(g_idx->name[id]));
    }
    TypesDlgSort(dlg);
}
static void TypesDlgCheck(HWND dlg, const wchar_t *const *types, int nt, int all) {
    HWND lv = GetDlgItem(dlg, TD_LIST);
    int n = ListView_GetItemCount(lv);
    for (int r = 0; r < n; r++) {
        if (all >= 0) { ListView_SetCheckState(lv, r, all); continue; }
        wchar_t t[TYPELEN]; ListView_GetItemText(lv, r, 0, t, TYPELEN);
        for (int i = 0; i < nt; i++) if (!wcscmp(t, types[i])) ListView_SetCheckState(lv, r, TRUE);
    }
}
static LRESULT CALLBACK TypesDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = GetModuleHandleW(NULL);
        struct { const wchar_t *cls, *txt; DWORD st, ex; int id; } c[] = {
            { L"STATIC", L"", 0, 0, TD_INFO },
            { WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP, WS_EX_CLIENTEDGE, TD_LIST },
            { L"BUTTON", L"+ Sounds", BS_PUSHBUTTON | WS_TABSTOP, 0, TD_SOUNDS },
            { L"BUTTON", L"+ 3D", BS_PUSHBUTTON | WS_TABSTOP, 0, TD_3D },
            { L"BUTTON", L"+ Text", BS_PUSHBUTTON | WS_TABSTOP, 0, TD_TEXT },
            { L"BUTTON", L"Tick all", BS_PUSHBUTTON | WS_TABSTOP, 0, TD_ALL },
            { L"BUTTON", L"Untick all", BS_PUSHBUTTON | WS_TABSTOP, 0, TD_NONE },
            { L"BUTTON", L"Show all types (no filter)", BS_PUSHBUTTON | WS_TABSTOP, 0, TD_OFF },
            { L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, IDOK },
            { L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, IDCANCEL },
        };
        for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
            HWND h = CreateWindowExW(c[i].ex, c[i].cls, c[i].txt, WS_CHILD | WS_VISIBLE | c[i].st, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)c[i].id, inst, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
        }
        HWND lv = GetDlgItem(hwnd, TD_LIST);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t *hd[3] = { L"Type", L"Files", L"What it is" }; int wd[3] = { 150, 80, 300 };
        for (int i = 0; i < 3; i++) {
            LVCOLUMNW col = { 0 }; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | (i == 1 ? LVCF_FMT : 0);
            col.fmt = LVCFMT_RIGHT; col.pszText = (LPWSTR)hd[i]; col.cx = S(wd[i]); col.iSubItem = i;
            SendMessageW(lv, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
        }
        TypesDlgFill(hwnd);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp), m = S(10), g = S(6), bh = S(26);
        MoveWindow(GetDlgItem(hwnd, TD_INFO), m, m, w - 2 * m, S(36), TRUE);
        int ly = m + S(40), lh = h - ly - 2 * bh - 2 * g - m;
        MoveWindow(GetDlgItem(hwnd, TD_LIST), m, ly, w - 2 * m, lh, TRUE);
        int y = ly + lh + g, x = m;
        int ids[5] = { TD_SOUNDS, TD_3D, TD_TEXT, TD_ALL, TD_NONE }, bw[5] = { 90, 70, 70, 80, 90 };
        for (int i = 0; i < 5; i++) { MoveWindow(GetDlgItem(hwnd, ids[i]), x, y, S(bw[i]), bh, TRUE); x += S(bw[i]) + g; }
        y += bh + g;
        MoveWindow(GetDlgItem(hwnd, TD_OFF), m, y, S(190), bh, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDCANCEL), w - m - S(85), y, S(85), bh, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDOK), w - m - S(85) - g - S(85), y, S(85), bh, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = S(560); mm->ptMinTrackSize.y = S(360); return 0; }
    case WM_APP_TYPEIDX: TypesDlgFill(hwnd); return 0;
    case WM_APP_TYPEPROG: {
        if (g_idx) return 0;
        wchar_t t[200]; swprintf(t, 200, L"Finding the file types in this scan... %u files so far (saved afterwards, so this only happens once per scan)", (unsigned)wp);
        SetWindowTextW(GetDlgItem(hwnd, TD_INFO), t); return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case TD_SOUNDS: { const wchar_t *t[] = { L"bars", L"bwav" }; TypesDlgCheck(hwnd, t, 2, -1); return 0; }
        case TD_3D:     { const wchar_t *t[] = { L"vgeob", L"voxel terrain", L"bphsh", L"bphsc" }; TypesDlgCheck(hwnd, t, 4, -1); return 0; }
        case TD_TEXT:   { const wchar_t *t[] = { L"msbt" }; TypesDlgCheck(hwnd, t, 1, -1); return 0; }
        case TD_ALL:  TypesDlgCheck(hwnd, NULL, 0, 1); return 0;
        case TD_NONE: TypesDlgCheck(hwnd, NULL, 0, 0); return 0;
        case TD_OFF:
            g_filterOn = FALSE; DestroyWindow(hwnd); ApplyFilterChange(); return 0;
        case IDOK: {
            if (!g_idx) return 0;
            HWND lv = GetDlgItem(hwnd, TD_LIST);
            int n = ListView_GetItemCount(lv), ns = 0, all = 1;
            wchar_t sel[MAXTYPES][TYPELEN];
            for (int r = 0; r < n && ns < MAXTYPES; r++) {
                if (ListView_GetCheckState(lv, r)) { ListView_GetItemText(lv, r, 0, sel[ns], TYPELEN); ns++; }
                else all = 0;
            }
            if (ns && !all) { memcpy(g_sel, sel, sizeof(sel[0]) * ns); g_nsel = ns; g_filterOn = TRUE; }
            else g_filterOn = FALSE;                 // nothing or everything ticked = no filter
            DestroyWindow(hwnd); ApplyFilterChange(); return 0;
        }
        case IDCANCEL: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->idFrom == TD_LIST && nh->code == LVN_COLUMNCLICK) {   // click a heading: sort by it (again = reverse)
            int c = ((NMLISTVIEW *)lp)->iSubItem;
            if (c == g_tdSortCol) g_tdSortDesc = !g_tdSortDesc;
            else { g_tdSortCol = c; g_tdSortDesc = (c == 1); }   // files: biggest first; text: A-Z
            TypesDlgSort(hwnd);
            return 0;
        }
        if (nh->idFrom == TD_LIST && nh->code == NM_DBLCLK) {   // double-click: just this type
            NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lp;
            if (ia->iItem >= 0) { TypesDlgCheck(hwnd, NULL, 0, 0); ListView_SetCheckState(GetDlgItem(hwnd, TD_LIST), ia->iItem, TRUE); }
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_typesDlg = NULL; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void ShowTypesDialog(void) {
    if (!g_root[0]) { MessageBoxW(g_wnd, L"Open a scan first (pick it from the Scan name list).", L"ns2dig", MB_ICONINFORMATION); return; }
    if (g_typesDlg) { SetForegroundWindow(g_typesDlg); return; }
    if (!g_idx && !g_idxBuilding) StartTypeIndex();
    static int reg = 0;
    HINSTANCE inst = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = TypesDlgProc; wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"AlsarcTypes"; RegisterClassExW(&wc); reg = 1;
    }
    RECT r; GetWindowRect(g_wnd, &r);
    int w = S(600), h = S(560);
    g_typesDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AlsarcTypes", L"Show only these file types",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
                                 r.left + (r.right - r.left - w) / 2, r.top + (r.bottom - r.top - h) / 2, w, h, g_wnd, NULL, inst, NULL);
    if (!g_typesDlg) return;
    HWND dlg = g_typesDlg;
    UiSkinWindow(dlg);                          // the main window's dark look
    ShowWindow(dlg, SW_SHOW);
    SetFocus(GetDlgItem(dlg, TD_LIST));
    EnableWindow(g_wnd, FALSE);
    MSG msg; int ret;
    while (IsWindow(dlg) && (ret = GetMessageW(&msg, NULL, 0, 0)) != 0) {
        if (ret == -1) break;
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_wnd, TRUE);
    SetActiveWindow(g_wnd);
    if (ret == 0) PostQuitMessage((int)msg.wParam);
}

/* ------------------------------------------------------------------ 3D view of voxel terrain parts */
static BOOL FileThere(const wchar_t *p) {
    wchar_t lp[PATHBUF + 16]; LongPath(p, lp, PATHBUF + 16);
    return GetFileAttributesW(lp) != INVALID_FILE_ATTRIBUTES;
}

// a terrain part folder in the output (has Info.bin.json and vbody.bin.json)? path = a file in it or the folder itself
static BOOL VoxPartOutDir(const wchar_t *path, BOOL isDir, wchar_t *outDir) {
    if (!path || !path[0] || !g_root[0]) return FALSE;
    wchar_t d[PATHBUF]; swprintf(d, PATHBUF, L"%ls", path);
    if (!isDir) { wchar_t *bs = wcsrchr(d, L'\\'); if (!bs) return FALSE; *bs = 0; }
    wchar_t a[PATHBUF + 32], b[PATHBUF + 32];
    swprintf(a, PATHBUF + 32, L"%ls\\Info.bin.json", d);
    swprintf(b, PATHBUF + 32, L"%ls\\vbody.bin.json", d);
    if (!FileThere(a) || !FileThere(b)) return FALSE;
    if (outDir) swprintf(outDir, PATHBUF, L"%ls", d);
    return TRUE;
}

// double-click on a file: open the viewer its button would (sound, textures, layout, font, 3D); FALSE if none
static BOOL HasViewer(const wchar_t *path) {
    if (!path || !path[0]) return FALSE;
    return IsSoundOut(path) || IsTexOut(path, FALSE) || IsLytOut(path, FALSE) || IsFontOut(path, FALSE) || IsLgtOut(path, FALSE) || IsEvtOut(path) || Open3DFor(path, FALSE, FALSE);
}
static BOOL OpenViewerFor(const wchar_t *path) {
    if (!path || !path[0]) return FALSE;
    if (IsSoundOut(path)) { OpenSound(path); return TRUE; }
    if (IsTexOut(path, FALSE)) { OpenTex(path); return TRUE; }
    if (IsLytOut(path, FALSE)) { OpenLyt(path); return TRUE; }
    if (IsFontOut(path, FALSE)) { OpenFont(path); return TRUE; }
    if (IsLgtOut(path, FALSE)) { OpenLgt(path, FALSE); return TRUE; }
    if (IsEvtOut(path)) { OpenEvt(path); return TRUE; }
    if (Open3DFor(path, FALSE, FALSE)) { Open3DFor(path, FALSE, TRUE); return TRUE; }
    return FALSE;
}
// viewer buttons: only the ones that fit what's shown are visible (re-laid out when that set changes)
static void UpdateVox3DButton(void) {
    BOOL on[7];
    on[0] = g_cur[0] ? Open3DFor(g_cur, FALSE, FALSE) : (g_curDir[0] && Open3DFor(g_curDir, TRUE, FALSE));
    on[1] = g_cur[0] && IsSoundOut(g_cur);
    on[2] = g_cur[0] && IsTexOut(g_cur, FALSE);
    on[3] = g_cur[0] && IsLytOut(g_cur, FALSE);
    on[4] = g_cur[0] && IsFontOut(g_cur, FALSE);
    on[5] = g_cur[0] && IsEvtOut(g_cur);
    on[6] = g_cur[0] ? IsLgtOut(g_cur, FALSE) : (g_curDir[0] && IsLgtOut(g_curDir, TRUE));
    HWND b[7] = { g_vox3d, g_playsnd, g_viewtex, g_viewlyt, g_viewfnt, g_viewevt, g_viewlgt };
    BOOL changed = FALSE;
    for (int i = 0; i < 7; i++) {
        EnableWindow(b[i], on[i]);
        if (!IsWindowVisible(b[i]) != !on[i]) { ShowWindow(b[i], on[i] ? SW_SHOW : SW_HIDE); changed = TRUE; }
    }
    if (changed && g_wnd) { RECT rc; GetClientRect(g_wnd, &rc); Layout(rc.right, rc.bottom); }
    UpdateBmStar();
}

// the folder this scan was extracted from: remembered choice, else the scan log's "from:" line
static BOOL ScanSource(const wchar_t *scan, wchar_t *out) {
    GetPrivateProfileStringW(L"sources", scan, L"", out, PATHBUF, g_ini);
    if (out[0]) return TRUE;
    wchar_t lp[PATHBUF]; ScanLogPath(g_root, lp, PATHBUF);
    HANDLE h = CreateFileW(lp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    char buf[8192]; DWORD got = 0; ReadFile(h, buf, sizeof(buf) - 1, &got, NULL); CloseHandle(h); buf[got] = 0;
    char *f = strstr(buf, "from:");
    if (!f) return FALSE;
    f += 5; while (*f == '\r' || *f == '\n' || *f == ' ') f++;
    char *e = f; while (*e && *e != '\r' && *e != '\n') e++;
    *e = 0;
    int k = MultiByteToWideChar(CP_UTF8, 0, f, -1, out, PATHBUF);
    return k > 1;
}

// the original in the game dump of something in the output, without asking:
// 0 = not found (srcKnown says whether the dump folder is known), 1 = a file, 2 = a folder,
// 3 = it was inside an archive: out = that archive file
static int DumpOrigNoAsk(const wchar_t *outPath, BOOL isDir, wchar_t *out, BOOL *srcKnown) {
    *srcKnown = FALSE; out[0] = 0;
    size_t rl = wcslen(g_root);
    if (!g_root[0] || _wcsnicmp(outPath, g_root, rl) != 0) return 0;
    wchar_t rel[PATHBUF]; swprintf(rel, PATHBUF, L"%ls", outPath + rl + (outPath[rl] == L'\\'));
    if (!isDir) { size_t l = wcslen(rel); if (l > 5 && !_wcsicmp(rel + l - 5, L".json")) rel[l - 5] = 0; }
    wchar_t scan[PATHBUF], src[PATHBUF] = L""; ScanNameOf(g_root, scan, PATHBUF);
    if (!ScanSource(scan, src)) return 0;
    size_t sl = wcslen(src); while (sl > 3 && src[sl - 1] == L'\\') src[--sl] = 0;
    wchar_t lp[PATHBUF + 16]; LongPath(src, lp, PATHBUF + 16);
    DWORD at = GetFileAttributesW(lp);
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    *srcKnown = TRUE;
    wchar_t cand[PATHBUF];
    swprintf(cand, PATHBUF, L"%ls%ls%ls", src, rel[0] ? L"\\" : L"", rel);
    BOOL first = TRUE;
    for (;;) {                                         // the path itself, else the archive it was inside
        LongPath(cand, lp, PATHBUF + 16);
        at = GetFileAttributesW(lp);
        if (at != INVALID_FILE_ATTRIBUTES) {
            if (first) { swprintf(out, PATHBUF, L"%ls", cand); return (at & FILE_ATTRIBUTE_DIRECTORY) ? 2 : 1; }
            if (!(at & FILE_ATTRIBUTE_DIRECTORY)) { swprintf(out, PATHBUF, L"%ls", cand); return 3; }
            return 0;                                  // a real folder above it, but the thing itself is missing
        }
        wchar_t *bs = wcsrchr(cand, L'\\');
        if (!bs || (size_t)(bs - cand) <= sl) return 0;
        *bs = 0; first = FALSE;
    }
}
BOOL UiPickFolder(HWND owner, const wchar_t *start, const wchar_t *title, wchar_t *out, size_t n);
/* while an extraction runs every control except Stop (and the log) is disabled; their states come back after */
static HWND g_lockWnd[256]; static BOOL g_lockOn[256]; static int g_nLock;
static BOOL CALLBACK LockOne(HWND h, LPARAM lp) {
    (void)lp;
    if (h == g_extract || h == g_view || h == g_progress || h == g_status || g_nLock >= 256) return TRUE;
    wchar_t cls[32]; GetClassNameW(h, cls, 32);
    if (!_wcsicmp(cls, L"Static") || !_wcsicmp(cls, PROGRESS_CLASSW) || GetParent(h) != g_wnd) return TRUE;
    g_lockWnd[g_nLock] = h; g_lockOn[g_nLock] = IsWindowEnabled(h); g_nLock++;
    EnableWindow(h, FALSE);
    return TRUE;
}
static void LockUi(BOOL lock) {
    if (lock) { g_nLock = 0; EnumChildWindows(g_wnd, LockOne, 0); }
    else { for (int i = 0; i < g_nLock; i++) if (IsWindow(g_lockWnd[i])) EnableWindow(g_lockWnd[i], g_lockOn[i]); g_nLock = 0; }
}
static void AskDumpFolder(void) {
    wchar_t scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF);
    wchar_t start[PATHBUF] = L""; ScanSource(scan, start);
    wchar_t chosen[PATHBUF];
    if (UiPickFolder(g_wnd, start, L"Choose the folder this scan was extracted from (the Game folder you picked)", chosen, PATHBUF)) { WritePrivateProfileStringW(L"sources", scan, chosen, g_ini); SetStatus(L"Game dump folder saved for this scan."); }
}

// map something in the output (a folder, or a .json file) back to the game dump.
// mustExist: name of a file that has to exist inside a folder (NULL for a file).
// Asks for the dump folder if it can't be found; returns FALSE if cancelled.
static BOOL DumpPathFor(const wchar_t *outPath, BOOL isDir, const wchar_t *mustExist, const wchar_t *what, wchar_t *out) {
    size_t rl = wcslen(g_root);
    if (_wcsnicmp(outPath, g_root, rl) != 0) return FALSE;
    wchar_t rel[PATHBUF]; swprintf(rel, PATHBUF, L"%ls", outPath + rl + (outPath[rl] == L'\\'));
    if (!isDir) { size_t l = wcslen(rel); if (l > 5 && !_wcsicmp(rel + l - 5, L".json")) rel[l - 5] = 0; }
    wchar_t scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF);
    for (int attempt = 0; attempt < 3; attempt++) {
        wchar_t src[PATHBUF] = L"", check[PATHBUF + 64];
        BOOL have = ScanSource(scan, src);
        if (have) {
            size_t sl = wcslen(src); while (sl > 3 && src[sl - 1] == L'\\') src[--sl] = 0;
            swprintf(out, PATHBUF, L"%ls%ls%ls", src, (sl && src[sl - 1] == L'\\') ? L"" : L"\\", rel);
            if (mustExist) swprintf(check, PATHBUF + 64, L"%ls\\%ls", out, mustExist); else swprintf(check, PATHBUF + 64, L"%ls", out);
            if (FileThere(check)) return TRUE;
        }
        wchar_t m[PATHBUF * 2 + 500];
        swprintf(m, PATHBUF * 2 + 500,
                 L"The %ls reads from your game dump, but it couldn't find this there:\n\n%ls%ls%ls\n\n"
                 L"Click OK and choose the folder you extracted for the scan \"%ls\" (the one you chose as Game folder - "
                 L"e.g. the dump's top folder or its romfs folder).",
                 what, have ? src : L"(the scan log doesn't say where it came from)", have ? L"\\" : L"", have ? rel : L"", scan);
        if (MessageBoxW(g_wnd, m, L"ns2dig", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return FALSE;
        wchar_t chosen[PATHBUF];
        if (!UiPickFolder(g_wnd, have ? src : L"", L"Choose the folder this scan was extracted from", chosen, PATHBUF)) return FALSE;
        WritePrivateProfileStringW(L"sources", scan, chosen, g_ini);
    }
    return FALSE;
}

static void OpenVox3D(const wchar_t *outDir) {
    wchar_t dir[PATHBUF];
    if (!DumpPathFor(outDir, TRUE, L"Info.bin", L"3D view", dir)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outDir, TRUE, title, PATHBUF);
    SetStatus(L"Opening the 3D view...");
    Vox3DOpen(g_wnd, dir, NULL, title, g_font, g_dpi);
}

// a voxel object in the output: ...\romfs\ObjectData\Voxel\<name>.alsarc.zs (a folder with *.vgeob.json), or
// ...\romfs\Voxel\Geometry\<name>.alsarc.zs. path = that folder or anything inside it.
// outDir = the ObjectData\Voxel\<name>.alsarc.zs folder, geomJson = <name>.al__gen__VoxelGeometry.bgyml.json (grid size)
static BOOL VoxObjOutDir(const wchar_t *path, BOOL isDir, wchar_t *outDir, wchar_t *geomJson) {
    (void)isDir;
    if (!path || !path[0] || !g_root[0]) return FALSE;
    wchar_t low[PATHBUF]; swprintf(low, PATHBUF, L"%ls", path); _wcslwr(low);
    const wchar_t *kinds[2] = { L"\\romfs\\objectdata\\voxel\\", L"\\romfs\\voxel\\geometry\\" };
    for (int k = 0; k < 2; k++) {
        wchar_t *hit = wcsstr(low, kinds[k]);
        if (!hit) continue;
        size_t pre = (size_t)(hit - low) + 6;                         // up to and including "\romfs"
        const wchar_t *nm = path + (hit - low) + wcslen(kinds[k]);
        const wchar_t *end = wcschr(nm, L'\\');
        size_t nl = end ? (size_t)(end - nm) : wcslen(nm);
        if (nl < 12 || nl >= 300 || _wcsnicmp(nm + nl - 10, L".alsarc.zs", 10)) return FALSE;
        wchar_t name[320]; swprintf(name, 320, L"%.*ls", (int)(nl - 10), nm);
        wchar_t od[PATHBUF], pat[PATHBUF + 32];
        swprintf(od, PATHBUF, L"%.*ls\\ObjectData\\Voxel\\%ls.alsarc.zs", (int)pre, path, name);
        swprintf(pat, PATHBUF + 32, L"%ls\\*.vgeob.json", od);
        wchar_t lp[PATHBUF + 64]; LongPath(pat, lp, PATHBUF + 64);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW(lp, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
        if (h == INVALID_HANDLE_VALUE) return FALSE;
        FindClose(h);
        // grid size: same romfs first, then the others (newest update first)
        wchar_t gj[PATHBUF]; BOOL found = FALSE;
        swprintf(gj, PATHBUF, L"%.*ls\\Voxel\\Geometry\\%ls.alsarc.zs\\%ls.al__gen__VoxelGeometry.bgyml.json", (int)pre, path, name, name);
        if (FileThere(gj)) found = TRUE;
        if (!found && geomJson) {
            static wchar_t roots[32][PATHBUF]; int nr = 0;
            FindRomfsRoots(g_root, 0, roots, &nr, 32);
            for (int r = nr - 1; r >= 0 && !found; r--) {
                swprintf(gj, PATHBUF, L"%ls\\Voxel\\Geometry\\%ls.alsarc.zs\\%ls.al__gen__VoxelGeometry.bgyml.json", roots[r], name, name);
                found = FileThere(gj);
            }
        }
        if (outDir) swprintf(outDir, PATHBUF, L"%ls", od);
        if (geomJson) swprintf(geomJson, PATHBUF, L"%ls", found ? gj : L"");
        return TRUE;
    }
    return FALSE;
}

static void OpenVoxObj(const wchar_t *outDir, const wchar_t *geomJson) {
    if (!geomJson[0]) {
        MessageBoxW(g_wnd, L"This voxel object's grid size is in Voxel\\Geometry\\<name>.alsarc.zs, which isn't in this scan.\n\n"
                           L"Extract a scan that includes the Voxel folder (or the whole RomFS) to view it.", L"ns2dig", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t arc[PATHBUF];
    if (!DumpPathFor(outDir, FALSE, NULL, L"3D view", arc)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outDir, TRUE, title, PATHBUF);
    SetStatus(L"Opening the 3D view...");
    Vox3DOpen(g_wnd, arc, geomJson, title, g_font, g_dpi);
}

// a Phive collision shape in the output (X.bphsh.zs.json)
static BOOL IsPhiveOut(const wchar_t *path, BOOL isDir) {
    return !isDir && path && (EndsWithI(path, L".bphsh.zs.json") || EndsWithI(path, L".bphsh.json") ||
                                  EndsWithI(path, L".bphsc.zs.json") || EndsWithI(path, L".bphsc.json"));
}
static BOOL IsClothOut(const wchar_t *path, BOOL isDir) {
    return !isDir && path && (EndsWithI(path, L".bphcl.json") || EndsWithI(path, L".bphcl.zs.json"));
}
// a BFRES model (X.Y.bfres.zs.json - not the X.anim.bfres animation files)
static BOOL IsBfresModelOut(const wchar_t *path, BOOL isDir) {
    if (isDir || !path || !path[0]) return FALSE;
    if (!EndsWithI(path, L".bfres.zs.json") && !EndsWithI(path, L".bfres.json")) return FALSE;
    const wchar_t *f = wcsrchr(path, L'\\'); f = f ? f + 1 : path;
    const wchar_t *a = wcsstr(f, L".anim.bfres"); return a == NULL;
}
static void OpenBfres(const wchar_t *outFile) {
    wchar_t dump[PATHBUF];
    if (!DumpPathFor(outFile, FALSE, NULL, L"3D view", dump)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Opening the 3D view...");
    Vox3DOpen(g_wnd, dump, L"bfres", title, g_font, g_dpi);
}
static void OpenCloth(const wchar_t *outFile) {
    wchar_t dump[PATHBUF];
    if (!DumpPathFor(outFile, FALSE, NULL, L"3D view", dump)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Opening the 3D view...");
    Vox3DOpen(g_wnd, dump, L"cloth", title, g_font, g_dpi);
}
static void OpenPhive(const wchar_t *outFile) {
    wchar_t dump[PATHBUF];
    if (!DumpPathFor(outFile, FALSE, NULL, L"3D view", dump)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Opening the 3D view...");
    Vox3DOpen(g_wnd, dump, L"phive", title, g_font, g_dpi);
}
// Mario Kart World terrain tiles in the output (TerrainArc\<scene>\<area>.hght.json / .norm / .mate / .<kind>.extm)
static BOOL IsTerrainLayerOut(const wchar_t *path) {
    if (!path) return FALSE;                                     // (any folder: Mario Kart World's TerrainArc, the demo's Terrain...)
    return EndsWithI(path, L".hght.json") || EndsWithI(path, L".norm.json") || EndsWithI(path, L".mate.json") || EndsWithI(path, L".extm.json");
}
static BOOL IsTerrainHeightOut(const wchar_t *path, BOOL isDir) {
    return !isDir && path && EndsWithI(path, L".hght.json");
}
// the terrain scene (TerrainArc\<scene>.tscb.json): its tiles stitched into one 3D world
static BOOL IsTerrainSceneOut(const wchar_t *path, BOOL isDir) {
    return !isDir && path && EndsWithI(path, L".tscb.json");
}
static void OpenTerrainWorld3D(const wchar_t *outFile) {
    wchar_t dump[PATHBUF];
    if (!DumpPathFor(outFile, FALSE, NULL, L"3D view", dump)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Stitching the terrain tiles into one world...");
    Vox3DOpen(g_wnd, dump, L"terrainworld", title, g_font, g_dpi);
}
static void OpenTerrain3D(const wchar_t *outFile) {
    wchar_t dump[PATHBUF];
    if (!DumpPathFor(outFile, FALSE, NULL, L"3D view", dump)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Opening the 3D view...");
    Vox3DOpen(g_wnd, dump, L"terrain", title, g_font, g_dpi);
}
// BNTX textures in the output (X.bntx.json, often timg\__Combined.bntx inside a layout archive)
static BOOL IsTexOut(const wchar_t *path, BOOL isDir) {
    return !isDir && path && path[0] && (EndsWithI(path, L".bntx.json") || EndsWithI(path, L".bntx.zs.json") ||
                                          EndsWithI(path, L".genvres.json") ||     // environment textures (a BFRES holding a BNTX)
                                          EndsWithI(path, L"arrow.raw.json") ||    // agl's mouse pointer image (raw 32x32 RGBA)
                                          EndsWithI(path, L".xtx.json") ||          // plain NVN texture (sead debug font)
                                          IsTerrainLayerOut(path));                 // Mario Kart World terrain tile layers
}
// the dump file for an output file, plus its path inside an archive (UTF-8, "" if it isn't in one). FALSE if not found.
static BOOL DumpFileAndInner(const wchar_t *outFile, wchar_t *dump, char *inner, size_t innerN) {
    BOOL known; inner[0] = 0;
    int k = DumpOrigNoAsk(outFile, FALSE, dump, &known);
    if (!k && !known) { AskDumpFolder(); k = DumpOrigNoAsk(outFile, FALSE, dump, &known); }
    if (k != 1 && k != 3) {
        MessageBoxW(g_wnd, known ? L"This file wasn't found in the game dump." : L"The game dump folder for this scan isn't known.", L"ns2dig", MB_ICONINFORMATION);
        return FALSE;
    }
    if (k == 3) {
        wchar_t src[PATHBUF], scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF); ScanSource(scan, src);
        size_t sl = wcslen(src); while (sl > 3 && src[sl - 1] == L'\\') src[--sl] = 0;
        const wchar_t *archRel = dump + sl + 1, *rel = outFile + wcslen(g_root) + 1;
        size_t al = wcslen(archRel);
        if (wcslen(rel) <= al + 1 || _wcsnicmp(rel, archRel, al)) { MessageBoxW(g_wnd, L"Couldn't work out where this file is inside its archive.", L"ns2dig", MB_ICONINFORMATION); return FALSE; }
        wchar_t in[PATHBUF]; swprintf(in, PATHBUF, L"%ls", rel + al + 1);
        size_t l = wcslen(in); if (l > 5 && !_wcsicmp(in + l - 5, L".json")) in[l - 5] = 0;
        for (wchar_t *q = in; *q; q++) if (*q == L'\\') *q = L'/';
        WideCharToMultiByte(CP_UTF8, 0, in, -1, inner, (int)innerN, NULL, NULL);
    }
    return TRUE;
}
// UI layouts and their animations (X.bflyt.json / X.bflan.json, normally inside a layout archive)
static BOOL IsLytOut(const wchar_t *path, BOOL isDir) {
    return !isDir && path && path[0] && (EndsWithI(path, L".bflyt.json") || EndsWithI(path, L".bflan.json"));
}
static void OpenLyt(const wchar_t *outFile) {
    wchar_t dump[PATHBUF]; char inner[PATHBUF];
    if (!DumpFileAndInner(outFile, dump, inner, PATHBUF)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Opening the layout...");
    LytViewOpen(g_wnd, dump, inner[0] ? inner : NULL, title, g_font, g_dpi);
}
// the game's fonts (inside Font\Font*.bfarc): real fonts, bitmap fonts and font combinations
static BOOL IsFontOut(const wchar_t *path, BOOL isDir) {
    return !isDir && path && path[0] && (EndsWithI(path, L".bfotf.json") || EndsWithI(path, L".bfttf.json") || EndsWithI(path, L".bffnt.json") || EndsWithI(path, L".bfcpx.json") ||
                                          ((EndsWithI(path, L".otf.json") || EndsWithI(path, L".ttf.json")) && StrStrIW(path, L"\\scft\\")) ||
                                          EndsWithI(path, L".bfotf.zs.json") || EndsWithI(path, L".bfttf.zs.json") || EndsWithI(path, L".bffnt.zs.json"));
}
// lighting: the AAMP files of an Env\X.genvb.zs archive (or the archive's folder in the tree)
static BOOL IsLgtOut(const wchar_t *path, BOOL isDir) {
    if (!path || !path[0] || !wcsstr(path, L".genvb")) return FALSE;
    if (isDir) return EndsWithI(path, L".genvb.zs") || EndsWithI(path, L".genvb");
    static const wchar_t *ext[] = { L".bprojgfxbase.json", L".bssprojgfxbase.json", L".baglblm.json", L".bagldof.json", L".baglccr.json",
                                    L".baglenv.json", L".baglenvset.json", L".baglsky.json", L".baglshpp.json", L".bgenv.json", L".baglcube.json", L".baglfila.json", L".bofx.json", L".bgsdw.json", L".baglvolm.json", L".baglatex.json", L".bagllmap.json" };
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++) if (EndsWithI(path, ext[i])) return TRUE;
    return FALSE;
}
static void OpenLgt(const wchar_t *path, BOOL isDir) {
    wchar_t dump[PATHBUF]; char inner[PATHBUF] = "";
    if (isDir) {
        BOOL known; int k = DumpOrigNoAsk(path, TRUE, dump, &known);
        if (!k && !known) { AskDumpFolder(); k = DumpOrigNoAsk(path, TRUE, dump, &known); }
        if (k != 1) { MessageBoxW(g_wnd, known ? L"This archive wasn't found in the game dump." : L"The game dump folder for this scan isn't known.", L"ns2dig", MB_ICONINFORMATION); return; }
    } else if (!DumpFileAndInner(path, dump, inner, PATHBUF)) return;
    const wchar_t *bs = wcsrchr(dump, L'\\'); wchar_t title[PATHBUF]; swprintf(title, PATHBUF, L"%ls", bs ? bs + 1 : dump);
    wchar_t *dot = wcsstr(title, L".Nin_"); if (dot) *dot = 0; else if ((dot = wcsstr(title, L".genvb"))) *dot = 0;
    SetStatus(L"Opening the lighting settings...");
    LgtViewOpen(g_wnd, dump, inner, title, g_font, g_dpi);
    SetStatus(L"");
}
static BOOL IsEvtOut(const wchar_t *path) { return path && (EndsWithI(path, L".bfevfl.json") || EndsWithI(path, L".bfevfl.zs.json")); }
static void OpenEvt(const wchar_t *path) {
    wchar_t dump[PATHBUF]; char inner[PATHBUF];
    if (!DumpFileAndInner(path, dump, inner, PATHBUF)) return;
    if (inner[0]) { MessageBoxW(g_wnd, L"This event script is inside an archive; the event viewer reads loose Event\\EventFlow files.", L"ns2dig", MB_ICONINFORMATION); return; }
    const wchar_t *bs = wcsrchr(dump, L'\\'); wchar_t title[PATHBUF]; swprintf(title, PATHBUF, L"%ls", bs ? bs + 1 : dump);
    wchar_t *dot = wcsstr(title, L".bfevfl"); if (dot) *dot = 0;
    SetStatus(L"Opening the event script...");
    EvtViewOpen(g_wnd, dump, title, g_font, g_dpi);
    SetStatus(L"");
}
static void OpenFont(const wchar_t *outFile) {
    wchar_t dump[PATHBUF]; char inner[PATHBUF];
    if (!DumpFileAndInner(outFile, dump, inner, PATHBUF)) return;
    if (!inner[0]) {     // a loose font file (Mario Kart World: romfs\Font\X.bfotf.zs, X.bfcpx): open its folder
        wchar_t *sl = wcsrchr(dump, L'\\'); if (!sl) return;
        char nm[300]; snprintf(nm, sizeof nm, "%ls", sl + 1); *sl = 0;
        size_t nl = strlen(nm); if (nl > 3 && !_stricmp(nm + nl - 3, ".zs")) nm[nl -= 3] = 0;
        if (strstr(nm, ".bfcpx")) { char *d = strchr(nm, '.'); if (d) *d = 0; snprintf(inner, PATHBUF, "fcpx/%s.bfcpx", nm); }
        else if (strstr(nm, ".bffnt")) snprintf(inner, PATHBUF, "font/%s", nm);
        else snprintf(inner, PATHBUF, "scft/%s", nm);
    }
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Opening the font...");
    FontViewOpen(g_wnd, dump, inner, title, g_font, g_dpi);
    SetStatus(L"");
}
static void OpenTex(const wchar_t *outFile) {
    wchar_t dump[PATHBUF]; BOOL known;
    int k = DumpOrigNoAsk(outFile, FALSE, dump, &known);
    if (!k && !known) { AskDumpFolder(); k = DumpOrigNoAsk(outFile, FALSE, dump, &known); }
    if (k != 1 && k != 3) {
        MessageBoxW(g_wnd, known ? L"This file wasn't found in the game dump." : L"The game dump folder for this scan isn't known.", L"ns2dig", MB_ICONINFORMATION);
        return;
    }
    char inner[PATHBUF] = "";
    if (k == 3) {                          // inside an archive: its path within it
        wchar_t src[PATHBUF], scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF); ScanSource(scan, src);
        size_t sl = wcslen(src); while (sl > 3 && src[sl - 1] == L'\\') src[--sl] = 0;
        const wchar_t *archRel = dump + sl + 1;
        const wchar_t *rel = outFile + wcslen(g_root) + 1;
        size_t al = wcslen(archRel);
        if (wcslen(rel) <= al + 1 || _wcsnicmp(rel, archRel, al)) { MessageBoxW(g_wnd, L"Couldn't work out where this file is inside its archive.", L"ns2dig", MB_ICONINFORMATION); return; }
        wchar_t in[PATHBUF]; swprintf(in, PATHBUF, L"%ls", rel + al + 1);
        size_t l = wcslen(in); if (l > 5 && !_wcsicmp(in + l - 5, L".json")) in[l - 5] = 0;
        for (wchar_t *q = in; *q; q++) if (*q == L'\\') *q = L'/';
        WideCharToMultiByte(CP_UTF8, 0, in, -1, inner, PATHBUF, NULL, NULL);
    }
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    size_t l = wcslen(title); if (l > 5 && !_wcsicmp(title + l - 5, L".json")) title[l - 5] = 0;
    SetStatus(L"Opening the textures...");
    TexViewOpen(g_wnd, dump, k == 3 ? inner : NULL, title, g_font, g_dpi);
}

// opens whichever 3D view fits the path; FALSE if none
static BOOL Open3DFor(const wchar_t *path, BOOL isDir, BOOL doOpen) {
    wchar_t od[PATHBUF], gj[PATHBUF];
    if (IsPhiveOut(path, isDir)) { if (doOpen) OpenPhive(path); return TRUE; }
    if (IsTerrainHeightOut(path, isDir)) { if (doOpen) OpenTerrain3D(path); return TRUE; }
    if (IsTerrainSceneOut(path, isDir)) { if (doOpen) OpenTerrainWorld3D(path); return TRUE; }
    if (IsClothOut(path, isDir)) { if (doOpen) OpenCloth(path); return TRUE; }
    if (IsBfresModelOut(path, isDir)) { if (doOpen) OpenBfres(path); return TRUE; }
    if (VoxPartOutDir(path, isDir, od)) { if (doOpen) OpenVox3D(od); return TRUE; }
    if (VoxObjOutDir(path, isDir, od, doOpen ? gj : NULL)) { if (doOpen) OpenVoxObj(od, gj); return TRUE; }
    return FALSE;
}

// a sound file in the output? (*.bwav.json, *.bars.json, and their .zs versions)
static BOOL IsSoundOut(const wchar_t *path) {
    if (!path || !path[0]) return FALSE;
    return EndsWithI(path, L".bwav.json") || EndsWithI(path, L".bwav.zs.json") || EndsWithI(path, L".bars.json") || EndsWithI(path, L".bars.zs.json") ||
           EndsWithI(path, L".webm.json");             // videos: the Play button opens the video player
}
static void OpenVideo(const wchar_t *outFile) {
    wchar_t dump[PATHBUF]; char inner[PATHBUF];
    if (!DumpFileAndInner(outFile, dump, inner, PATHBUF)) return;
    if (inner[0]) { MessageBoxW(g_wnd, L"This video is inside an archive; the player reads loose Movie files.", L"ns2dig", MB_ICONINFORMATION); return; }
    const wchar_t *bs = wcsrchr(dump, L'\\'); wchar_t title[PATHBUF]; swprintf(title, PATHBUF, L"%ls", bs ? bs + 1 : dump);
    wchar_t *dot = wcsstr(title, L".webm"); if (dot) *dot = 0;
    SetStatus(L"Opening the video...");
    VidViewOpen(g_wnd, dump, title, g_font, g_dpi);
    SetStatus(L"");
}

static void OpenSound(const wchar_t *outFile) {
    if (EndsWithI(outFile, L".webm.json")) { OpenVideo(outFile); return; }
    wchar_t f[PATHBUF];
    if (!DumpPathFor(outFile, FALSE, NULL, L"sound player", f)) return;
    wchar_t title[PATHBUF]; ScanRelPath(outFile, FALSE, title, PATHBUF);
    SetStatus(L"Opening the sound player...");
    SndPlayerOpen(g_wnd, f, title, g_font, g_dpi);
}

/* ------------------------------------------------------------------ search */
typedef struct { wchar_t *path; int line; } Result;
typedef struct { LONG gen; int n, cap; Result *items; wchar_t **labels; } Batch;
typedef struct {
    wchar_t root[PATHBUF];      // folder being searched
    wchar_t scanRoot[PATHBUF];  // scan folder, for relative result paths
    char term[1024]; size_t tlen; BOOL wild;
    BOOL mc; LONG gen;
    int files, matches;
    DWORD lastStat, lastMem;
    BOOL filt; int nsel; wchar_t sel[MAXTYPES][TYPELEN];   // file-type filter (copy)
} SearchArgs;
static BOOL SearchTypeOk(const SearchArgs *a, const wchar_t *name) {
    if (!a->filt) return TRUE;
    wchar_t t[TYPELEN]; TypeOfName(name, t);
    for (int i = 0; i < a->nsel; i++) if (!wcscmp(a->sel[i], t)) return TRUE;
    return FALSE;
}

static void BatchAdd(Batch *b, const wchar_t *path, int line, const wchar_t *label) {
    if (b->n == b->cap) {
        int nc = b->cap ? b->cap * 2 : 16;
        Result *ni = (Result *)xalloc(nc * sizeof(Result));
        wchar_t **nl = (wchar_t **)xalloc(nc * sizeof(wchar_t *));
        if (b->n) { memcpy(ni, b->items, b->n * sizeof(Result)); memcpy(nl, b->labels, b->n * sizeof(wchar_t *)); }
        xfree(b->items); xfree(b->labels); b->items = ni; b->labels = nl; b->cap = nc;
    }
    b->items[b->n].path = xwcsdup(path); b->items[b->n].line = line;
    b->labels[b->n] = xwcsdup(label); b->n++;
}

static void FreeBatch(Batch *b) {
    for (int i = 0; i < b->n; i++) { xfree(b->items[i].path); xfree(b->labels[i]); }
    xfree(b->items); xfree(b->labels); xfree(b);
}

static void LowerAscii(char *s, size_t n) { for (size_t i = 0; i < n; i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32; }

static const char *MemFind(const char *h, size_t hn, const char *nd, size_t nn) {
    if (!nn || hn < nn) return NULL;
    const char *end = h + hn - nn + 1;
    for (const char *p = h; p < end; ) {
        p = (const char *)memchr(p, nd[0], end - p);
        if (!p) return NULL;
        if (!memcmp(p, nd, nn)) return p;
        p++;
    }
    return NULL;
}

// wildcards: * = any run of characters, ? = one character; a pattern matches anywhere in the line
// (as if it had * at both ends). Case is handled by the caller (both lowered when not matching case).
static BOOL HasWild(const char *t, size_t n) { for (size_t i = 0; i < n; i++) if (t[i] == '*' || t[i] == '?') return TRUE; return FALSE; }
static BOOL WSegAt(const char *s, size_t n, size_t p, const char *seg, size_t sl, size_t *end) {
    for (size_t k = 0; k < sl; k++) {
        if (p >= n) return FALSE;
        if (seg[k] == '?') { p++; while (p < n && ((unsigned char)s[p] & 0xC0) == 0x80) p++; }   // one UTF-8 character
        else if (s[p] != seg[k]) return FALSE;
        else p++;
    }
    *end = p; return TRUE;
}
static BOOL WildLine(const char *s, size_t n, const char *pat, size_t pl) {
    size_t p = 0, i = 0;
    while (i < pl) {
        if (pat[i] == '*') { i++; continue; }
        size_t j = i; while (j < pl && pat[j] != '*') j++;
        size_t e = 0; BOOL ok = FALSE;
        for (size_t q = p; q < n; q++) if (WSegAt(s, n, q, pat + i, j - i, &e)) { ok = TRUE; break; }
        if (!ok) return FALSE;
        p = e; i = j;
    }
    return TRUE;
}
// the longest stretch without * or ? (used to find candidate lines quickly)
static void WildLiteral(const char *pat, size_t pl, const char **lit, size_t *ll) {
    *lit = NULL; *ll = 0;
    for (size_t i = 0; i < pl;) {
        if (pat[i] == '*' || pat[i] == '?') { i++; continue; }
        size_t j = i; while (j < pl && pat[j] != '*' && pat[j] != '?') j++;
        if (j - i > *ll) { *lit = pat + i; *ll = j - i; }
        i = j;
    }
}

static void SearchFile(SearchArgs *a, const wchar_t *path, const wchar_t *name) {
    Batch *b = (Batch *)xalloc(sizeof(Batch)); b->gen = a->gen;
    const wchar_t *rel = path + wcslen(a->scanRoot) + 1;
    wchar_t relNoExt[PATHBUF]; swprintf(relNoExt, PATHBUF, L"%ls", rel);
    if (EndsWithI(relNoExt, L".json")) relNoExt[wcslen(relNoExt) - 5] = 0;

    // file-name match
    {
        char nm[MAX_PATH * 4];
        int k = WideCharToMultiByte(CP_UTF8, 0, name, -1, nm, sizeof(nm), NULL, NULL);
        if (k > 0) {
            size_t nl = strlen(nm);
            if (nl > 5) nl -= 5;   // ignore ".json"
            if (!a->mc) LowerAscii(nm, nl);
            if (a->wild ? WildLine(nm, nl, a->term, a->tlen) : MemFind(nm, nl, a->term, a->tlen) != NULL) {
                wchar_t lbl[PATHBUF + 32]; swprintf(lbl, PATHBUF + 32, L"%ls   [file name]", relNoExt);
                BatchAdd(b, path, -1, lbl);
            }
        }
    }

    wchar_t lp[PATHBUF + 8]; LongPath(path, lp, PATHBUF + 8);
    HANDLE h = CreateFileW(lp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
        if (sz.QuadPart > 0 && sz.QuadPart < 256LL * 1024 * 1024) {
            size_t n = (size_t)sz.QuadPart;
            char *buf = (char *)xalloc(n + 1); DWORD got = 0;
            if (buf && ReadFile(h, buf, (DWORD)n, &got, NULL)) {
                n = got;
                char *hay = buf;
                char *low = NULL;
                if (!a->mc) { low = (char *)xalloc(n + 1); memcpy(low, buf, n); LowerAscii(low, n); hay = low; }
                size_t pos = 0, lineStart = 0; int line = 0; size_t counted = 0;
                const char *lit = a->term; size_t ll = a->tlen;
                if (a->wild) WildLiteral(a->term, a->tlen, &lit, &ll);
                while (pos < n && a->gen == g_searchGen && !g_searchLowMem) {
                    const char *m = ll ? MemFind(hay + pos, n - pos, lit, ll) : hay + pos;
                    if (!m) break;
                    size_t mp = (size_t)(m - hay);
                    for (; counted < mp; counted++) if (buf[counted] == '\n') { line++; lineStart = counted + 1; }
                    size_t le = mp; while (le < n && buf[le] != '\n' && buf[le] != '\r') le++;
                    if (a->wild && !WildLine(hay + lineStart, le - lineStart, a->term, a->tlen)) { pos = le + 1; continue; }
                    size_t ls = lineStart; while (ls < le && (buf[ls] == ' ' || buf[ls] == '\t')) ls++;
                    size_t cut = le - ls > 200 ? 200 : le - ls;
                    while (cut > 0 && cut < le - ls && (buf[ls + cut] & 0xC0) == 0x80) cut--;   // don't split UTF-8
                    wchar_t snip[512];
                    int k = MultiByteToWideChar(CP_UTF8, 0, buf + ls, (int)cut, snip, 500); snip[k > 0 ? k : 0] = 0;
                    wchar_t lbl[PATHBUF + 640];
                    swprintf(lbl, PATHBUF + 640, L"%ls  :%d   %ls", relNoExt, line + 1, snip);
                    BatchAdd(b, path, line, lbl);
                    pos = le + 1;
                }
                xfree(low);
            }
            xfree(buf);
        }
        CloseHandle(h);
    }
    a->files++;
    if (b->n) { a->matches += b->n; PostMessageW(g_wnd, WM_APP_RESULTS, 0, (LPARAM)b); }
    else FreeBatch(b);
    DWORD now = GetTickCount();
    if (now - a->lastMem > 500) {           // no limit on matches, but stop before the computer runs out of memory
        a->lastMem = now;
        MEMORYSTATUSEX ms = { sizeof ms };
        if (GlobalMemoryStatusEx(&ms) && (ms.ullAvailPhys < 300ull * 1024 * 1024 || ms.dwMemoryLoad >= 95)) InterlockedExchange(&g_searchLowMem, 1);
    }
    if (now - a->lastStat > 150) { a->lastStat = now; PostMessageW(g_wnd, WM_APP_SSTAT, a->files, a->gen); }
}

static void SearchDir(SearchArgs *a, const wchar_t *dir) {
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (a->gen != g_searchGen || g_searchLowMem) break;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t p[PATHBUF];
        swprintf(p, PATHBUF, L"%ls\\%ls", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) SearchDir(a, p);
        else if (EndsWithI(fd.cFileName, L".json") && SearchTypeOk(a, fd.cFileName)) SearchFile(a, p, fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int CountJson(SearchArgs *a, const wchar_t *dir) {
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd; int n = 0;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (a->gen != g_searchGen) break;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wchar_t p[PATHBUF]; swprintf(p, PATHBUF, L"%ls\\%ls", dir, fd.cFileName); n += CountJson(a, p);
        } else if (EndsWithI(fd.cFileName, L".json") && SearchTypeOk(a, fd.cFileName)) n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

static DWORD SearchWorkerBody(LPVOID p);
static DWORD WINAPI SearchWorker(LPVOID p) {          // counted, so closing the app can wait for it to finish
    InterlockedIncrement(&g_workers); CrashGuardThread();
    DWORD r = SearchWorkerBody(p);
    InterlockedDecrement(&g_workers);
    return r;
}
static DWORD SearchWorkerBody(LPVOID p) {
    SearchArgs *a = (SearchArgs *)p;
    int total = CountJson(a, a->root);                     // quick first pass so the progress bar has a total
    PostMessageW(g_wnd, WM_APP_STOTAL, total, a->gen);
    SearchDir(a, a->root);
    PostMessageW(g_wnd, WM_APP_SSTAT, a->files, a->gen);   // final "Searched N files"
    PostMessageW(g_wnd, WM_APP_SDONE, 0, a->gen);
    xfree(a);
    return 0;
}

// emptying the tree / results list with drawing switched off: item by item with redraws takes seconds on a big scan
static void TreeClearFast(void) {
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_tree);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, NULL, TRUE);
}
static void ClearResults(void) {
    SendMessageW(g_results, WM_SETREDRAW, FALSE, 0);
    int n = (int)SendMessageW(g_results, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        Result *r = (Result *)SendMessageW(g_results, LB_GETITEMDATA, i, 0);
        if (r && r != (Result *)LB_ERR) { xfree(r->path); xfree(r); }
    }
    SendMessageW(g_results, LB_RESETCONTENT, 0, 0);
    SendMessageW(g_results, WM_SETREDRAW, TRUE, 0); InvalidateRect(g_results, NULL, TRUE);
    g_resultCount = 0; g_resultFiles = 0;
    g_resultsCached = FALSE;
}

static wchar_t g_lastFilt[400];          // type filter the shown results used ("" = none)
static void UpdateResultsLabel(BOOL done) {
    wchar_t s[PATHBUF + 200], where[PATHBUF + 16] = L"", tail[96] = L"";
    if (g_lastScope[0]) swprintf(where, PATHBUF + 16, L" in %ls/", g_lastScope);
    if (g_lastFilt[0]) { wcsncat(where, L" (types: ", PATHBUF + 15 - wcslen(where)); wcsncat(where, g_lastFilt, PATHBUF + 15 - wcslen(where)); wcsncat(where, L")", PATHBUF + 15 - wcslen(where)); }
    if (!done) swprintf(tail, 96, L" (searching...)");
    else if (g_searchLowMem) swprintf(tail, 96, L"  (stopped early: the computer is low on memory)");
    else if (g_resultsCached) swprintf(tail, 96, L"  (saved search from %ls)", g_cachedWhen);
    if (g_searchLowMem)
        swprintf(s, PATHBUF + 200, L"Search results: first %d matches in %d files%ls%ls", g_resultCount, g_resultFiles, where, tail);
    else
        swprintf(s, PATHBUF + 200, L"Search results: %d matches in %d files%ls%ls", g_resultCount, g_resultFiles, where, tail);
    SetWindowTextW(g_reslbl, s);
}

static void ShowSearchProgress(BOOL on) {
    if (!on && !g_running) SendMessageW(g_sprog, PBM_SETPOS, 0, 0);
    RECT rc; GetClientRect(g_wnd, &rc); Layout(rc.right, rc.bottom);
}

/* ---- search cache: <exe dir>\search_cache\<scan name>\<hash>.txt, cleared when the scan is extracted again */
static void ScanNameOf(const wchar_t *scanRoot, wchar_t *out, size_t n) {
    const wchar_t *bs = wcsrchr(scanRoot, L'\\'); swprintf(out, n, L"%ls", bs ? bs + 1 : scanRoot);
}
static void CacheDirFor(const wchar_t *scanName, wchar_t *out, size_t n) { swprintf(out, n, L"%ls\\search_cache\\%ls", g_exeDir, scanName); }

static void CacheKey(const wchar_t *term, BOOL mc, const wchar_t *scope, wchar_t *key, size_t n) {
    swprintf(key, n, L"%d\t%ls\t%ls", mc ? 1 : 0, scope, term);
    if (g_filterOn) {                      // results depend on the type filter too
        wcsncat(key, L"\ttypes:", n - wcslen(key) - 1);
        for (int i = 0; i < g_nsel; i++) { wcsncat(key, i ? L"|" : L"", n - wcslen(key) - 1); wcsncat(key, g_sel[i], n - wcslen(key) - 1); }
    }
}
static void CachePath(const wchar_t *key, wchar_t *out, size_t n) {
    unsigned long long h = 1469598103934665603ULL;            // FNV-1a over the key
    for (const wchar_t *q = key; *q; q++) { h ^= (unsigned)*q; h *= 1099511628211ULL; }
    wchar_t scan[PATHBUF], dir[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF); CacheDirFor(scan, dir, PATHBUF);
    swprintf(out, n, L"%ls\\%016llx.txt", dir, h);
}

static void ClearSearchCache(const wchar_t *scanName) {
    wchar_t dir[PATHBUF], pat[PATHBUF + 8]; CacheDirFor(scanName, dir, PATHBUF);
    swprintf(pat, PATHBUF + 8, L"%ls\\*.txt", dir);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { wchar_t f[PATHBUF + MAX_PATH]; swprintf(f, PATHBUF + MAX_PATH, L"%ls\\%ls", dir, fd.cFileName); DeleteFileW(f); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    { wchar_t f[PATHBUF + 32]; swprintf(f, PATHBUF + 32, L"%ls\\types.idx", dir); DeleteFileW(f); }
    RemoveDirectoryW(dir);
}

static void WriteUtf8(HANDLE h, const wchar_t *s) {
    int len = (int)wcslen(s); if (!len) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, s, len, NULL, 0, NULL, NULL);
    char stackbuf[4096]; char *u = bytes <= (int)sizeof(stackbuf) ? stackbuf : (char *)xalloc(bytes);
    if (!u) return;
    WideCharToMultiByte(CP_UTF8, 0, s, len, u, bytes, NULL, NULL);
    DWORD wr; WriteFile(h, u, (DWORD)bytes, &wr, NULL);
    if (u != stackbuf) xfree(u);
}

// file format (UTF-8): header lines, then one line per result:  <line>\t<path within scan>\t<label>
static void SaveSearchCache(void) {
    if (!g_root[0]) return;
    wchar_t key[PATHBUF + 600 + MAXTYPES * TYPELEN], path[PATHBUF + 64], scan[PATHBUF], dir[PATHBUF];
    CacheKey(g_lastTerm, g_lastMatchCase, g_lastScope, key, PATHBUF + 600 + MAXTYPES * TYPELEN);
    CachePath(key, path, PATHBUF + 64);
    ScanNameOf(g_root, scan, PATHBUF); CacheDirFor(scan, dir, PATHBUF);
    wchar_t top[PATHBUF]; swprintf(top, PATHBUF, L"%ls\\search_cache", g_exeDir);
    CreateDirectoryW(top, NULL); CreateDirectoryW(dir, NULL);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t head[PATHBUF + 800 + MAXTYPES * TYPELEN];
    swprintf(head, PATHBUF + 800 + MAXTYPES * TYPELEN, L"ALSARC-SEARCH-CACHE 1\nK\t%ls\nN\t%d\t%d\t%04d-%02d-%02d %02d:%02d\n",
             key, g_resultCount, g_resultFiles, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    WriteUtf8(h, head);
    size_t rl = wcslen(g_root);
    int n = (int)SendMessageW(g_results, LB_GETCOUNT, 0, 0);
    WBuf b = { 0 };
    for (int i = 0; i < n; i++) {
        Result *r = (Result *)SendMessageW(g_results, LB_GETITEMDATA, i, 0);
        int len = (int)SendMessageW(g_results, LB_GETTEXTLEN, i, 0);
        if (!r || r == (Result *)LB_ERR || len < 0) continue;
        wchar_t *lbl = (wchar_t *)xalloc(((size_t)len + 1) * sizeof(wchar_t));
        if (!lbl) continue;
        SendMessageW(g_results, LB_GETTEXT, i, (LPARAM)lbl);
        for (wchar_t *q = lbl; *q; q++) if (*q == L'\n' || *q == L'\r') *q = L' ';
        const wchar_t *rel = (!_wcsnicmp(r->path, g_root, rl) && r->path[rl] == L'\\') ? r->path + rl + 1 : r->path;
        wchar_t num[24]; swprintf(num, 24, L"%d\t", r->line);
        WAdd(&b, num); WAdd(&b, rel); WAdd(&b, L"\t"); WAdd(&b, lbl); WAdd(&b, L"\n");
        xfree(lbl);
        if (b.n > (1 << 20)) { WriteUtf8(h, b.p); b.n = 0; b.p[0] = 0; }
    }
    if (b.p) WriteUtf8(h, b.p);
    xfree(b.p);
    CloseHandle(h);
}

// returns TRUE if saved results were found and shown
static BOOL LoadSearchCache(const wchar_t *key) {
    wchar_t path[PATHBUF + 64]; CachePath(key, path, PATHBUF + 64);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 512LL * 1024 * 1024) { CloseHandle(h); return FALSE; }
    char *buf = (char *)xalloc((size_t)sz.QuadPart + 1); DWORD got = 0;
    BOOL ok = buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL);
    CloseHandle(h);
    if (!ok) { xfree(buf); return FALSE; }
    int wn = MultiByteToWideChar(CP_UTF8, 0, buf, (int)got, NULL, 0);
    wchar_t *w = (wchar_t *)xalloc(((size_t)wn + 1) * sizeof(wchar_t));
    if (!w) { xfree(buf); return FALSE; }
    MultiByteToWideChar(CP_UTF8, 0, buf, (int)got, w, wn); w[wn] = 0;
    xfree(buf);
    // header
    wchar_t *ctx = w, *line;
    #define NEXTLINE() (line = ctx, ctx = wcschr(ctx, L'\n'), ctx ? (*ctx++ = 0, line) : (ctx = line + wcslen(line), (*line ? line : NULL)))
    BOOL good = FALSE; int count = 0, files = 0; wchar_t when[64] = L"";
    if (NEXTLINE() && !wcscmp(line, L"ALSARC-SEARCH-CACHE 1") && NEXTLINE() && line[0] == L'K' && line[1] == L'\t' &&
        !wcscmp(line + 2, key) && NEXTLINE() && line[0] == L'N') {
        wchar_t *f1 = wcschr(line, L'\t'); wchar_t *f2 = f1 ? wcschr(f1 + 1, L'\t') : NULL; wchar_t *f3 = f2 ? wcschr(f2 + 1, L'\t') : NULL;
        if (f1 && f2 && f3) { count = _wtoi(f1 + 1); files = _wtoi(f2 + 1); swprintf(when, 64, L"%ls", f3 + 1); good = TRUE; }
    }
    if (!good) { xfree(w); return FALSE; }
    ClearResults();
    SendMessageW(g_results, WM_SETREDRAW, FALSE, 0);
    int added = 0;
    while (NEXTLINE()) {
        wchar_t *t1 = wcschr(line, L'\t'); if (!t1) continue;
        wchar_t *t2 = wcschr(t1 + 1, L'\t'); if (!t2) continue;
        *t1 = 0; *t2 = 0;
        Result *r = (Result *)xalloc(sizeof(Result));
        if (!r) break;
        wchar_t full[PATHBUF]; swprintf(full, PATHBUF, L"%ls\\%ls", g_root, t1 + 1);
        r->path = xwcsdup(full); r->line = _wtoi(line);
        int idx = (int)SendMessageW(g_results, LB_ADDSTRING, 0, (LPARAM)(t2 + 1));
        if (idx < 0) { xfree(r->path); xfree(r); break; }
        SendMessageW(g_results, LB_SETITEMDATA, idx, (LPARAM)r);
        added++;
    }
    #undef NEXTLINE
    SendMessageW(g_results, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_results, NULL, TRUE);
    xfree(w);
    g_resultCount = added ? added : count; g_resultFiles = files;
    g_resultsCached = TRUE; swprintf(g_cachedWhen, 64, L"%ls", when);
    return TRUE;
}

static void StartSearch(BOOL force) {
    BOOL ovr = g_ovrOn; g_ovrOn = FALSE;                     // settings from a recent search, used once
    wchar_t term[512]; GetText(g_sedit, term, 512);
    if (!ovr && g_pickIdx >= 0 && g_pickIdx < g_nrecent && !wcscmp(term, g_recent[g_pickIdx])) {   // picked from the list (arrow keys + Enter)
        ovr = TRUE; g_ovrMc = g_recentMc[g_pickIdx]; swprintf(g_ovrScope, PATHBUF, L"%ls", g_recentScope[g_pickIdx]);
    }
    if (!term[0]) { SetFocus(g_sedit); return; }
    if (!g_root[0]) { MessageBoxW(g_wnd, L"Open a scan first (pick it from the Scan name list).", L"ns2dig", MB_ICONINFORMATION); return; }
    if (!force && g_searching && !wcscmp(term, g_lastTerm) &&
        (SendMessageW(g_case, BM_GETCHECK, 0, 0) == BST_CHECKED) == g_lastMatchCase) return;   // same search already running
    InterlockedIncrement(&g_searchGen);                      // cancel any search still running
    SearchArgs *a = (SearchArgs *)xalloc(sizeof(SearchArgs));
    swprintf(a->scanRoot, PATHBUF, L"%ls", g_root);
    swprintf(a->root, PATHBUF, L"%ls", g_root);
    g_lastScope[0] = 0;
    if (ovr) {                                           // a recent search / "Search again": its own folder and Match case
        SendMessageW(g_case, BM_SETCHECK, g_ovrMc ? BST_CHECKED : BST_UNCHECKED, 0);
        if (g_ovrScope[0]) {
            wchar_t dir[PATHBUF]; swprintf(dir, PATHBUF, L"%ls\\%ls", g_root, g_ovrScope);
            for (wchar_t *q = dir; *q; q++) if (*q == L'/') *q = L'\\';
            DWORD at = GetFileAttributesW(dir);
            if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) { swprintf(a->root, PATHBUF, L"%ls", dir); swprintf(g_lastScope, PATHBUF, L"%ls", g_ovrScope); }
        }
        SendMessageW(g_scope, BM_SETCHECK, g_lastScope[0] ? BST_CHECKED : BST_UNCHECKED, 0);
    } else if (SendMessageW(g_scope, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        HTREEITEM sel = TreeView_GetSelection(g_tree);
        if (sel) {
            TVITEMW ti = { 0 }; ti.mask = TVIF_PARAM; ti.hItem = sel; TreeView_GetItem(g_tree, &ti);
            if (ti.lParam == 0) sel = TreeView_GetParent(g_tree, sel);   // a file: use its folder
            if (sel && TreeView_GetParent(g_tree, sel)) {                 // not the scan root itself
                ItemPath(sel, a->root, PATHBUF);
                ScanRelPath(a->root, TRUE, g_lastScope, PATHBUF);
            }
        }
    }
    a->mc = ovr ? g_ovrMc : SendMessageW(g_case, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (g_filterOn) FilterSummary(g_lastFilt, 400); else g_lastFilt[0] = 0;
    a->filt = g_filterOn; a->nsel = g_nsel; memcpy(a->sel, g_sel, sizeof(g_sel[0]) * g_nsel);
    swprintf(g_lastTerm, 512, L"%ls", term);
    g_lastMatchCase = a->mc;
    AddRecent(term, a->mc, g_lastScope);
    SetWindowTextW(g_sedit, term);

    if (!force) {
        wchar_t key[PATHBUF + 600 + MAXTYPES * TYPELEN]; CacheKey(term, a->mc, g_lastScope, key, PATHBUF + 600 + MAXTYPES * TYPELEN);
        if (LoadSearchCache(key)) {
            xfree(a);
            InterlockedExchange(&g_searching, 0);
            StopOrNext(FALSE);
            if (!g_running) SendMessageW(g_sprog, PBM_SETPOS, 0, 0);
            UpdateResultsLabel(TRUE);
            SetStatus(L"Showing saved results. Right-click a result and choose \"Search again\" to refresh them.");
            return;
        }
    }

    int k = WideCharToMultiByte(CP_UTF8, 0, term, -1, a->term, sizeof(a->term), NULL, NULL);
    a->tlen = k > 0 ? (size_t)k - 1 : 0;
    if (!a->mc) LowerAscii(a->term, a->tlen);
    a->wild = HasWild(a->term, a->tlen);
    a->gen = InterlockedIncrement(&g_searchGen);
    InterlockedExchange(&g_searchLowMem, 0);
    ClearResults();
    InterlockedExchange(&g_searching, 1);
    StopOrNext(TRUE);
    g_searchTotal = 0;
    SendMessageW(g_sprog, PBM_SETPOS, 0, 0);
    ShowSearchProgress(TRUE);
    UpdateResultsLabel(FALSE);
    {
        wchar_t st[PATHBUF + 64];
        if (g_lastScope[0]) swprintf(st, PATHBUF + 64, L"Counting files in %ls/ ...", g_lastScope); else swprintf(st, PATHBUF + 64, L"Counting files in the whole scan...");
        SetStatus(st);
    }
    HANDLE t = CreateThread(NULL, 0, SearchWorker, a, 0, NULL);
    if (t) CloseHandle(t);
}

static void StopSearch(void);
// results menu: empty the list, take the term out of the recent searches and the search box, and drop its saved results
static void ForgetSearch(void) {
    StopSearch();
    wchar_t term[512]; swprintf(term, 512, L"%ls", g_lastTerm);
    if (term[0] && g_root[0]) {
        wchar_t key[PATHBUF + 600 + MAXTYPES * TYPELEN], path[PATHBUF + 64];
        CacheKey(term, g_lastMatchCase, g_lastScope, key, PATHBUF + 600 + MAXTYPES * TYPELEN);
        CachePath(key, path, PATHBUF + 64);
        DeleteFileW(path);
    }
    if (term[0]) { int i = FindRecent(term, g_lastMatchCase, g_lastScope); if (i >= 0) { DropRecent(i); SaveRecent(); } }
    ClearResults();
    g_lastTerm[0] = 0; g_lastScope[0] = 0;
    SetWindowTextW(g_sedit, L"");
    FillRecentList();
    SetWindowTextW(g_reslbl, L"Search results:");
    SetStatus(term[0] ? L"Search cleared and removed from the recent searches." : L"Search results cleared.");
}
static void StopSearch(void) {
    if (!g_searching) return;
    InterlockedIncrement(&g_searchGen);
    InterlockedExchange(&g_searching, 0);
    StopOrNext(FALSE);
    ShowSearchProgress(FALSE);
    UpdateResultsLabel(TRUE);
    wchar_t lbl[PATHBUF + 220]; GetWindowTextW(g_reslbl, lbl, PATHBUF + 200); wcscat(lbl, L"  (stopped)");
    SetWindowTextW(g_reslbl, lbl);
    SetStatus(L"Search stopped. Stopped searches aren't saved.");
}

static void OpenResult(void) {
    int nsel = (int)SendMessageW(g_results, LB_GETSELCOUNT, 0, 0);
    if (nsel > 1) {
        wchar_t st[96]; swprintf(st, 96, L"%d results selected - right-click to make a ZIP of their files.", nsel); SetStatus(st);
        return;
    }
    int i = (int)SendMessageW(g_results, LB_GETCARETINDEX, 0, 0);
    if (i < 0 || nsel != 1 || SendMessageW(g_results, LB_GETSEL, i, 0) <= 0) {
        int one = -1;
        if (nsel == 1) SendMessageW(g_results, LB_GETSELITEMS, 1, (LPARAM)&one);
        i = one;
    }
    if (i < 0) return;
    Result *r = (Result *)SendMessageW(g_results, LB_GETITEMDATA, i, 0);
    if (!r || r == (Result *)LB_ERR) return;
    if (_wcsicmp(g_cur, r->path) != 0) {
        if (!SelectPathInTree(r->path) || _wcsicmp(g_cur, r->path) != 0) ShowFile(r->path);
    }
    HighlightLine(r->line);
}

/* ------------------------------------------------------------------ ZIP files (own writer: deflate, UTF-8 names, ZIP64 when needed) */
static DWORD g_crcTab[256];
static void CrcInit(void) {
    for (DWORD i = 0; i < 256; i++) { DWORD c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; g_crcTab[i] = c; }
}
static DWORD Crc32(const unsigned char *p, size_t n) {
    DWORD c = 0xFFFFFFFFu; while (n--) c = g_crcTab[(c ^ *p++) & 0xFF] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}

typedef struct { unsigned char *p; size_t n, cap; unsigned long long bb; int bits; BOOL fail; } BitOut;
static void BoGrow(BitOut *o, size_t need) {
    if (o->fail || o->n + need <= o->cap) return;
    size_t nc = o->cap ? o->cap * 2 : 65536; while (nc < o->n + need) nc *= 2;
    unsigned char *np = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, nc);
    if (!np) { o->fail = TRUE; return; }
    if (o->p) { memcpy(np, o->p, o->n); xfree(o->p); }
    o->p = np; o->cap = nc;
}
static void PutBits(BitOut *o, unsigned v, int n) {
    o->bb |= (unsigned long long)v << o->bits; o->bits += n;
    while (o->bits >= 8) {
        if (o->n >= o->cap) { BoGrow(o, 1); if (o->fail) { o->bits = 0; o->bb = 0; return; } }
        o->p[o->n++] = (unsigned char)o->bb; o->bb >>= 8; o->bits -= 8;
    }
}
static unsigned RevBits(unsigned v, int n) { unsigned r = 0; while (n--) { r = (r << 1) | (v & 1); v >>= 1; } return r; }
static void PutLit(BitOut *o, int v) {            // fixed Huffman literal/length codes
    if (v < 144) PutBits(o, RevBits(0x30 + v, 8), 8);
    else if (v < 256) PutBits(o, RevBits(0x190 + v - 144, 9), 9);
    else if (v < 280) PutBits(o, RevBits(v - 256, 7), 7);
    else PutBits(o, RevBits(0xC0 + v - 280, 8), 8);
}
static const unsigned short kLBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const unsigned char  kLExt[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const unsigned short kDBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const unsigned char  kDExt[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static void PutMatch(BitOut *o, int len, int dist) {
    int i = 28; while (kLBase[i] > len) i--;
    PutLit(o, 257 + i); if (kLExt[i]) PutBits(o, (unsigned)(len - kLBase[i]), kLExt[i]);
    int j = 29; while (kDBase[j] > dist) j--;
    PutBits(o, RevBits((unsigned)j, 5), 5); if (kDExt[j]) PutBits(o, (unsigned)(dist - kDBase[j]), kDExt[j]);
}

#define ZHBITS 16
// raw deflate (one fixed-Huffman block, greedy LZ77 with hash chains); NULL if out of memory
static unsigned char *DeflateBuf(const unsigned char *in, size_t n, size_t *outn) {
    BitOut o = { 0 };
    BoGrow(&o, n / 3 + 4096);
    int *head = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int) << ZHBITS);
    int *prev = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int) * 32768);
    if (!head || !prev || o.fail) { xfree(head); xfree(prev); xfree(o.p); return NULL; }
    for (int i = 0; i < (1 << ZHBITS); i++) head[i] = -1;
    PutBits(&o, 1, 1); PutBits(&o, 1, 2);               // BFINAL = 1, BTYPE = 01 (fixed Huffman)
    size_t i = 0;
    #define ZHASH(p) ((((unsigned)in[p] << 11) ^ ((unsigned)in[(p) + 1] << 5) ^ in[(p) + 2] ^ ((unsigned)in[(p) + 1] >> 3)) & ((1u << ZHBITS) - 1))
    while (i < n && !o.fail) {
        size_t best = 0, bdist = 0;
        if (i + 2 < n) {
            unsigned h = ZHASH(i);
            int cand = head[h], chain = 64;
            size_t maxl = n - i < 258 ? n - i : 258;
            while (cand >= 0 && chain--) {
                size_t d = i - (size_t)cand;
                if (d > 32768) break;
                if (in[cand + best] == in[i + best] && in[cand] == in[i]) {
                    size_t l = 0; while (l < maxl && in[cand + l] == in[i + l]) l++;
                    if (l > best) { best = l; bdist = d; if (l == maxl) break; }
                }
                int pc = prev[cand & 32767];
                if (pc >= cand) break;                   // slot reused by a newer position
                cand = pc;
            }
            prev[i & 32767] = head[h]; head[h] = (int)i;
        }
        if (best >= 3) {
            PutMatch(&o, (int)best, (int)bdist);
            for (size_t q = 1; q < best; q++) { size_t j = i + q; if (j + 2 < n) { unsigned h = ZHASH(j); prev[j & 32767] = head[h]; head[h] = (int)j; } }
            i += best;
        } else { PutLit(&o, in[i]); i++; }
    }
    #undef ZHASH
    PutLit(&o, 256);
    if (o.bits) PutBits(&o, 0, 8 - o.bits);
    xfree(head); xfree(prev);
    if (o.fail) { xfree(o.p); return NULL; }
    *outn = o.n;
    return o.p;
}

typedef struct { wchar_t *src; char *name; } ZipItem;
typedef struct {
    wchar_t out[PATHBUF];
    ZipItem *items; int n, cap;
    // a folder to collect in the worker (so the window stays responsive), with the file-type filter
    BOOL scanDir; wchar_t srcDir[PATHBUF], leaf[MAX_PATH];
    BOOL filt; int nsel; wchar_t sel[MAXTYPES][TYPELEN];
    // results
    int written, skipped; unsigned long long bytes; BOOL ok, cancelled; wchar_t err[512];
    wchar_t *note;                   // Bulk ZIP: the pasted paths that weren't found (shown when the ZIP is done)
} ZipJob;
typedef struct { char *name; DWORD crc, csize, usize; WORD method, time, date; unsigned long long off; } ZipEnt;

static void ZipAdd(ZipJob *j, const wchar_t *src, const wchar_t *zname) {
    if (j->n == j->cap) {
        int nc = j->cap ? j->cap * 2 : 256;
        ZipItem *ni = (ZipItem *)xalloc(nc * sizeof(ZipItem)); if (!ni) return;
        if (j->n) memcpy(ni, j->items, j->n * sizeof(ZipItem));
        xfree(j->items); j->items = ni; j->cap = nc;
    }
    wchar_t nm[PATHBUF]; swprintf(nm, PATHBUF, L"%ls", zname);
    for (wchar_t *q = nm; *q; q++) if (*q == L'\\') *q = L'/';
    int k = WideCharToMultiByte(CP_UTF8, 0, nm, -1, NULL, 0, NULL, NULL);
    char *u = (char *)xalloc(k + 1); if (!u) return;
    WideCharToMultiByte(CP_UTF8, 0, nm, -1, u, k, NULL, NULL);
    j->items[j->n].src = xwcsdup(src); j->items[j->n].name = u; j->n++;
}
static void FreeZipJob(ZipJob *j) {
    for (int i = 0; i < j->n; i++) { xfree(j->items[i].src); xfree(j->items[i].name); }
    xfree(j->items); xfree(j->note); xfree(j);
}

typedef struct { HANDLE h; unsigned long long pos; BOOL fail; } ZOut;
static void ZW(ZOut *z, const void *p, size_t n) {
    while (n && !z->fail) {
        DWORD part = n > (1u << 30) ? (1u << 30) : (DWORD)n, wr = 0;
        if (!WriteFile(z->h, p, part, &wr, NULL) || wr != part) { z->fail = TRUE; return; }
        z->pos += part; p = (const char *)p + part; n -= part;
    }
}
static void Z16(unsigned char **q, unsigned v) { (*q)[0] = (unsigned char)v; (*q)[1] = (unsigned char)(v >> 8); *q += 2; }
static void Z32(unsigned char **q, DWORD v) { for (int i = 0; i < 4; i++) (*q)[i] = (unsigned char)(v >> (8 * i)); *q += 4; }
static void Z64(unsigned char **q, unsigned long long v) { for (int i = 0; i < 8; i++) (*q)[i] = (unsigned char)(v >> (8 * i)); *q += 8; }

static BOOL ZipTypeOk(const ZipJob *j, const wchar_t *name) {
    if (!j->filt) return TRUE;
    wchar_t t[TYPELEN]; TypeOfName(name, t);
    for (int i = 0; i < j->nsel; i++) if (!wcscmp(j->sel[i], t)) return TRUE;
    return FALSE;
}
static void ZipFolderRec(ZipJob *j, const wchar_t *dir, const wchar_t *zprefix, DWORD *lastPost) {
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (g_zipCancel) break;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t p[PATHBUF], zn[PATHBUF];
        swprintf(p, PATHBUF, L"%ls\\%ls", dir, fd.cFileName);
        swprintf(zn, PATHBUF, L"%ls%ls%ls", zprefix, zprefix[0] ? L"/" : L"", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ZipFolderRec(j, p, zn, lastPost);
        else if (ZipTypeOk(j, fd.cFileName)) {
            ZipAdd(j, p, zn);
            DWORD now = GetTickCount();
            if (now - *lastPost > 150) { *lastPost = now; PostMessageW(g_wnd, WM_APP_ZIPPROG, j->n, -1); }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static DWORD ZipWorkerBody(LPVOID p);
static DWORD WINAPI ZipWorker(LPVOID p) {          // counted, so closing the app can wait for it to finish
    InterlockedIncrement(&g_workers); CrashGuardThread();
    DWORD r = ZipWorkerBody(p);
    InterlockedDecrement(&g_workers);
    return r;
}
static DWORD ZipWorkerBody(LPVOID p) {
    ZipJob *j = (ZipJob *)p;
    if (j->scanDir) { DWORD lp0 = 0; ZipFolderRec(j, j->srcDir, j->leaf, &lp0); }
    for (int i = 0; i < j->n; i++) if (!_wcsicmp(j->items[i].src, j->out)) {     // never put the ZIP inside itself
        xfree(j->items[i].src); xfree(j->items[i].name); j->items[i] = j->items[--j->n]; break;
    }
    if (g_zipCancel) { j->cancelled = TRUE; PostMessageW(g_wnd, WM_APP_ZIPDONE, 0, (LPARAM)j); return 0; }
    if (!j->n) {
        swprintf(j->err, 512, j->filt ? L"There are no files of the chosen file types there." : L"There are no files to put in the ZIP.");
        PostMessageW(g_wnd, WM_APP_ZIPDONE, 0, (LPARAM)j); return 0;
    }
    ZipEnt *ents = (ZipEnt *)xalloc(sizeof(ZipEnt) * (j->n ? j->n : 1));
    wchar_t lp[PATHBUF + 16]; LongPath(j->out, lp, PATHBUF + 16);
    ZOut z = { 0 };
    z.h = CreateFileW(lp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (!ents || z.h == INVALID_HANDLE_VALUE) {
        swprintf(j->err, 512, L"Couldn't create the ZIP file (error %lu).", GetLastError());
        xfree(ents); PostMessageW(g_wnd, WM_APP_ZIPDONE, 0, (LPARAM)j); return 0;
    }
    int ne = 0; DWORD lastPost = 0;
    for (int i = 0; i < j->n && !z.fail; i++) {
        if (g_zipCancel) { j->cancelled = TRUE; break; }
        wchar_t sp[PATHBUF + 16]; LongPath(j->items[i].src, sp, PATHBUF + 16);
        HANDLE h = CreateFileW(sp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (h == INVALID_HANDLE_VALUE) { j->skipped++; continue; }
        LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
        FILETIME ft, lt; GetFileTime(h, NULL, NULL, &ft); FileTimeToLocalFileTime(&ft, &lt);
        WORD dd = 0, dt = 0; FileTimeToDosDateTime(&lt, &dd, &dt);
        if (sz.QuadPart >= 0xF0000000LL) { CloseHandle(h); j->skipped++; continue; }   // over ~4 GB: not supported
        size_t n = (size_t)sz.QuadPart;
        unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
        DWORD got = 0; BOOL rd = buf && (n == 0 || (ReadFile(h, buf, (DWORD)n, &got, NULL) && got == n));
        CloseHandle(h);
        if (!rd) { xfree(buf); j->skipped++; continue; }
        DWORD crc = Crc32(buf, n);
        size_t cn = 0; unsigned char *cbuf = n >= 64 ? DeflateBuf(buf, n, &cn) : NULL;
        WORD method = 8; const unsigned char *data = cbuf; size_t dn = cn;
        if (!cbuf || cn >= n) { method = 0; data = buf; dn = n; }
        ZipEnt *e = &ents[ne];
        e->name = j->items[i].name; e->crc = crc; e->csize = (DWORD)dn; e->usize = (DWORD)n;
        e->method = method; e->time = dt; e->date = dd; e->off = z.pos;
        size_t nl = strlen(e->name);
        unsigned char hdr[30], *q = hdr;
        Z32(&q, 0x04034b50); Z16(&q, 20); Z16(&q, 0x0800); Z16(&q, method); Z16(&q, dt); Z16(&q, dd);
        Z32(&q, crc); Z32(&q, (DWORD)dn); Z32(&q, (DWORD)n); Z16(&q, (unsigned)nl); Z16(&q, 0);
        ZW(&z, hdr, 30); ZW(&z, e->name, nl); ZW(&z, data, dn);
        j->bytes += n; ne++; j->written++;
        xfree(cbuf); xfree(buf);
        DWORD now = GetTickCount();
        if (now - lastPost > 100) { lastPost = now; PostMessageW(g_wnd, WM_APP_ZIPPROG, i + 1, j->n); }
    }
    if (j->cancelled) {                                   // stopped: remove the unfinished ZIP
        CloseHandle(z.h); DeleteFileW(lp); xfree(ents);
        PostMessageW(g_wnd, WM_APP_ZIPDONE, 0, (LPARAM)j); return 0;
    }
    // central directory
    unsigned long long cdStart = z.pos;
    for (int i = 0; i < ne && !z.fail; i++) {
        ZipEnt *e = &ents[i];
        BOOL big = e->off >= 0xFFFFFFFFULL;
        size_t nl = strlen(e->name);
        unsigned char hdr[46 + 12], *q = hdr;
        Z32(&q, 0x02014b50); Z16(&q, big ? 45 : 20); Z16(&q, big ? 45 : 20); Z16(&q, 0x0800); Z16(&q, e->method);
        Z16(&q, e->time); Z16(&q, e->date); Z32(&q, e->crc); Z32(&q, e->csize); Z32(&q, e->usize);
        Z16(&q, (unsigned)nl); Z16(&q, big ? 12 : 0); Z16(&q, 0); Z16(&q, 0); Z16(&q, 0); Z32(&q, 0);
        Z32(&q, big ? 0xFFFFFFFFu : (DWORD)e->off);
        ZW(&z, hdr, 46); ZW(&z, e->name, nl);
        if (big) { unsigned char ex[12], *x = ex; Z16(&x, 1); Z16(&x, 8); Z64(&x, e->off); ZW(&z, ex, 12); }
    }
    unsigned long long cdSize = z.pos - cdStart;
    BOOL z64 = ne >= 0xFFFF || cdStart >= 0xFFFFFFFFULL || cdSize >= 0xFFFFFFFFULL;
    if (z64 && !z.fail) {
        unsigned long long eocd64 = z.pos;
        unsigned char r[56], *q = r;
        Z32(&q, 0x06064b50); Z64(&q, 44); Z16(&q, 45); Z16(&q, 45); Z32(&q, 0); Z32(&q, 0);
        Z64(&q, ne); Z64(&q, ne); Z64(&q, cdSize); Z64(&q, cdStart);
        ZW(&z, r, 56);
        unsigned char l[20], *x = l; Z32(&x, 0x07064b50); Z32(&x, 0); Z64(&x, eocd64); Z32(&x, 1);
        ZW(&z, l, 20);
    }
    {
        unsigned char r[22], *q = r;
        unsigned cnt = ne >= 0xFFFF ? 0xFFFF : (unsigned)ne;
        Z32(&q, 0x06054b50); Z16(&q, 0); Z16(&q, 0); Z16(&q, cnt); Z16(&q, cnt);
        Z32(&q, cdSize >= 0xFFFFFFFFULL ? 0xFFFFFFFFu : (DWORD)cdSize);
        Z32(&q, cdStart >= 0xFFFFFFFFULL ? 0xFFFFFFFFu : (DWORD)cdStart); Z16(&q, 0);
        ZW(&z, r, 22);
    }
    BOOL fail = z.fail;
    CloseHandle(z.h);
    xfree(ents);
    if (fail) { DeleteFileW(lp); swprintf(j->err, 512, L"Writing the ZIP file failed (disk full?)."); }
    else j->ok = TRUE;
    PostMessageW(g_wnd, WM_APP_ZIPDONE, 0, (LPARAM)j);
    return 0;
}

// ask where to save; returns FALSE if cancelled
static BOOL AskZipPath(const wchar_t *suggest, wchar_t *out) {
    wchar_t name[MAX_PATH]; swprintf(name, MAX_PATH, L"%ls", suggest);
    for (wchar_t *q = name; *q; q++) if (wcschr(L"\\/:*?\"<>|", *q) || *q < 32) *q = L'_';
    swprintf(out, PATHBUF, L"%ls", name);
    wchar_t initDir[PATHBUF];
    GetPrivateProfileStringW(L"paths", L"zipdir", L"", initDir, PATHBUF, g_ini);
    OPENFILENAMEW of; ZeroMemory(&of, sizeof(of));
    of.lStructSize = sizeof(of); of.hwndOwner = g_wnd;
    of.lpstrFilter = L"ZIP files (*.zip)\0*.zip\0All files\0*.*\0";
    of.lpstrFile = out; of.nMaxFile = PATHBUF;
    of.lpstrInitialDir = initDir[0] ? initDir : NULL;
    of.lpstrDefExt = L"zip"; of.lpstrTitle = L"Save ZIP file as";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return FALSE;
    wchar_t d[PATHBUF]; swprintf(d, PATHBUF, L"%ls", out);
    wchar_t *bs = wcsrchr(d, L'\\'); if (bs) { *bs = 0; WritePrivateProfileStringW(L"paths", L"zipdir", d, g_ini); }
    return TRUE;
}

static void ShowZipStop(BOOL on) {
    ShowWindow(g_zipstop, on ? SW_SHOW : SW_HIDE); EnableWindow(g_zipstop, on);
    RECT rc; GetClientRect(g_wnd, &rc); Layout(rc.right, rc.bottom);
}
static void StartZip(ZipJob *j) {
    if (!j->scanDir && !j->n) { MessageBoxW(g_wnd, L"There are no files to put in the ZIP.", L"ns2dig", MB_ICONINFORMATION); FreeZipJob(j); return; }
    InterlockedExchange(&g_zipCancel, 0);
    InterlockedExchange(&g_zipping, 1);
    SetStatus(j->scanDir ? L"Making ZIP: finding the files..." : L"Making ZIP...");
    if (g_zipThread) { CloseHandle(g_zipThread); g_zipThread = NULL; }
    g_zipThread = CreateThread(NULL, 0, ZipWorker, j, 0, NULL);
    if (!g_zipThread) { InterlockedExchange(&g_zipping, 0); FreeZipJob(j); return; }
    ShowZipStop(TRUE);
}
static void StopZip(void) {
    if (!g_zipping) return;
    InterlockedExchange(&g_zipCancel, 1);
    EnableWindow(g_zipstop, FALSE);
    SetStatus(L"Stopping the ZIP...");
}

static BOOL ZipBusy(void) {
    if (!g_zipping) return FALSE;
    MessageBoxW(g_wnd, L"A ZIP file is still being made. Wait for it to finish first.", L"ns2dig", MB_ICONINFORMATION);
    return TRUE;
}

// tree: a folder (with everything under it) or one file
static void ZipTreePath(const wchar_t *path, BOOL isDir) {
    if (ZipBusy()) return;
    const wchar_t *bs = wcsrchr(path, L'\\');
    const wchar_t *leaf = bs ? bs + 1 : path;
    wchar_t sug[MAX_PATH]; swprintf(sug, MAX_PATH, L"%ls", leaf);
    if (!isDir && EndsWithI(sug, L".json")) sug[wcslen(sug) - 5] = 0;
    wcsncat(sug, L".zip", MAX_PATH - wcslen(sug) - 1);
    ZipJob *j = (ZipJob *)xalloc(sizeof(ZipJob));
    if (!j || !AskZipPath(sug, j->out)) { xfree(j); return; }
    if (isDir) {                       // collected by the worker, honouring the file-type filter
        j->scanDir = TRUE;
        swprintf(j->srcDir, PATHBUF, L"%ls", path); swprintf(j->leaf, MAX_PATH, L"%ls", leaf);
        j->filt = g_filterOn; j->nsel = g_nsel; memcpy(j->sel, g_sel, sizeof(g_sel[0]) * g_nsel);
    } else ZipAdd(j, path, leaf);
    StartZip(j);
}

static int CmpWstrPtrI(const void *a, const void *b) { return _wcsicmp(*(const wchar_t * const *)a, *(const wchar_t * const *)b); }

// results: the files of the selected results (each file once), keeping their folders within the scan
static void ZipSelectedResults(void) {
    if (ZipBusy()) return;
    int ns = (int)SendMessageW(g_results, LB_GETSELCOUNT, 0, 0);
    if (ns <= 0) return;
    int *sel = (int *)xalloc(sizeof(int) * ns);
    const wchar_t **paths = (const wchar_t **)xalloc(sizeof(wchar_t *) * ns);
    if (!sel || !paths) { xfree(sel); xfree(paths); return; }
    ns = (int)SendMessageW(g_results, LB_GETSELITEMS, ns, (LPARAM)sel);
    int np = 0;
    for (int i = 0; i < ns; i++) {
        Result *r = (Result *)SendMessageW(g_results, LB_GETITEMDATA, sel[i], 0);
        if (r && r != (Result *)LB_ERR) paths[np++] = r->path;
    }
    qsort(paths, np, sizeof(wchar_t *), CmpWstrPtrI);
    int nu = 0;
    for (int i = 0; i < np; i++) if (!nu || _wcsicmp(paths[i], paths[nu - 1])) paths[nu++] = paths[i];
    wchar_t sug[MAX_PATH]; swprintf(sug, MAX_PATH, L"search - %.80ls.zip", g_lastTerm);
    ZipJob *j = (ZipJob *)xalloc(sizeof(ZipJob));
    if (j && AskZipPath(sug, j->out)) {
        size_t rl = wcslen(g_root);
        for (int i = 0; i < nu; i++) {
            const wchar_t *rel = (!_wcsnicmp(paths[i], g_root, rl) && paths[i][rl] == L'\\') ? paths[i] + rl + 1 : paths[i];
            ZipAdd(j, paths[i], rel);
        }
        StartZip(j);
    } else xfree(j);
    xfree(sel); xfree(paths);
}

// the originals in the game dump of the selected results (an archive once, however many of its files were selected)
static void ZipSelectedDump(void) {
    if (ZipBusy()) return;
    int ns = (int)SendMessageW(g_results, LB_GETSELCOUNT, 0, 0);
    if (ns <= 0) return;
    int *sel = (int *)xalloc(sizeof(int) * ns);
    wchar_t **paths = (wchar_t **)xalloc(sizeof(wchar_t *) * ns);
    if (!sel || !paths) { xfree(sel); xfree(paths); return; }
    ns = (int)SendMessageW(g_results, LB_GETSELITEMS, ns, (LPARAM)sel);
    int np = 0, missing = 0;
    wchar_t src[PATHBUF] = L"", scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF); ScanSource(scan, src);
    size_t sl = wcslen(src); while (sl > 3 && src[sl - 1] == L'\\') src[--sl] = 0;
    for (int i = 0; i < ns; i++) {
        Result *r = (Result *)SendMessageW(g_results, LB_GETITEMDATA, sel[i], 0);
        if (!r || r == (Result *)LB_ERR) continue;
        wchar_t d[PATHBUF]; BOOL known;
        int k = DumpOrigNoAsk(r->path, FALSE, d, &known);
        if (k == 1 || k == 3) paths[np++] = xwcsdup(d); else missing++;
    }
    qsort(paths, np, sizeof(wchar_t *), CmpWstrPtrI);
    int nu = 0;
    for (int i = 0; i < np; i++) { if (!nu || _wcsicmp(paths[i], paths[nu - 1])) paths[nu++] = paths[i]; else xfree(paths[i]); }
    if (!nu) {
        MessageBoxW(g_wnd, L"None of the selected results' original files were found in the game dump.", L"ns2dig", MB_ICONINFORMATION);
    } else {
        wchar_t sug[MAX_PATH]; swprintf(sug, MAX_PATH, L"search - %.80ls (game dump).zip", g_lastTerm);
        ZipJob *j = (ZipJob *)xalloc(sizeof(ZipJob));
        if (j && AskZipPath(sug, j->out)) {
            for (int i = 0; i < nu; i++) {
                const wchar_t *rel = (sl && !_wcsnicmp(paths[i], src, sl) && paths[i][sl] == L'\\') ? paths[i] + sl + 1 : paths[i];
                ZipAdd(j, paths[i], rel);
            }
            if (missing) { wchar_t st[160]; swprintf(st, 160, L"%d of the selected results' originals weren't found in the game dump and are left out.", missing); SetStatus(st); }
            StartZip(j);
        } else xfree(j);
    }
    for (int i = 0; i < nu; i++) xfree(paths[i]);
    xfree(sel); xfree(paths);
}

/* ------------------------------------------------------------------ Bulk ZIP
   A window to paste many paths (one per line: full paths, paths within the scan such as base\romfs\X, with or without
   .json) and ZIP their JSON files (from the scan's output folder), their originals from the game dump, or both
   (folders JSON\ and ROM\). A file that was inside an archive gives the archive itself. Lines that aren't found are
   listed when the ZIP is done; the rest are still zipped. */
static HWND g_bulkDlg;
enum { BZ_HEAD = 6101, BZ_EDIT, BZ_JSON, BZ_ROM, BZ_BOTH, BZ_CLEAR };
// one pasted line -> its file or folder in the scan's output (FALSE: not found, or not a path at all: *isPath)
static BOOL BulkResolve(const wchar_t *line, wchar_t *found, BOOL *isPath) {
    wchar_t in[PATHBUF]; swprintf(in, PATHBUF, L"%ls", line); *isPath = FALSE; found[0] = 0;
    wchar_t *s = in;
    while (*s == L' ' || *s == L'\t' || *s == L'"' || *s == L'\'' || *s == L'`' || *s == L'-' || *s == L'*' || *s == 0x2022 || (*s >= 0x2500 && *s <= 0x257F)) s++;
    for (wchar_t *q = s; *q; q++) if ((q[0] == L' ' && q[1] == L' ') || *q == L'\t' || *q == L'\r' || *q == L'\n') { *q = 0; break; }
    size_t l = wcslen(s);
    while (l && (s[l - 1] == L' ' || s[l - 1] == L'"' || s[l - 1] == L'\'' || s[l - 1] == L'`' || s[l - 1] == L'/' || s[l - 1] == L'\\' || s[l - 1] == L',')) s[--l] = 0;
    if (!l) return FALSE;
    for (wchar_t *q = s; *q; q++) if (*q == L'/') *q = L'\\';
    if (!wcschr(s, L'\\') && !wcschr(s, L'.')) return FALSE;          // a heading such as "Send JSON"
    *isPath = TRUE;
    if (!wcsncmp(s, L"\\\\?\\", 4)) s += 4;
    wchar_t rel[PATHBUF], scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF);
    size_t rl = wcslen(g_root), sl = wcslen(scan);
    wchar_t src[PATHBUF] = L""; ScanSource(scan, src);
    size_t dl = wcslen(src); while (dl > 3 && src[dl - 1] == L'\\') src[--dl] = 0;
    if (!_wcsnicmp(s, g_root, rl) && (s[rl] == 0 || s[rl] == L'\\')) swprintf(rel, PATHBUF, L"%ls", s + rl + (s[rl] == L'\\'));
    else if (dl && !_wcsnicmp(s, src, dl) && (s[dl] == 0 || s[dl] == L'\\')) swprintf(rel, PATHBUF, L"%ls", s + dl + (s[dl] == L'\\'));   // a path in the game dump
    else if (!_wcsnicmp(s, scan, sl) && (s[sl] == 0 || s[sl] == L'\\')) swprintf(rel, PATHBUF, L"%ls", s + sl + (s[sl] == L'\\'));
    else if (s[1] == L':' || (s[0] == L'\\' && s[1] == L'\\')) {
        const wchar_t *r = NULL;
        for (const wchar_t *q = s; (q = wcsstr(q, L"\\")) != NULL; q++) if (!_wcsnicmp(q, L"\\romfs\\", 7)) r = q + 7;
        if (!r) return FALSE;
        swprintf(rel, PATHBUF, L"%ls", r);
    } else swprintf(rel, PATHBUF, L"%ls", s);
    if (!rel[0]) return FALSE;
    int depth; wchar_t deepest[PATHBUF];
    if (ResolveUnder(g_root, rel, found, PATHBUF, &depth, deepest)) return TRUE;
    static wchar_t roots[32][PATHBUF]; int nr = 0;
    FindRomfsRoots(g_root, 0, roots, &nr, 32);
    for (int i = 0; i < nr; i++) if (ResolveUnder(roots[i], rel, found, PATHBUF, &depth, deepest)) return TRUE;
    found[0] = 0; return FALSE;
}
// ZipAdd, unless that name is already in the ZIP
static void ZipAddOnce(ZipJob *j, const wchar_t *src, const wchar_t *zname) {
    wchar_t nm[PATHBUF]; swprintf(nm, PATHBUF, L"%ls", zname);
    for (wchar_t *q = nm; *q; q++) if (*q == L'\\') *q = L'/';
    char u[PATHBUF * 3]; WideCharToMultiByte(CP_UTF8, 0, nm, -1, u, sizeof u, NULL, NULL);
    for (int i = 0; i < j->n; i++) if (!_stricmp(j->items[i].name, u)) return;
    ZipAdd(j, src, nm);
}
static void BulkNote(wchar_t **note, size_t *cap, const wchar_t *line, const wchar_t *why) {
    size_t have = *note ? wcslen(*note) : 0, need = have + wcslen(line) + wcslen(why) + 16;
    if (need > *cap) { size_t nc = need * 2; wchar_t *n2 = (wchar_t *)xalloc(nc * sizeof(wchar_t)); if (!n2) return; if (*note) { wcscpy(n2, *note); xfree(*note); } else n2[0] = 0; *note = n2; *cap = nc; }
    swprintf(*note + have, *cap - have, L"%ls  (%ls)\n", line, why);
}
// mode 1 = JSON, 2 = ROM, 3 = both
static void BulkZip(HWND dlg, int mode) {
    if (!g_root[0]) { MessageBoxW(dlg, L"Open a scan first (pick it from the Scan name list).", L"Bulk ZIP", MB_ICONINFORMATION); return; }
    if (g_zipping) { MessageBoxW(dlg, L"A ZIP file is still being made. Wait for it to finish first.", L"Bulk ZIP", MB_ICONINFORMATION); return; }
    HWND ed = GetDlgItem(dlg, BZ_EDIT);
    int tl = GetWindowTextLengthW(ed);
    if (tl <= 0) { MessageBoxW(dlg, L"Paste some file paths into the box first.", L"Bulk ZIP", MB_ICONINFORMATION); return; }
    wchar_t *txt = (wchar_t *)xalloc((tl + 2) * sizeof(wchar_t)); if (!txt) return;
    GetWindowTextW(ed, txt, tl + 1);
    ZipJob *j = (ZipJob *)xalloc(sizeof(ZipJob)); if (!j) { xfree(txt); return; }
    wchar_t scan[PATHBUF], src[PATHBUF] = L""; ScanNameOf(g_root, scan, PATHBUF); ScanSource(scan, src);
    size_t dl = wcslen(src); while (dl > 3 && src[dl - 1] == L'\\') src[--dl] = 0;
    size_t rl = wcslen(g_root);
    wchar_t *note = NULL; size_t ncap = 0; int nPaths = 0, nMiss = 0, nArc = 0;
    HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
    const wchar_t *pj = mode == 3 ? L"JSON\\" : L"", *pr = mode == 3 ? L"ROM\\" : L"";
    for (wchar_t *line = txt; line && *line; ) {
        wchar_t *e = line; while (*e && *e != L'\r' && *e != L'\n') e++;
        wchar_t keep = *e; *e = 0;
        wchar_t found[PATHBUF]; BOOL isPath;
        if (BulkResolve(line, found, &isPath)) {
            nPaths++;
            BOOL isDir = FALSE; PathExists(found, &isDir);
            const wchar_t *relOut = found + rl + (found[rl] == L'\\');
            if (mode & 1) {
                wchar_t zn[PATHBUF]; swprintf(zn, PATHBUF, L"%ls%ls", pj, relOut);
                if (isDir) { DWORD lp0 = 0; wchar_t zp[PATHBUF]; swprintf(zp, PATHBUF, L"%ls", zn); for (wchar_t *q = zp; *q; q++) if (*q == L'\\') *q = L'/'; ZipFolderRec(j, found, zp, &lp0); }
                else ZipAddOnce(j, found, zn);
            }
            if (mode & 2) {
                wchar_t d[PATHBUF]; BOOL known; int k = DumpOrigNoAsk(found, isDir, d, &known);
                if (k == 0) { BulkNote(&note, &ncap, line, known ? L"not in the game dump" : L"the game dump folder for this scan isn't known"); nMiss++; }
                else {
                    const wchar_t *relD = (dl && !_wcsnicmp(d, src, dl) && d[dl] == L'\\') ? d + dl + 1 : wcsrchr(d, L'\\') ? wcsrchr(d, L'\\') + 1 : d;
                    wchar_t zn[PATHBUF]; swprintf(zn, PATHBUF, L"%ls%ls", pr, relD);
                    if (k == 2) { DWORD lp0 = 0; wchar_t zp[PATHBUF]; swprintf(zp, PATHBUF, L"%ls", zn); for (wchar_t *q = zp; *q; q++) if (*q == L'\\') *q = L'/'; ZipFolderRec(j, d, zp, &lp0); }
                    else { if (k == 3) nArc++; ZipAddOnce(j, d, zn); }
                }
            }
        } else if (isPath) { nPaths++; nMiss++; BulkNote(&note, &ncap, line, L"not found in this scan"); }
        *e = keep;
        line = e; while (*line == L'\r' || *line == L'\n') line++;
    }
    SetCursor(oc);
    xfree(txt);
    if (!nPaths) { MessageBoxW(dlg, L"No file paths were found in the box.", L"Bulk ZIP", MB_ICONINFORMATION); xfree(note); FreeZipJob(j); return; }
    if (!j->n) {
        size_t nl = (note ? wcslen(note) : 0) + 200; wchar_t *m = (wchar_t *)xalloc(nl * sizeof(wchar_t));
        if (m) { swprintf(m, nl, L"None of the pasted paths could be found, so no ZIP was made:\n\n%ls", note ? note : L""); MessageBoxW(dlg, m, L"Bulk ZIP", MB_ICONWARNING); xfree(m); }
        xfree(note); FreeZipJob(j); return;
    }
    {                                                    // the message shown when the ZIP is done
        size_t nl = (note ? wcslen(note) : 0) + 400; wchar_t *m = (wchar_t *)xalloc(nl * sizeof(wchar_t));
        if (m) {
            int o = swprintf(m, nl, L"Pasted paths: %d - found %d.\n", nPaths, nPaths - nMiss);
            if (nMiss) o += swprintf(m + o, nl - o, L"%d of the pasted paths couldn't be found (everything else is in the ZIP):\n%ls", nMiss, note);
            if (nArc) o += swprintf(m + o, nl - o, L"%ls%d of the files are inside archives in the game dump: the archive itself is in the ZIP.", nMiss ? L"\n" : L"", nArc);
        }
        j->note = m;
    }
    xfree(note);
    wchar_t sug[MAX_PATH]; swprintf(sug, MAX_PATH, L"%.80ls - %ls.zip", scan, mode == 1 ? L"JSON" : mode == 2 ? L"ROM" : L"JSON and ROM");
    if (!AskZipPath(sug, j->out)) { FreeZipJob(j); return; }
    StartZip(j);
}
// pasting: text copied from a web page often has bare line feeds, which an edit box shows as one long line -
// paste it with Windows line ends instead
static LRESULT CALLBACK BulkEditSub(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_PASTE && IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(h)) {
        HANDLE g = GetClipboardData(CF_UNICODETEXT);
        const wchar_t *t = g ? (const wchar_t *)GlobalLock(g) : NULL;
        if (t) {
            size_t n = wcslen(t); wchar_t *o = (wchar_t *)xalloc((2 * n + 1) * sizeof(wchar_t));
            if (o) {
                size_t k = 0;
                for (size_t i = 0; i < n; i++) {
                    if (t[i] == L'\r') { o[k++] = L'\r'; o[k++] = L'\n'; if (t[i + 1] == L'\n') i++; }
                    else if (t[i] == L'\n') { o[k++] = L'\r'; o[k++] = L'\n'; }
                    else if (t[i] == 0x2028 || t[i] == 0x2029 || t[i] == 0x85) { o[k++] = L'\r'; o[k++] = L'\n'; }   // Unicode line / paragraph separators
                    else o[k++] = t[i];
                }
                o[k] = 0;
                GlobalUnlock(g); CloseClipboard();
                SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)o);
                xfree(o);
                return 0;
            }
            GlobalUnlock(g);
        }
        CloseClipboard();
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, BulkEditSub, 41);
    return DefSubclassProc(h, msg, wp, lp);
}
static LRESULT CALLBACK BulkDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = GetModuleHandleW(NULL);
        struct { const wchar_t *cls, *txt; DWORD st, ex; int id; } c[] = {
            { L"STATIC", L"Paste full file paths here to generate a ZIP file of all those files", 0, 0, BZ_HEAD },
            { L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, BZ_EDIT },
            { L"BUTTON", L"ZIP JSON", BS_PUSHBUTTON | WS_TABSTOP, 0, BZ_JSON },
            { L"BUTTON", L"ZIP ROM", BS_PUSHBUTTON | WS_TABSTOP, 0, BZ_ROM },
            { L"BUTTON", L"ZIP both", BS_PUSHBUTTON | WS_TABSTOP, 0, BZ_BOTH },
            { L"BUTTON", L"Clear", BS_PUSHBUTTON | WS_TABSTOP, 0, BZ_CLEAR },
        };
        for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
            HWND h = CreateWindowExW(c[i].ex, c[i].cls, c[i].txt, WS_CHILD | WS_VISIBLE | c[i].st, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)c[i].id, inst, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
        }
        SendMessageW(GetDlgItem(hwnd, BZ_EDIT), EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);
        SetWindowSubclass(GetDlgItem(hwnd, BZ_EDIT), BulkEditSub, 41, 0);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp), m = S(10), g = S(8), bh = S(28), hh = S(22);
        MoveWindow(GetDlgItem(hwnd, BZ_HEAD), m, m, w - 2 * m, hh, TRUE);
        int ey = m + hh + S(4), eh = h - ey - bh - g - m;
        MoveWindow(GetDlgItem(hwnd, BZ_EDIT), m, ey, w - 2 * m, eh, TRUE);
        int y = ey + eh + g, bw = S(110), x = m;
        int ids[3] = { BZ_JSON, BZ_ROM, BZ_BOTH };
        for (int i = 0; i < 3; i++) { MoveWindow(GetDlgItem(hwnd, ids[i]), x, y, bw, bh, TRUE); x += bw + g; }
        MoveWindow(GetDlgItem(hwnd, BZ_CLEAR), w - m - S(80), y, S(80), bh, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = S(520); mm->ptMinTrackSize.y = S(300); return 0; }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case BZ_JSON: BulkZip(hwnd, 1); return 0;
        case BZ_ROM:  BulkZip(hwnd, 2); return 0;
        case BZ_BOTH: BulkZip(hwnd, 3); return 0;
        case BZ_CLEAR: SetWindowTextW(GetDlgItem(hwnd, BZ_EDIT), L""); SetFocus(GetDlgItem(hwnd, BZ_EDIT)); return 0;
        case IDCANCEL: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_bulkDlg = NULL; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void ShowBulkZip(void) {
    if (g_bulkDlg) { ShowWindow(g_bulkDlg, SW_RESTORE); SetForegroundWindow(g_bulkDlg); return; }
    static int reg = 0;
    HINSTANCE inst = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = BulkDlgProc; wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"Ns2digBulkZip"; RegisterClassExW(&wc); reg = 1;
    }
    RECT r; GetWindowRect(g_wnd, &r);
    int w = S(760), h = S(520);
    g_bulkDlg = CreateWindowExW(0, L"Ns2digBulkZip", L"Bulk ZIP", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                r.left + (r.right - r.left - w) / 2, r.top + (r.bottom - r.top - h) / 2, w, h, g_wnd, NULL, inst, NULL);
    if (!g_bulkDlg) return;
    UiSkinWindow(g_bulkDlg);
    ShowWindow(g_bulkDlg, SW_SHOW);
    SetFocus(GetDlgItem(g_bulkDlg, BZ_EDIT));
}

/* ------------------------------------------------------------------ extraction */
typedef struct { wchar_t script[PATHBUF], input[PATHBUF], output[PATHBUF]; } RunArgs;

static void PostLog(const wchar_t *s) { PostMessageW(g_wnd, WM_APP_LOG, 0, (LPARAM)xwcsdup(s)); }

// Append one argument quoted by the Windows command-line rules: backslashes before a quote
// (including the closing one) must be doubled, so "E:\" doesn't swallow the closing quote.
static void AppendQuotedArg(wchar_t *cmd, size_t cap, const wchar_t *arg) {
    size_t n = wcslen(cmd);
#define PUT(c) do { if (n + 2 < cap) cmd[n++] = (c); } while (0)
    PUT(L' '); PUT(L'"');
    const wchar_t *p = arg;
    for (;;) {
        size_t bs = 0;
        while (*p == L'\\') { bs++; p++; }
        if (!*p) { for (size_t i = 0; i < bs * 2; i++) PUT(L'\\'); break; }       // before closing quote: double
        if (*p == L'"') { for (size_t i = 0; i < bs * 2 + 1; i++) PUT(L'\\'); PUT(L'"'); p++; continue; }
        for (size_t i = 0; i < bs; i++) PUT(L'\\');                              // ordinary backslashes: as-is
        PUT(*p); p++;
    }
    PUT(L'"');
#undef PUT
    cmd[n] = 0;
}

static BOOL Launch(const wchar_t *interp, RunArgs *a, HANDLE wr, PROCESS_INFORMATION *pi) {
    static wchar_t cmd[3 * PATHBUF * 2 + 64];
    size_t cap = sizeof(cmd) / sizeof(cmd[0]);
    swprintf(cmd, cap, L"%ls -u", interp);
    AppendQuotedArg(cmd, cap, a->script);
    AppendQuotedArg(cmd, cap, a->input);
    AppendQuotedArg(cmd, cap, a->output);
    STARTUPINFOEXW si; ZeroMemory(&si, sizeof(si)); si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES; si.StartupInfo.hStdOutput = wr; si.StartupInfo.hStdError = wr;
    si.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    ZeroMemory(pi, sizeof(*pi));

    // Put Python straight into the job object as it is created (Windows 10+), so everything it
    // starts - the py launcher runs python.exe as a child - can be ended by Stop or by closing the app.
    BOOL inJob = FALSE, ok = FALSE;
    SIZE_T attrSize = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    if (g_job) {
        InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
        attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)xalloc(attrSize);
        if (attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) {
            if (UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &g_job, sizeof(HANDLE), NULL, NULL)) {
                si.lpAttributeList = attrs;
                ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                    NULL, NULL, &si.StartupInfo, pi);
                inJob = ok;
            }
        }
    }
    if (!ok) {
        // older Windows: start normally, then add it to the job straight away
        si.lpAttributeList = NULL;
        si.StartupInfo.cb = sizeof(STARTUPINFOW);
        ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si.StartupInfo, pi);
        if (ok && g_job && !inJob) AssignProcessToJobObject(g_job, pi->hProcess);
    }
    if (attrs) { DeleteProcThreadAttributeList(attrs); xfree(attrs); }
    if (ok) { PostLog(L"> "); PostLog(cmd); PostLog(L"\r\n\r\n"); }
    return ok;
}

static DWORD RunWorkerBody(LPVOID p);
static DWORD WINAPI RunWorker(LPVOID p) {          // counted, so closing the app can wait for it to finish
    InterlockedIncrement(&g_workers); CrashGuardThread();
    DWORD r = RunWorkerBody(p);
    InterlockedDecrement(&g_workers);
    return r;
}
static DWORD RunWorkerBody(LPVOID p) {
    RunArgs *a = (RunArgs *)p;
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    SetEnvironmentVariableW(L"ALSARC_STOP_FILE", g_stopFile);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd, wr; DWORD code = (DWORD)-1;
    PROCESS_INFORMATION pi;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { PostLog(L"Could not create pipe.\r\n"); goto done; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    if (!Launch(L"py -3", a, wr, &pi) && !Launch(L"python", a, wr, &pi)) {
        PostLog(L"Could not start Python. Install Python 3 from python.org (tick \"Add python.exe to PATH\"), "
                L"then (only for Python 3.13 or older) run:  pip install zstandard\r\n");
        CloseHandle(wr); CloseHandle(rd); goto done;
    }
    CloseHandle(wr);
    // Read output line by line: "@@PROGRESS done total" lines drive the progress bar,
    // everything else goes to the log. Whole lines also keep UTF-8 characters intact.
    char buf[4096]; DWORD got;
    size_t cap = 8192, used = 0;
    char *line = (char *)xalloc(cap);
    for (;;) {
        BOOL ok = ReadFile(rd, buf, sizeof(buf), &got, NULL);
        BOOL eof = !ok || got == 0;
        if (!eof) {
            if (used + got + 1 > cap) {
                while (used + got + 1 > cap) cap *= 2;
                char *nl = (char *)xalloc(cap); memcpy(nl, line, used); xfree(line); line = nl;
            }
            memcpy(line + used, buf, got); used += got;
        }
        // split off complete lines (or everything at end of output)
        size_t start = 0, logStart = 0, logLen = 0;
        char *logbuf = (char *)xalloc(used + 1);
        for (size_t i = 0; i < used; i++) {
            if (line[i] != '\n' && !(eof && i == used - 1)) continue;
            size_t len = i - start + 1;
            if (len >= 11 && !memcmp(line + start, "@@PROGRESS ", 11)) {
                long d = 0, t = 0;
                char tmp[64]; size_t k = len < 63 ? len : 63; memcpy(tmp, line + start, k); tmp[k] = 0;
                if (sscanf(tmp + 11, "%ld %ld", &d, &t) == 2) PostMessageW(g_wnd, WM_APP_PROGRESS, (WPARAM)d, (LPARAM)t);
            } else {
                memcpy(logbuf + logLen, line + start, len); logLen += len;
            }
            start = i + 1;
        }
        (void)logStart;
        for (size_t q = 0; q < logLen; q++) if (!logbuf[q]) logbuf[q] = ' ';    // a 0 byte would cut the rest of the log off
        if (logLen) { wchar_t *w = Utf8ToEditText(logbuf, (int)logLen); if (w) PostMessageW(g_wnd, WM_APP_LOG, 0, (LPARAM)w); }
        xfree(logbuf);
        memmove(line, line + start, used - start); used -= start;
        if (eof) break;
    }
    xfree(line);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
done:
    xfree(a);
    PostMessageW(g_wnd, WM_APP_DONE, (WPARAM)code, 0);
    return 0;
}

static BOOL DirHasFiles(const wchar_t *dir) {
    wchar_t pat[PATHBUF + 8]; swprintf(pat, PATHBUF + 8, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    BOOL any = FALSE;
    do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) { any = TRUE; break; } } while (FindNextFileW(h, &fd));
    FindClose(h); return any;
}

/* ------------------------------------------------------------------ saved extraction log */
#define SCAN_LOG_NAME L"_extraction_log.txt"     // not .json, so it stays out of the tree and searches

static void ScanLogPath(const wchar_t *scanDir, wchar_t *out, size_t n) { swprintf(out, n, L"%ls\\%ls", scanDir, SCAN_LOG_NAME); }

static void UpdateScanLogButton(void) {
    wchar_t p[PATHBUF];
    BOOL ok = FALSE;
    if (g_root[0]) { ScanLogPath(g_root, p, PATHBUF); ok = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
    EnableWindow(g_scanlog, ok);
}

// save the log pane (UTF-8 with BOM) into the scan folder
static void SaveScanLog(const wchar_t *scanDir) {
    int n = GetWindowTextLengthW(g_view);
    wchar_t *w = (wchar_t *)xalloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return;
    GetWindowTextW(g_view, w, n + 1);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w, n, NULL, 0, NULL, NULL);
    char *u = (char *)xalloc((size_t)bytes + 4);
    if (u) {
        WideCharToMultiByte(CP_UTF8, 0, w, n, u, bytes, NULL, NULL);
        wchar_t p[PATHBUF + 8], lp[PATHBUF + 16];
        ScanLogPath(scanDir, p, PATHBUF + 8); LongPath(p, lp, PATHBUF + 16);
        HANDLE h = CreateFileW(lp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wr; WriteFile(h, "\xEF\xBB\xBF", 3, &wr, NULL); WriteFile(h, u, (DWORD)bytes, &wr, NULL); CloseHandle(h);
        }
        xfree(u);
    }
    xfree(w);
}

static void ShowScanLog(void) {
    if (!g_root[0]) return;
    wchar_t p[PATHBUF]; ScanLogPath(g_root, p, PATHBUF);
    ShowFile(p);                                   // reuses the file viewer (handles UTF-8)
    const wchar_t *bs = wcsrchr(g_root, L'\\');
    wchar_t lbl[PATHBUF + 64]; swprintf(lbl, PATHBUF + 64, L"Extraction log for scan \"%ls\"", bs ? bs + 1 : g_root);
    SetWindowTextW(g_viewlbl, lbl);
    g_dirItem = NULL;
    SendMessageW(g_view, EM_SETSEL, 0, 0); SendMessageW(g_view, EM_SCROLLCARET, 0, 0);
}

/* ---- removing an old extraction quickly: rename it out of the way (instant), delete it in the background.
   Leftover "_deleting_*" folders (app closed part-way) are finished off at the next start. ---- */
static volatile LONG g_delRunning = 0, g_delStop = 0;
#define WM_APP_DELDONE (WM_APP + 13)
static unsigned long long DeleteTreeW(const wchar_t *dir) {
    unsigned long long n = 0;
    wchar_t pat[PATHBUF + 16]; LongPath(dir, pat, PATHBUF + 8); wcscat(pat, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (g_delStop) break;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t p[PATHBUF], lp[PATHBUF + 16];
            swprintf(p, PATHBUF, L"%ls\\%ls", dir, fd.cFileName); LongPath(p, lp, PATHBUF + 16);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                n += DeleteTreeW(p);
                if (!RemoveDirectoryW(lp)) { SetFileAttributesW(lp, FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(lp); }
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(lp);
                else if (!DeleteFileW(lp)) { SetFileAttributesW(lp, FILE_ATTRIBUTE_NORMAL); DeleteFileW(lp); }
                n++;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return n;
}
static DWORD WINAPI DeleteOldWorkerBody(LPVOID p) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    unsigned long long total = 0;
    for (;;) {                                         // every _deleting_* folder in the output folder
        wchar_t root[PATHBUF], pat[PATHBUF + 32], dir[PATHBUF]; OutputRoot(root, PATHBUF);
        swprintf(pat, PATHBUF + 32, L"%ls\\_deleting_*", root);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) break;
        BOOL found = FALSE;
        do { if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { swprintf(dir, PATHBUF, L"%ls\\%ls", root, fd.cFileName); found = TRUE; break; } } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (!found || g_delStop) break;
        total += DeleteTreeW(dir);
        wchar_t lp[PATHBUF + 16]; LongPath(dir, lp, PATHBUF + 16);
        if (g_delStop || !RemoveDirectoryW(lp)) break;  // couldn't finish (file in use?) - try again next start
    }
    PostMessageW(g_wnd, WM_APP_DELDONE, 0, (LPARAM)total);
    (void)p;
    return 0;
}
static DWORD WINAPI DeleteOldWorker(LPVOID p) { InterlockedIncrement(&g_workers); DWORD r = DeleteOldWorkerBody(p); InterlockedDecrement(&g_workers); InterlockedExchange(&g_delRunning, 0); return r; }
static void StartDeleteOld(void) {
    if (InterlockedCompareExchange(&g_delRunning, 1, 0)) return;
    g_delStop = 0;
    HANDLE t = CreateThread(NULL, 0, DeleteOldWorker, NULL, 0, NULL);
    if (t) CloseHandle(t); else InterlockedExchange(&g_delRunning, 0);
}
static BOOL HaveDeletingFolders(void) {
    wchar_t root[PATHBUF], pat[PATHBUF + 32]; OutputRoot(root, PATHBUF);
    swprintf(pat, PATHBUF + 32, L"%ls\\_deleting_*", root);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    FindClose(h); return TRUE;
}
// 0 cancel, 1 replace, 2 overwrite in place
int UiChoiceDialog(HWND owner, const wchar_t *title, const wchar_t *heading, const wchar_t *text,
                   const wchar_t *const *titles, const wchar_t *const *descs, int n, int def);
static int AskReplaceScan(const wchar_t *name) {
    wchar_t head[PATHBUF + 64]; swprintf(head, PATHBUF + 64, L"A scan called \"%ls\" already exists", name);
    static const wchar_t *t[2] = { L"Replace it", L"Overwrite in place" };
    static const wchar_t *d[2] = { L"The old extraction is moved aside at once and deleted in the background while the new one runs. Recommended.",
                                   L"Files with the same names are replaced; anything the new extraction doesn't produce is left behind." };
    return UiChoiceDialog(g_wnd, L"ns2dig", head, L"What should happen to the old extraction?", t, d, 2, 1);   // 1 replace, 2 overwrite, 0 cancel
}

/* scan <-> game folder links (ini [linked]: <scan> = <16 hex digits: when, FILETIME>|<game folder>), written when an
   extraction starts (from build .170 on). Picking a scan fills in its game folder; browsing to a game folder picks its
   scan (the most recent if several). */
static void LinkScan(const wchar_t *scan, const wchar_t *folder) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    wchar_t v[PATHBUF + 40]; swprintf(v, PATHBUF + 40, L"%08lX%08lX|%ls", ft.dwHighDateTime, ft.dwLowDateTime, folder);
    WritePrivateProfileStringW(L"linked", scan, v, g_ini);
}
static BOOL LinkedFolder(const wchar_t *scan, wchar_t *out, size_t n) {
    wchar_t v[PATHBUF + 40]; GetPrivateProfileStringW(L"linked", scan, L"", v, PATHBUF + 40, g_ini);
    const wchar_t *bar = wcschr(v, L'|'); if (!bar || !bar[1]) return FALSE;
    swprintf(out, n, L"%ls", bar + 1); return TRUE;
}
// the most recent scan linked to this game folder (FALSE: none)
static BOOL LinkedScan(const wchar_t *folder, wchar_t *out, size_t n) {
    size_t cap = 65536; wchar_t *all = (wchar_t *)calloc(cap, sizeof(wchar_t)); if (!all) return FALSE;
    GetPrivateProfileSectionW(L"linked", all, (DWORD)cap, g_ini);
    wchar_t want[PATHBUF]; swprintf(want, PATHBUF, L"%ls", folder);
    { size_t l = wcslen(want); while (l > 3 && (want[l - 1] == L'\\' || want[l - 1] == L'/')) want[--l] = 0; }
    unsigned long long best = 0; BOOL found = FALSE;
    for (wchar_t *e = all; *e; e += wcslen(e) + 1) {               // "scan=time|folder"
        wchar_t *eq = wcschr(e, L'='); if (!eq) continue;
        wchar_t *bar = wcschr(eq + 1, L'|'); if (!bar) continue;
        wchar_t fol[PATHBUF]; swprintf(fol, PATHBUF, L"%ls", bar + 1);
        size_t l = wcslen(fol); while (l > 3 && (fol[l - 1] == L'\\' || fol[l - 1] == L'/')) fol[--l] = 0;
        if (_wcsicmp(fol, want)) continue;
        unsigned long long t = wcstoull(eq + 1, NULL, 16);
        if (!found || t > best) { best = t; found = TRUE; swprintf(out, n, L"%.*ls", (int)(eq - e), e); }
    }
    free(all); return found;
}
// the scan list: select this name (TRUE if it's there)
static BOOL SelectScanName(const wchar_t *name) {
    int i = (int)SendMessageW(g_out, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)name);
    if (i == CB_ERR) return FALSE;
    SendMessageW(g_out, CB_SETCURSEL, i, 0); SetWindowTextW(g_out, name);
    return TRUE;
}
static void StartExtract(void) {
    if (g_running) return;
    RunArgs *a = (RunArgs *)xalloc(sizeof(RunArgs));
    wchar_t name[PATHBUF];
    swprintf(a->script, PATHBUF, L"%ls", g_scriptPath);
    GetText(g_romfs, a->input, PATHBUF); GetText(g_out, name, PATHBUF);
    const wchar_t *err = NULL;
    DWORD at = GetFileAttributesW(a->input);
    if (GetFileAttributesW(a->script) == INVALID_FILE_ATTRIBUTES) err = L"ns2dig.py was not found. Put it in the same folder as ns2dig.exe.";
    else if (!a->input[0] || at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) err = L"Choose an existing RomFS folder.";
    else err = CheckScanName(name);
    if (err) { MessageBoxW(g_wnd, err, L"ns2dig", MB_ICONWARNING); xfree(a); return; }
    SetWindowTextW(g_out, name);
    ScanDir(name, a->output, PATHBUF);
    ClearSearchCache(name);           // saved search results for this scan would be out of date
    if (g_idx) { IdxFree(g_idx); g_idx = NULL; }   // so would the file-type index
    if (g_idxBuilding) g_idxCancel = 1;
    if (DirHasFiles(a->output)) {
        int how = AskReplaceScan(name);
        if (!how) { xfree(a); return; }
        if (how == 1) {
            // let go of the tree first (it may hold the folder open), then rename the old scan out of the way
            TreeClearFast(); g_root[0] = 0;
            wchar_t root[PATHBUF], aside[PATHBUF]; OutputRoot(root, PATHBUF);
            swprintf(aside, PATHBUF, L"%ls\\_deleting_%ls_%lu", root, name, GetTickCount());
            BOOL moved = FALSE;
            for (int tries = 0; tries < 5 && !moved; tries++) { moved = MoveFileExW(a->output, aside, 0); if (!moved) Sleep(200); }
            if (!moved) {
                wchar_t m[PATHBUF + 300];
                swprintf(m, PATHBUF + 300, L"The old extraction couldn't be moved aside (error %lu) - something has a file or folder in it open, "
                                           L"e.g. an Explorer window, a text editor, or a 3D/texture window from this app.\n\n"
                                           L"Overwrite it in place instead?", GetLastError());
                if (MessageBoxW(g_wnd, m, L"ns2dig", MB_YESNO | MB_ICONWARNING) != IDYES) { xfree(a); return; }
            } else StartDeleteOld();
        }
    }
    SaveSettings();
    LinkScan(name, a->input);                                  // (this scan came from this game folder)
    SetWindowTextW(g_view, L""); g_cur[0] = 0; g_curDir[0] = 0;
    SetWindowTextW(g_viewlbl, L"Extraction log");
    {
        SYSTEMTIME t; GetLocalTime(&t);
        wchar_t head[PATHBUF * 2 + 160];
        swprintf(head, PATHBUF * 2 + 160, L"Scan \"%ls\" - extracted %04d-%02d-%02d %02d:%02d from:\r\n  %ls\r\n\r\n",
                 name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, a->input);
        AppendView(head);
    }
    InterlockedExchange(&g_running, 1);
    // while it runs: nothing from the old scan on show, and only Stop can be clicked
    StopSearch();
    ClearResults(); g_lastTerm[0] = 0; g_lastScope[0] = 0;
    SetWindowTextW(g_sedit, L""); SetWindowTextW(g_reslbl, L"Search results:");
    UpdateVox3DButton();                                  // (nothing selected: the viewer buttons go)
    HdrSet(NULL, FALSE, L"Extracting...", L"The tree and the search come back when it finishes");
    LockUi(TRUE);
    { wchar_t cap[256]; GetWindowTextW(g_wnd, cap, 256); SetWindowTextW(g_wnd, cap); }   // (taskbar caption refresh)
    // fresh job object (kills its processes when the handle closes) and stop-request file
    if (g_job) { CloseHandle(g_job); g_job = NULL; }
    g_job = CreateJobObjectW(NULL, NULL);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; ZeroMemory(&li, sizeof(li));
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }
    { wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
      swprintf(g_stopFile, MAX_PATH, L"%lsalsarc_stop_%lu.flag", tmp, GetCurrentProcessId());
      DeleteFileW(g_stopFile); }
    InterlockedExchange(&g_stopRequested, 0);
    SetWindowTextW(g_extract, L"Stop");
    SetStatus(L"Starting Python...");
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    g_extractStart = GetTickCount();
    { RECT rc; GetClientRect(g_wnd, &rc); Layout(rc.right, rc.bottom); }
    HANDLE t = CreateThread(NULL, 0, RunWorker, a, 0, NULL);
    if (t) CloseHandle(t);
}

/* ------------------------------------------------------------------ browse dialogs */
// the modern Windows folder picker, opened at the folder already in the box (its parent shown, the folder itself
// selected, so nearby folders are one click away). Falls back to the old browse dialog if it isn't available.
static const GUID kClsidFileOpenDialog = { 0xDC1C5A9C, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 } };
static const GUID kIidFileOpenDialog = { 0xD57C7288, 0xD4AD, 0x4768, { 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60 } };
static const GUID kIidShellItem = { 0x43826D1E, 0xE718, 0x42EE, { 0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE } };
static BOOL PickFolderModern(HWND owner, const wchar_t *start, const wchar_t *title, wchar_t *out, size_t n) {
    IFileOpenDialog *dlg = NULL;
    if (FAILED(CoCreateInstance(&kClsidFileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &kIidFileOpenDialog, (void **)&dlg)) || !dlg) return FALSE;
    DWORD opt = 0; dlg->lpVtbl->GetOptions(dlg, &opt);
    dlg->lpVtbl->SetOptions(dlg, opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->lpVtbl->SetTitle(dlg, title);
    if (start && start[0]) {
        wchar_t dir[PATHBUF]; swprintf(dir, PATHBUF, L"%ls", start);
        size_t l = wcslen(dir); while (l > 3 && (dir[l - 1] == L'\\' || dir[l - 1] == L'/')) dir[--l] = 0;
        DWORD at = GetFileAttributesW(dir);
        if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) {
            wchar_t parent[PATHBUF]; swprintf(parent, PATHBUF, L"%ls", dir);
            wchar_t *bs = wcsrchr(parent, L'\\');
            IShellItem *it = NULL;
            if (bs && bs > parent + 2) {                          // show the folder next to its neighbours, selected
                *bs = 0;
                if (SUCCEEDED(SHCreateItemFromParsingName(parent, NULL, &kIidShellItem, (void **)&it)) && it) {
                    dlg->lpVtbl->SetFolder(dlg, it); it->lpVtbl->Release(it);
                    dlg->lpVtbl->SetFileName(dlg, bs + 1);
                }
            } else if (SUCCEEDED(SHCreateItemFromParsingName(dir, NULL, &kIidShellItem, (void **)&it)) && it) {
                dlg->lpVtbl->SetFolder(dlg, it); it->lpVtbl->Release(it);   // a drive root: open it
            }
        }
    }
    BOOL ok = FALSE;
    HRESULT hr = dlg->lpVtbl->Show(dlg, owner);
    if (SUCCEEDED(hr)) {
        IShellItem *res = NULL;
        if (SUCCEEDED(dlg->lpVtbl->GetResult(dlg, &res)) && res) {
            wchar_t *p = NULL;
            if (SUCCEEDED(res->lpVtbl->GetDisplayName(res, SIGDN_FILESYSPATH, &p)) && p) { swprintf(out, n, L"%ls", p); CoTaskMemFree(p); ok = TRUE; }
            res->lpVtbl->Release(res);
        }
    } else if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { out[0] = 0; ok = TRUE; }   // cancelled: nothing to do
    dlg->lpVtbl->Release(dlg);
    return ok;
}
static int CALLBACK BrowseInitProc(HWND h, UINT msg, LPARAM lp, LPARAM data) {
    (void)lp;
    if (msg == BFFM_INITIALIZED && data) { SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data); SendMessageW(h, BFFM_SETEXPANDED, TRUE, data); }
    return 0;
}
// every folder picker in the app: the full Explorer-style one (the old tree dialog only if Windows lacks it).
// TRUE with the chosen folder in out; FALSE if cancelled.
BOOL UiPickFolder(HWND owner, const wchar_t *start, const wchar_t *title, wchar_t *out, size_t n) {
    out[0] = 0;
    if (!PickFolderModern(owner, start, title, out, n)) {
        BROWSEINFOW bi; ZeroMemory(&bi, sizeof(bi));
        bi.hwndOwner = owner; bi.lpszTitle = title;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        bi.lpfn = BrowseInitProc; bi.lParam = (LPARAM)(start ? start : L"");
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return FALSE;
        wchar_t p2[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, p2)) swprintf(out, n, L"%ls", p2);
        CoTaskMemFree(pidl);
    }
    return out[0] != 0;
}
static void PickFolder(HWND target, const wchar_t *title) {
    wchar_t cur[PATHBUF]; GetWindowTextW(target, cur, PATHBUF);
    wchar_t path[PATHBUF] = L"";
    if (!UiPickFolder(g_wnd, cur, title, path, PATHBUF)) return;
    SetWindowTextW(target, path);
    if (target == g_romfs) SuggestScanName(path);
}

#include "ui_main.inc"

/* ------------------------------------------------------------------ layout & window */
static HWND MakeCtl(const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id) {
    BOOL tab = wcscmp(cls, L"STATIC") != 0;
    BOOL btn = !wcscmp(cls, L"BUTTON"); int isCheck = 0;
    if (btn) style = UiButtonStyle(style, &isCheck);
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style | (tab ? WS_TABSTOP : 0),
                             0, 0, 10, 10, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (btn && (style & BS_TYPEMASK) == BS_OWNERDRAW) UiAttachButton(h, isCheck, id);
    return h;
}

static int g_mainTop = 0;

static int BtnW(HWND h, int minw) {
    wchar_t t[128]; GetWindowTextW(h, t, 128);
    int w = TextW(t) + S(30) + (UiGlyphFor(GetDlgCtrlID(h)) ? S(23) : 0);   // room for the button's icon
    return w < S(minw) ? S(minw) : w;
}

// Place controls left to right from (x0, y), wrapping onto a new row when the next one won't fit before
// x0 + maxw. Hidden controls are skipped. extra[i] adds space after control i (to group related buttons).
// Returns the y just below the last row used.
typedef struct { HWND h; int w; int after; BOOL combo; } FlowItem;
static int FlowRow(FlowItem *it, int n, int x0, int y, int maxw, int rh, int gap) {
    int x = x0, used = 0;
    for (int i = 0; i < n; i++) {
        if (!it[i].h || !IsWindowVisible(it[i].h)) continue;
        int w = it[i].w; if (w > maxw) w = maxw;
        if (used && x + w > x0 + maxw) { x = x0; y += rh + gap; }
        MoveWindow(it[i].h, x, y, w, it[i].combo ? rh + S(300) : rh, TRUE);
        x += w + gap + it[i].after; used = 1;
    }
    return y + rh;
}

static void Layout(int w, int h) {
    int m = S(10), gap = S(6), rh = g_rowH, y = m;
    // status bar along the bottom: a "ready" dot and the status text, the build number on the right, and under
    // them a full-width progress bar (extracting and searching) that always has its place
    {
        int sbH = S(40); h -= sbH;
        SetRect(&g_sbRect, 0, h, w, h + sbH);
        MoveWindow(g_status, S(26), h + S(7), w - S(26) - S(190), S(18), TRUE);
        MoveWindow(g_progress, 0, h + sbH - S(10), w, S(6), TRUE);
    }
    int lblOff = S(5);   // vertically centre label text in the row
    // --- top right: search box and buttons, the search options under them
    int sw = S(520); if (sw > w / 2) sw = w / 2;
    int sx = w - m - sw;
    int yR = m, bx = sx;
    {
        int wS = BtnW(g_search, 80), wT = BtnW(g_stop, 60), wN = BtnW(g_next, 60);
        if (wN > wT) wT = wN;                                   // Stop and In File share one place, same size
        int boxW = (sw - wS - wT - gap * 2) * 8 / 10; if (boxW < S(120)) boxW = S(120);
        bx = w - m - wT - gap - wS - gap - boxW;                // right-aligned
        MoveWindow(g_sedit, bx, yR, boxW, rh + S(300), TRUE);
        UiComboHeight(g_sedit, rh);
        MoveWindow(g_search, bx + boxW + gap, yR, wS, rh, TRUE);
        MoveWindow(g_stop, w - m - wT, yR, wT, rh, TRUE);
        MoveWindow(g_next, w - m - wT, yR, wT, rh, TRUE);
        yR += rh + gap;
        wchar_t t[64]; int wc[3]; HWND cb[3] = { g_case, g_scope, g_allfiles };
        for (int i = 0; i < 3; i++) { GetWindowTextW(cb[i], t, 64); wc[i] = TextW(t) + S(30); }
        FlowItem it[] = { { g_case, wc[0], gap * 2 }, { g_scope, wc[1], gap * 2 }, { g_allfiles, wc[2], 0 } };
        yR = FlowRow(it, 3, bx, yR, w - m - bx, rh, S(4));
        ShowWindow(g_lbl[3], SW_HIDE);
    }
    // --- top left, two rows: game folder [Browse] [Extract] / scan name [Show in Explorer] [Scan log] [Stop ZIP]
    ShowWindow(g_load, SW_HIDE); ShowWindow(GetDlgItem(g_wnd, ID_SCRIPT), SW_HIDE);
    int leftR = bx - S(24);
    int lw = 0;
    for (int i = 0; i < 2; i++) { wchar_t t[64]; GetWindowTextW(g_lbl[i], t, 64); int tw = TextW(t); if (tw > lw) lw = tw; }
    lw += S(6);
    int ex = m + lw + gap;
    int bBrowse = BtnW(GetDlgItem(g_wnd, ID_ROMFS_BTN), 80), bExtract = BtnW(g_extract, 100);
    int bShow = BtnW(GetDlgItem(g_wnd, ID_OUT_BTN), 80), bLog = BtnW(g_scanlog, 80);
    int row2 = bShow + gap + bLog;
    if (bBrowse + gap + bExtract < row2) bExtract = row2 - bBrowse - gap;   // Extract's right edge lines up with Scan log's
    int row1 = bBrowse + gap + bExtract;
    int ew = leftR - ex - gap - (row1 > row2 ? row1 : row2);   // both boxes the same width, buttons after them
    if (ew > S(440)) ew = S(440);
    if (ew < S(140)) ew = S(140);
    MoveWindow(g_lbl[0], m, y + lblOff, lw, rh - lblOff, TRUE);
    MoveWindow(g_romfs, ex, y, ew, rh, TRUE);
    int bx2 = ex + ew + gap;
    MoveWindow(GetDlgItem(g_wnd, ID_ROMFS_BTN), bx2, y, bBrowse, rh, TRUE);
    MoveWindow(g_extract, bx2 + bBrowse + gap, y, bExtract, rh, TRUE);
    y += rh + gap;
    MoveWindow(g_lbl[1], m, y + lblOff, lw, rh - lblOff, TRUE);
    MoveWindow(g_out, ex, y, ew, rh + S(300), TRUE);
    UiComboHeight(g_out, rh);
    MoveWindow(GetDlgItem(g_wnd, ID_OUT_BTN), bx2, y, bShow, rh, TRUE);
    MoveWindow(g_scanlog, bx2 + bShow + gap, y, bLog, rh, TRUE);
    y += rh;
    // a thin vertical line between this section and the search section
    { int lx = (bx2 + row1 + bx) / 2; if (lx < bx2 + row1 + S(8)) lx = bx2 + row1 + S(8); SetRect(&g_sepRect, lx, m, lx + 1, (y > yR ? y : yR)); }
    if (yR > y) y = yR;
    y += S(14);
    g_mainTop = y;
    int mh = h - y - m; if (mh < S(80)) mh = S(80);
    int split = S(g_split);
    if (split > w - S(400)) split = w - S(400);
    if (split < S(150)) split = S(150);
    // --- left pane: file-type filter, tree, results
    {
        int tb = BtnW(g_typesbtn, 110); if (tb > split) tb = split;
        MoveWindow(g_typesbtn, m, y, tb, rh, TRUE);
        // extra buttons go to the right of the file-type filter (Stop ZIP while a ZIP is being made)
        FlowItem extra[] = { { g_bmBtn, BtnW(g_bmBtn, 90), 0 }, { g_bulkBtn, BtnW(g_bulkBtn, 80), 0 }, { g_zipstop, BtnW(g_zipstop, 80), 0 } };
        FlowRow(extra, 3, m + tb + gap, y, split - tb - gap, rh, S(4));
        ShowWindow(g_gobtn, SW_SHOW);
    }
    int treeTop = y + rh + S(6);
    int treeH = (mh - rh - S(6)) * 58 / 100;
    MoveWindow(g_tree, m, treeTop, split, treeH, TRUE);
    {
        int cb = BtnW(g_copyres, 70), ry = treeTop + treeH + S(6);
        MoveWindow(g_reslbl, m, ry + S(5), split - cb - gap, rh - S(5), TRUE);
        MoveWindow(g_copyres, m + split - cb, ry, cb, rh, TRUE);
        MoveWindow(g_results, m, ry + rh + S(6), split, y + mh - (ry + rh + S(6)), TRUE);
    }
    // --- right pane: path box (shows the open file; paste a path to go to it), header (preview, name, type),
    //     tool buttons (wrap as needed), then the contents
    {
        int rx = m + split + gap * 2, rw = w - rx - m;
        int gb = BtnW(g_gobtn, 50);
        MoveWindow(g_gopath, rx, y, rw - gb - gap, rh, TRUE);
        MoveWindow(g_gobtn, rx + rw - gb, y, gb, rh, TRUE);
        y += rh + S(12);
        int hh = S(100), pw = S(156);
        SetRect(&g_hdrRect, rx, y, rx + rw, y + hh);
        SetRect(&g_prevRect, rx, y, rx + pw, y + hh);
        MoveWindow(g_viewlbl, rx + pw + S(16), y + S(70), rw - pw - S(16), rh - S(6), TRUE);
        y += hh + S(10);
        HWND vb[7] = { g_vox3d, g_playsnd, g_viewtex, g_viewlyt, g_viewfnt, g_viewlgt, g_viewevt };
        int vw = 0; for (int i = 0; i < 7; i++) { int bw2 = BtnW(vb[i], 70); if (bw2 > vw) vw = bw2; }   // all the same width
        FlowItem it[] = { { g_bmStar, rh, 0 }, { g_opendir, BtnW(g_opendir, 90), 0 }, { g_openrom, BtnW(g_openrom, 90), gap * 3 },
                          { g_vox3d, vw, 0 }, { g_playsnd, vw, 0 }, { g_viewtex, vw, 0 }, { g_viewlyt, vw, 0 },
                          { g_viewfnt, vw, 0 }, { g_viewlgt, vw, 0 }, { g_viewevt, vw, 0 } };
        int ry = FlowRow(it, (int)(sizeof(it) / sizeof(it[0])), rx, y, rw, rh, S(4)) + S(10);
        int vh = g_mainTop + mh - ry; if (vh < S(40)) vh = S(40);
        MoveWindow(g_view, rx, ry, rw, vh, TRUE);
    }
    // repaint the whole window and every control: moved controls otherwise leave copies of themselves behind
    RedrawWindow(g_wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static BOOL OverSplitter(int x, int y) {
    int sx = S(10) + S(g_split);
    return y >= g_mainTop && y < g_sbRect.top && x >= sx - S(2) && x < sx + S(12) + S(2);
}

static void ApplyFonts(void) {
    HWND c = GetWindow(g_wnd, GW_CHILD);
    while (c) { SendMessageW(c, WM_SETFONT, (WPARAM)(c == g_view ? g_mono : g_font), TRUE); c = GetWindow(c, GW_HWNDNEXT); }
    MeasureRow();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hwnd;
        g_dpi = QueryDpi(hwnd);
        CreateFonts();

        g_lbl[0] = MakeCtl(L"STATIC", L"Game folder:", 0, 0, 0);
        g_lbl[1] = MakeCtl(L"STATIC", L"Scan name:", 0, 0, 0);
        g_lbl[2] = MakeCtl(L"STATIC", L"", 0, 0, 0); ShowWindow(g_lbl[2], SW_HIDE);
        g_lbl[3] = MakeCtl(L"STATIC", L"Search:", 0, 0, 0);
        g_romfs  = MakeCtl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_ROMFS);
        g_out    = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_SORT | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL, 0, ID_OUT);
        MakeCtl(L"STATIC", L"Saved in the \"output\" folder next to this program. Pick an earlier scan from the list to open it.",
                SS_ENDELLIPSIS, 0, ID_SCRIPT);
        MakeCtl(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 0, ID_ROMFS_BTN);
        MakeCtl(L"BUTTON", L"Show in Explorer", BS_PUSHBUTTON, 0, ID_OUT_BTN);
        g_extract = MakeCtl(L"BUTTON", L"Extract", BS_PUSHBUTTON, 0, ID_EXTRACT);
        g_load    = MakeCtl(L"BUTTON", L"Open scan", BS_PUSHBUTTON, 0, ID_LOAD);
        g_sedit   = MakeCtl(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL, 0, ID_SEARCH_EDIT);
        g_search  = MakeCtl(L"BUTTON", L"Search", BS_PUSHBUTTON, 0, ID_SEARCH);
        g_stop    = MakeCtl(L"BUTTON", L"Stop", BS_PUSHBUTTON, 0, ID_STOP);
        g_next    = MakeCtl(L"BUTTON", L"In File", BS_PUSHBUTTON, 0, ID_NEXT);
        g_case    = MakeCtl(L"BUTTON", L"Match case", BS_AUTOCHECKBOX, 0, ID_CASE);
        g_allfiles = MakeCtl(L"BUTTON", L"List every file", BS_AUTOCHECKBOX, 0, ID_ALLFILES);
        g_scope    = MakeCtl(L"BUTTON", L"Only in selected folder", BS_AUTOCHECKBOX, 0, ID_SCOPE);
        g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, 0, 0, 10, 10, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_progress, PBM_SETRANGE32, 0, 1000);
        g_sprog = g_progress;                 // one progress bar (bottom of the window) for extracting and searching
        ShowWindow(g_progress, SW_SHOW);
        g_status  = MakeCtl(L"STATIC", L"", SS_ENDELLIPSIS, 0, ID_STATUS);
        g_tree    = MakeCtl(WC_TREEVIEWW, L"", TVS_HASBUTTONS | TVS_FULLROWSELECT | TVS_LINESATROOT |
                            TVS_SHOWSELALWAYS | TVS_TRACKSELECT, 0, ID_TREE);
        g_reslbl  = MakeCtl(L"STATIC", L"Search results:", SS_ENDELLIPSIS, 0, ID_RESULTS_LBL);
        g_copyres = MakeCtl(L"BUTTON", L"Copy all", BS_PUSHBUTTON, 0, ID_COPYRES);
        g_gopath  = MakeCtl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_GOPATH);
        g_gobtn   = MakeCtl(L"BUTTON", L"Go", BS_PUSHBUTTON, 0, ID_GOBTN);
        g_typesbtn = MakeCtl(L"BUTTON", L"File types: all", BS_PUSHBUTTON, 0, ID_TYPES);
        g_bmBtn   = MakeCtl(L"BUTTON", L"Bookmarks", BS_PUSHBUTTON, 0, ID_BOOKMARKS);
        g_bulkBtn = MakeCtl(L"BUTTON", L"Bulk ZIP", BS_PUSHBUTTON, 0, ID_BULKZIP);
        g_zipstop = MakeCtl(L"BUTTON", L"Stop ZIP", BS_PUSHBUTTON, 0, ID_ZIPSTOP);
        ShowWindow(g_zipstop, SW_HIDE);
        SendMessageW(g_gopath, EM_SETCUEBANNER, TRUE, (LPARAM)L"Paste any path and press Enter to go to it");
        g_results = MakeCtl(L"LISTBOX", L"", LBS_NOTIFY | LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                            WS_VSCROLL | WS_HSCROLL, 0, ID_RESULTS);
        g_viewlbl = MakeCtl(L"STATIC", L"", SS_PATHELLIPSIS, 0, ID_VIEW_LBL);
        g_opendir = MakeCtl(L"BUTTON", L"Open JSON", BS_PUSHBUTTON, 0, ID_OPENDIR);
        g_openrom = MakeCtl(L"BUTTON", L"Open ROM", BS_PUSHBUTTON, 0, ID_OPENROM);
        g_scanlog = MakeCtl(L"BUTTON", L"Scan log", BS_PUSHBUTTON, 0, ID_SCANLOG);
        g_bmStar  = MakeCtl(L"BUTTON", L"", BS_PUSHBUTTON, 0, ID_BMSTAR);
        EnableWindow(g_bmStar, FALSE);
        {   // the star has no text: a tooltip says what it does
            HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                       CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL, GetModuleHandleW(NULL), NULL);
            TTTOOLINFOW ti = { sizeof ti }; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = hwnd; ti.uId = (UINT_PTR)g_bmStar;
            ti.lpszText = (LPWSTR)L"Bookmark this (click again to remove it) - see the Bookmarks menu";
            if (tip) SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
        g_vox3d   = MakeCtl(L"BUTTON", L"3D view", BS_PUSHBUTTON, 0, ID_VOX3D);
        EnableWindow(g_vox3d, FALSE); ShowWindow(g_vox3d, SW_HIDE);
        g_playsnd = MakeCtl(L"BUTTON", L"Play", BS_PUSHBUTTON, 0, ID_PLAYSND);
        EnableWindow(g_playsnd, FALSE); ShowWindow(g_playsnd, SW_HIDE);
        g_viewtex = MakeCtl(L"BUTTON", L"Textures", BS_PUSHBUTTON, 0, ID_VIEWTEX);
        EnableWindow(g_viewtex, FALSE); ShowWindow(g_viewtex, SW_HIDE);
        g_viewlyt = MakeCtl(L"BUTTON", L"Layout", BS_PUSHBUTTON, 0, ID_VIEWLYT);
        EnableWindow(g_viewlyt, FALSE); ShowWindow(g_viewlyt, SW_HIDE);
        g_viewfnt = MakeCtl(L"BUTTON", L"Font", BS_PUSHBUTTON, 0, ID_VIEWFNT);
        EnableWindow(g_viewfnt, FALSE); ShowWindow(g_viewfnt, SW_HIDE);
        g_viewlgt = MakeCtl(L"BUTTON", L"Lighting", BS_PUSHBUTTON, 0, ID_VIEWLGT);
        EnableWindow(g_viewlgt, FALSE); ShowWindow(g_viewlgt, SW_HIDE);
        g_viewevt = MakeCtl(L"BUTTON", L"Event", BS_PUSHBUTTON, 0, ID_VIEWEVT);
        EnableWindow(g_viewevt, FALSE); ShowWindow(g_viewevt, SW_HIDE);
        EnableWindow(g_scanlog, FALSE);
        EnableWindow(g_opendir, FALSE); EnableWindow(g_openrom, FALSE);
        g_view    = MakeCtl(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                            ES_NOHIDESEL | WS_VSCROLL | WS_HSCROLL, 0, ID_VIEW);
        ApplyFonts();
        UiMakeIcons();
        UiStyleControls();
        UiStyleInputs();
        SetWindowSubclass(g_view, UiViewProc, 4, 0);
        SendMessageW(g_view, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(10), S(6)));
        SendMessageW(g_view, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(g_results, LB_SETHORIZONTALEXTENT, 4000, 0);
        StopOrNext(FALSE);
        SendMessageW(g_sedit, CB_SETCUEBANNER, 0, (LPARAM)L"Search text, values or file names");
        SendMessageW(g_sedit, CB_SETMINVISIBLE, 15, 0);
        SendMessageW(g_sedit, CB_LIMITTEXT, 500, 0);
        CrcInit();

        wchar_t exe[PATHBUF]; GetModuleFileNameW(NULL, exe, PATHBUF);
        swprintf(g_ini, PATHBUF, L"%ls", exe);
        wchar_t *dot = wcsrchr(g_ini, L'.'); if (dot) wcscpy(dot, L".ini");
        if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {   // renamed (AlsarcDumpGUI -> ns2dig -> ns2dig): keep the old settings
            static const wchar_t *olds[2] = { L"ns2digger.ini", L"AlsarcDumpGUI.ini" };
            for (int k = 0; k < 2 && GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES; k++) {
                wchar_t old[PATHBUF]; swprintf(old, PATHBUF, L"%ls", g_ini);
                wchar_t *sl = wcsrchr(old, L'\\'); if (sl) { wcscpy(sl + 1, olds[k]); if (GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES) CopyFileW(old, g_ini, TRUE); }
            }
        }
        wchar_t *slash = wcsrchr(exe, L'\\');
        if (slash) *slash = 0;
        swprintf(g_exeDir, PATHBUF, L"%ls", exe);
        swprintf(g_scriptPath, PATHBUF, L"%ls\\ns2dig.py", g_exeDir);
        g_stage = "startup: loading settings";
        LoadSettings();
        g_stage = "startup: file types button";
        UpdateTypesButton();
        g_stage = "startup: recent searches";
        LoadRecent();
        g_stage = "startup: listing scans / opening the last scan";
        RefreshScans();
        g_stage = "startup: welcome text";
        if (HaveDeletingFolders()) StartDeleteOld();      // finish removing old extractions from last time
        SetWindowTextW(g_viewlbl, L"");
        HdrSet(NULL, FALSE, L"Welcome to ns2dig", L"Getting started");
        SetWindowTextW(g_view,
            L"1. Extract: choose a RomFS folder - or the top folder of a dump with base, updates and dlc -\r\n"
            L"   type a scan name, then click Extract.\r\n"
            L"   Each scan is saved in its own subfolder of the \"output\" folder next to this program.\r\n"
            L"   (Needs Python 3.14 or newer, or an older Python 3 with:  pip install zstandard)\r\n"
            L"2. Browse: pick an earlier scan from the Scan name list to open it.\r\n"
            L"   Click files in the tree to view their JSON here.\r\n"
            L"3. Search: type a word or value and press Enter. Click a result to jump to it.\r\n"
            L"   F3 finds the next match in the open file. Ctrl+F jumps to the search box.\r\n"
            L"   The arrow on the search box lists recent searches. Finished searches are saved for each scan,\r\n"
            L"   so running one again shows its results straight away (they're cleared when the scan is re-extracted;\r\n"
            L"   right-click a result > Search again to refresh them by hand).\r\n"
            L"4. ZIP: Shift+click or Ctrl+click results to pick several (Ctrl+A for all), then right-click >\r\n"
            L"   Create ZIP. Right-click a folder or file in the tree to ZIP it too.\r\n"
            L"   Wildcards in the search box: * = any characters, ? = one character (e.g. Hill*Parts3?).\r\n"
            L"5. File types (button above the tree): tick types such as bars and bwav (or click + Sounds / + 3D)\r\n"
            L"   to show only those files in the tree, folder view and search results. Folders without any are hidden.\r\n"
            L"\r\nDrag the gap between the left and right panes to resize them.\r\n");
        if (GetFileAttributesW(g_scriptPath) == INVALID_FILE_ATTRIBUTES)
            AppendView(L"\r\nWARNING: ns2dig.py was not found next to this program. Put it in the same folder to extract.\r\n");
        {
            wchar_t name[PATHBUF], dir[PATHBUF]; GetText(g_out, name, PATHBUF);
            if (name[0] && !CheckScanName(name)) {
                ScanDir(name, dir, PATHBUF);
                DWORD at = GetFileAttributesW(dir);
                if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) { LoadTree(TRUE); SetWindowTextW(g_viewlbl, L""); HdrSet(NULL, FALSE, L"Welcome to ns2dig", L"Getting started"); }
            }
        }
        return 0;
    }
    case WM_SIZE: Layout(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        UiPaintHeader(dc);
        UiPaintStatusBar(dc);
        if (g_sepRect.bottom > g_sepRect.top) { HBRUSH lb = CreateSolidBrush(UI_LINE2); FillRect(dc, &g_sepRect, lb); DeleteObject(lb); }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
        return UiCtlColor(msg, (HDC)wp, (HWND)lp);
    case WM_APP_THUMB: UiThumbArrived((ThumbJob *)lp); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = S(1000); ((MINMAXINFO *)lp)->ptMinTrackSize.y = S(520); return 0;
    case 0x02E0: { /* WM_DPICHANGED */
        g_dpi = HIWORD(wp) ? HIWORD(wp) : 96;
        CreateFonts(); ApplyFonts(); UiMakeIcons(); TreeView_SetItemHeight(g_tree, S(26));
        RECT *r = (RECT *)lp;
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RECT rc; GetClientRect(hwnd, &rc); Layout(rc.right, rc.bottom);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        if ((HWND)wp == hwnd && OverSplitter(pt.x, pt.y)) { SetCursor(LoadCursor(NULL, IDC_SIZEWE)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN:
        if (OverSplitter((short)LOWORD(lp), (short)HIWORD(lp))) { g_dragging = 1; SetCapture(hwnd); }
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging) {
            RECT rc; GetClientRect(hwnd, &rc);
            g_split = MulDiv((short)LOWORD(lp) - S(8) - S(3), 96, g_dpi);
            Layout(rc.right, rc.bottom);
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) { g_dragging = 0; ReleaseCapture(); }
        return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->hwndFrom == g_tree && nh->code == NM_CUSTOMDRAW) return UiTreeCustomDraw((NMTVCUSTOMDRAW *)lp);
        if (nh->hwndFrom == g_tree) {
            if (nh->code == TVN_ITEMEXPANDINGW) {
                NMTREEVIEWW *tv = (NMTREEVIEWW *)lp;
                if (tv->action & TVE_EXPAND) {
                    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
                    FillFolder(tv->itemNew.hItem);
                    SetCursor(old);
                }
            } else if (nh->code == NM_RCLICK) {
                TVHITTESTINFO ht = { 0 }; POINT scr; GetCursorPos(&scr); ht.pt = scr; ScreenToClient(g_tree, &ht.pt);
                HTREEITEM hit = TreeView_HitTest(g_tree, &ht);
                if (hit && (ht.flags & TVHT_ONITEM)) {
                    TVITEMW ti = { 0 }; ti.mask = TVIF_PARAM; ti.hItem = hit; TreeView_GetItem(g_tree, &ti);
                    wchar_t p[PATHBUF]; ItemPath(hit, p, PATHBUF);
                    TreeView_SelectDropTarget(g_tree, hit);          // highlight while the menu is open
                    PathMenu(p, ti.lParam != 0, scr, FALSE);
                    TreeView_SelectDropTarget(g_tree, NULL);
                }
                return TRUE;
            } else if (nh->code == NM_DBLCLK) {
                TVHITTESTINFO ht = { 0 }; GetCursorPos(&ht.pt); ScreenToClient(g_tree, &ht.pt);
                HTREEITEM hit = TreeView_HitTest(g_tree, &ht);
                if (hit && (ht.flags & TVHT_ONITEM)) {
                    TVITEMW ti = { 0 }; ti.mask = TVIF_PARAM; ti.hItem = hit; TreeView_GetItem(g_tree, &ti);
                    if (ti.lParam == 0) {                         // a file: open its viewer, if it has one
                        wchar_t p[PATHBUF]; ItemPath(hit, p, PATHBUF);
                        // open it a moment later: done right now, the tree takes the focus back when the
                        // double-click finishes and the new window ends up behind this one
                        if (HasViewer(p)) { wcscpy(g_dblPath, p); SetTimer(hwnd, TIMER_OPEN_VIEWER, 30, NULL); return TRUE; }
                    }
                }
            } else if (nh->code == NM_CLICK) {
                // clicking the item that is already selected doesn't fire TVN_SELCHANGED; handle it here
                TVHITTESTINFO ht = { 0 }; GetCursorPos(&ht.pt); ScreenToClient(g_tree, &ht.pt);
                HTREEITEM hit = TreeView_HitTest(g_tree, &ht);
                if (hit && (ht.flags & TVHT_ONITEM) && hit == TreeView_GetSelection(g_tree)) {
                    TVITEMW ti = { 0 }; ti.mask = TVIF_PARAM; ti.hItem = hit; TreeView_GetItem(g_tree, &ti);
                    if (ti.lParam != 0) { if (g_dirItem != hit) ShowDirTree(hit); }
                    else { wchar_t p[PATHBUF]; ItemPath(hit, p, PATHBUF); if (_wcsicmp(p, g_cur)) { g_dirItem = NULL; ShowFile(p); } }
                }
            } else if (nh->code == TVN_SELCHANGEDW) {
                NMTREEVIEWW *tv = (NMTREEVIEWW *)lp;
                if (tv->itemNew.lParam == 0 && tv->itemNew.hItem) {
                    wchar_t p[PATHBUF]; ItemPath(tv->itemNew.hItem, p, PATHBUF);
                    g_dirItem = NULL;
                    ShowFile(p);
                } else if (tv->itemNew.hItem && !g_suppressDirView) {
                    ShowDirTree(tv->itemNew.hItem);
                }
            }
        }
        return 0;
    }
    case WM_CONTEXTMENU:
        if (g_running) return 0;                           // extracting: nothing to click but Stop
        if ((HWND)wp == g_results) {
            POINT scr = { (short)LOWORD(lp), (short)HIWORD(lp) };
            int idx;
            if (lp == -1) { idx = (int)SendMessageW(g_results, LB_GETCARETINDEX, 0, 0); RECT r; GetWindowRect(g_results, &r); scr.x = r.left + 20; scr.y = r.top + 20; }
            else {
                POINT c = scr; ScreenToClient(g_results, &c);
                LRESULT hit = SendMessageW(g_results, LB_ITEMFROMPOINT, 0, MAKELPARAM(c.x, c.y));
                idx = HIWORD(hit) ? -1 : (int)LOWORD(hit);
            }
            if (idx >= 0 && idx < (int)SendMessageW(g_results, LB_GETCOUNT, 0, 0) && SendMessageW(g_results, LB_GETSEL, idx, 0) <= 0) {
                // right-clicking an unselected result selects just that one (like Explorer)
                SendMessageW(g_results, LB_SETSEL, FALSE, -1);
                SendMessageW(g_results, LB_SETSEL, TRUE, idx);
                SendMessageW(g_results, LB_SETCARETINDEX, idx, 0);
                OpenResult();
            }
            if (idx >= 0) {
                Result *r = (Result *)SendMessageW(g_results, LB_GETITEMDATA, idx, 0);
                if (r && r != (Result *)LB_ERR) PathMenu(r->path, FALSE, scr, TRUE);
            } else ResultsBlankMenu(scr);           // empty part of the list (or no results at all)
            return 0;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wp) == CBN_SETFOCUS || HIWORD(wp) == CBN_KILLFOCUS) UiInputFocusChanged((HWND)lp);
        switch (LOWORD(wp)) {
        case IDOK:
            if (GetFocus() == g_sedit || GetParent(GetFocus()) == g_sedit) {
                if (SendMessageW(g_sedit, CB_GETDROPPEDSTATE, 0, 0)) SendMessageW(g_sedit, CB_SHOWDROPDOWN, FALSE, 0);
                StartSearch(FALSE);
            }
            else if (GetFocus() == g_out || GetParent(GetFocus()) == g_out) LoadTree(TRUE);
            else if (GetFocus() == g_gopath) GoToPath();
            break;
        case ID_GOBTN:      GoToPath(); break;
        case ID_TYPES:      ShowTypesDialog(); break;
        case ID_BOOKMARKS:  ShowBookmarksMenu(); break;
        case ID_BULKZIP:    ShowBulkZip(); break;
        case ID_BMSTAR:     ToggleBookmark(); break;
        case ID_ZIPSTOP:    StopZip(); break;
        case ID_PLAYSND: if (IsSoundOut(g_cur)) OpenSound(g_cur); break;
        case ID_VIEWTEX: if (IsTexOut(g_cur, FALSE)) OpenTex(g_cur); break;
        case ID_VIEWLYT: if (IsLytOut(g_cur, FALSE)) OpenLyt(g_cur); break;
        case ID_VIEWFNT: if (IsFontOut(g_cur, FALSE)) OpenFont(g_cur); break;
        case ID_VIEWLGT:
            if (g_cur[0] && IsLgtOut(g_cur, FALSE)) OpenLgt(g_cur, FALSE);
            else if (!g_cur[0] && g_curDir[0] && IsLgtOut(g_curDir, TRUE)) OpenLgt(g_curDir, TRUE);
            break;
        case ID_VIEWEVT: if (IsEvtOut(g_cur)) OpenEvt(g_cur); break;
        case ID_VOX3D: {
            if (g_cur[0]) Open3DFor(g_cur, FALSE, TRUE); else if (g_curDir[0]) Open3DFor(g_curDir, TRUE, TRUE);
            break;
        }
        case ID_ROMFS_BTN: {
            wchar_t before[PATHBUF]; GetText(g_romfs, before, PATHBUF);
            PickFolder(g_romfs, L"Choose the folder to extract: a RomFS folder, or the top folder of a dump (base, updates and dlc are all included)");
            wchar_t now[PATHBUF], sc[PATHBUF], cur[PATHBUF]; GetText(g_romfs, now, PATHBUF); GetText(g_out, cur, PATHBUF);
            if (now[0] && _wcsicmp(now, before) && LinkedScan(now, sc, PATHBUF) && _wcsicmp(sc, cur) && !g_running && SelectScanName(sc)) {
                LoadTree(TRUE);                                    // (its scan: the most recent extraction of this folder)
                wchar_t st[PATHBUF + 80]; swprintf(st, PATHBUF + 80, L"Opened the scan \"%ls\", extracted from this game folder.", sc); SetStatus(st);
            }
            break;
        }
        case ID_OUT_BTN: {
            wchar_t name[PATHBUF], dir[PATHBUF]; GetText(g_out, name, PATHBUF);
            if (name[0] && !CheckScanName(name)) ScanDir(name, dir, PATHBUF); else OutputRoot(dir, PATHBUF);
            if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) OutputRoot(dir, PATHBUF);
            CreateDirectoryW(dir, NULL);
            ShellExecuteW(hwnd, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case ID_OUT:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int i = (int)SendMessageW(g_out, CB_GETCURSEL, 0, 0);
                wchar_t t[PATHBUF];
                if (i != CB_ERR && SendMessageW(g_out, CB_GETLBTEXTLEN, i, 0) < PATHBUF) {
                    SendMessageW(g_out, CB_GETLBTEXT, i, (LPARAM)t);
                    SetWindowTextW(g_out, t);
                    wchar_t fol[PATHBUF]; BOOL linked = LinkedFolder(t, fol, PATHBUF);
                    if (linked) SetWindowTextW(g_romfs, fol);       // (the game folder it was extracted from)
                    LoadTree(TRUE);
                    // a scan from before the links (build .170) or never linked: its remembered dump folder, else the
                    // "from:" line of its scan log - and link it now, dated 0 so it never beats a real extraction
                    if (!linked && g_root[0] && ScanSource(t, fol) && fol[0]) {
                        SetWindowTextW(g_romfs, fol);
                        WritePrivateProfileStringW(L"linked", t, NULL, g_ini);
                        wchar_t v[PATHBUF + 40]; swprintf(v, PATHBUF + 40, L"0000000000000000|%ls", fol);
                        WritePrivateProfileStringW(L"linked", t, v, g_ini);
                    }
                }
            }
            break;
        case ID_EXTRACT:
            if (!g_running) StartExtract();
            else if (!g_stopRequested) {
                InterlockedExchange(&g_stopRequested, 1);
                HANDLE f = CreateFileW(g_stopFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
                if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
                EnableWindow(g_extract, FALSE); SetWindowTextW(g_extract, L"Stopping...");
                SetStatus(L"Stopping after the current file...");
                SetTimer(hwnd, TIMER_FORCE_STOP, 10000, NULL);   // force it if it doesn't stop in 10 s
            }
            break;
        case ID_LOAD:       LoadTree(FALSE); break;
        case ID_SEARCH:     StartSearch(FALSE); break;
        case ID_SEARCH_EDIT:
            // picking a recent search from the list runs it (saved results load instantly)
            // (acted on after the drop-down closes, whatever order the notifications come in)
            if (HIWORD(wp) == CBN_DROPDOWN) { g_recentOpen = TRUE; g_recentPicked = FALSE; }
            else if (HIWORD(wp) == CBN_SELENDOK && g_recentOpen) g_recentPicked = TRUE;
            if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_SELENDOK) { int ci = (int)SendMessageW(g_sedit, CB_GETCURSEL, 0, 0); if (ci >= 0) g_pickIdx = ci; }
            else if (HIWORD(wp) == CBN_EDITCHANGE) g_pickIdx = -1;
            else if (HIWORD(wp) == CBN_CLOSEUP) PostMessageW(hwnd, WM_APP_RECENT, 0, 0);
            break;
        case ID_STOP:       StopSearch(); break;
        case ID_NEXT:       FindNextInView(); break;
        case ID_ALLFILES:   if (g_dirItem) ShowDirTree(g_dirItem); break;
        case ID_OPENDIR:    OpenCurrentFolder(); break;
        case ID_OPENROM:    OpenRomFolder(); break;
        case ID_SCANLOG:    ShowScanLog(); break;
        case ID_COPYRES:    CopyAllResults(); break;
        case ID_RESULTS:
            if (HIWORD(wp) == LBN_SELCHANGE || HIWORD(wp) == LBN_DBLCLK) OpenResult();
            if (HIWORD(wp) == LBN_DBLCLK && SendMessageW(g_results, LB_GETSELCOUNT, 0, 0) == 1 && HasViewer(g_cur)) {
                wcscpy(g_dblPath, g_cur); SetTimer(hwnd, TIMER_OPEN_VIEWER, 30, NULL);
            }
            break;
        }
        return 0;
    case WM_APP_LOG: {
        wchar_t *s = (wchar_t *)lp;
        if (s) { AppendView(s); xfree(s); }
        return 0;
    }
    case WM_APP_PROGRESS: {
        long d = (long)wp, t = (long)lp;
        if (t > 0) {
            SendMessageW(g_progress, PBM_SETPOS, (WPARAM)(d * 1000LL / t), 0);
            wchar_t st[160], eta[64] = L"";
            DWORD el = GetTickCount() - g_extractStart;
            if (d > 0 && d < t && el > 3000) {
                DWORD rem = (DWORD)((double)el * (t - d) / d / 1000.0);
                if (rem >= 3600) swprintf(eta, 64, L", about %lu h %lu min left", rem / 3600, (rem % 3600) / 60);
                else if (rem >= 60) swprintf(eta, 64, L", about %lu min %lu s left", rem / 60, rem % 60);
                else swprintf(eta, 64, L", about %lu s left", rem);
            }
            if (g_stopRequested) swprintf(st, 160, L"Stopping after the current file... (%ld of %ld files done)", d, t);
            else swprintf(st, 160, L"Extracting: %ld of %ld files (%ld%%)%ls", d, t, (long)(d * 100LL / t), eta);
            SetStatus(st);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_OPEN_VIEWER) {
            KillTimer(hwnd, TIMER_OPEN_VIEWER);
            if (GetCapture()) { SetTimer(hwnd, TIMER_OPEN_VIEWER, 30, NULL); return 0; }   // mouse still down
            wchar_t p[PATHBUF]; wcscpy(p, g_dblPath); g_dblPath[0] = 0;
            if (p[0]) OpenViewerFor(p);
            return 0;
        }
        if (wp == TIMER_FORCE_STOP) {
            KillTimer(hwnd, TIMER_FORCE_STOP);
            if (g_running && g_job) { AppendView(L"\r\n(Forcing the stop - the script didn't finish its current file in time.)\r\n"); TerminateJobObject(g_job, 3); }
        }
        return 0;
    case WM_APP_DONE: {
        KillTimer(hwnd, TIMER_FORCE_STOP);
        DeleteFileW(g_stopFile);
        if (g_job) { CloseHandle(g_job); g_job = NULL; }
        SendMessageW(g_progress, PBM_SETPOS, 0, 0);
        { RECT rc; GetClientRect(g_wnd, &rc); Layout(rc.right, rc.bottom); }
        InterlockedExchange(&g_running, 0);
        g_bmRootFor[0] = 0;                                    // (a scan has finished: tell the game again next time)
        LockUi(FALSE);
        { wchar_t cap[256]; GetWindowTextW(g_wnd, cap, 256); SetWindowTextW(g_wnd, cap); }
        EnableWindow(g_extract, TRUE); SetWindowTextW(g_extract, L"Extract");
        BOOL wasStopped = g_stopRequested;
        InterlockedExchange(&g_stopRequested, 0);
        if ((DWORD)wp == 0 || wasStopped) {
            wchar_t name[PATHBUF], dir[PATHBUF]; GetText(g_out, name, PATHBUF); ScanDir(name, dir, PATHBUF);
            DWORD at = GetFileAttributesW(dir);
            if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) {
                AppendView(L"\r\nThe script finished but the scan folder wasn't created - see the log above for the reason.\r\n");
                SetStatus(L"Extraction didn't produce a scan - see log.");
            } else {
                if (wasStopped) { AppendView(L"\r\nStopped. Files extracted so far are kept; the tree shows the partial scan.\r\n"); SetStatus(L"Extraction stopped - partial scan kept."); }
                else { AppendView(L"\r\nDone. The tree now shows this scan.\r\n"); SetStatus(L"Extraction finished."); }
                SaveScanLog(dir);
                RefreshScans(); LoadTree(FALSE); SetWindowTextW(g_viewlbl, L"Extraction log");
            }
        }
        else { wchar_t m[96]; swprintf(m, 96, L"\r\nFinished with exit code %ld.\r\n", (long)(DWORD)wp); AppendView(m); SetStatus(L"Extraction failed - see log."); }
        return 0;
    }
    case WM_APP_RESULTS: {
        Batch *b = (Batch *)lp;
        if (b->gen == g_searchGen) {
            SendMessageW(g_results, WM_SETREDRAW, FALSE, 0);
            for (int i = 0; i < b->n; i++) {
                int idx = (int)SendMessageW(g_results, LB_ADDSTRING, 0, (LPARAM)b->labels[i]);
                Result *r = (Result *)xalloc(sizeof(Result));
                r->path = b->items[i].path; b->items[i].path = NULL; r->line = b->items[i].line;
                SendMessageW(g_results, LB_SETITEMDATA, idx, (LPARAM)r);
            }
            SendMessageW(g_results, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(g_results, NULL, TRUE);
            g_resultCount += b->n; g_resultFiles++;
            UpdateResultsLabel(FALSE);
        }
        FreeBatch(b);
        return 0;
    }
    case WM_APP_STOTAL:
        if ((LONG)lp == g_searchGen) { g_searchTotal = (int)wp; SendMessageW(g_sprog, PBM_SETPOS, 0, 0); }
        return 0;
    case WM_APP_SSTAT:
        if ((LONG)lp == g_searchGen) {
            wchar_t s[160];
            int done = (int)wp, tot = g_searchTotal;
            if (tot > 0) {
                SendMessageW(g_sprog, PBM_SETPOS, (WPARAM)((long long)done * 1000 / tot), 0);
                swprintf(s, 160, L"%ls %d of %d files (%d%%)", g_searching ? L"Searching..." : L"Searched", done, tot, (int)((long long)done * 100 / tot));
            } else swprintf(s, 160, L"%ls %d files", g_searching ? L"Searching..." : L"Searched", done);
            SetStatus(s);
        }
        return 0;
    case WM_APP_SDONE:
        if ((LONG)lp == g_searchGen && g_searching) {
            InterlockedExchange(&g_searching, 0);
            StopOrNext(FALSE);
            ShowSearchProgress(FALSE);
            UpdateResultsLabel(TRUE);
            SaveSearchCache();      // finished normally: keep the results for next time
        }
        return 0;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_LISTBOX && mi->CtlID == ID_RESULTS) { mi->itemHeight = S(26); return TRUE; }
        if (mi->CtlType == ODT_COMBOBOX) { mi->itemHeight = (mi->itemID == (UINT)-1) ? g_rowH - S(6) : S(24); return TRUE; }
        if (mi->CtlType == ODT_MENU && UiMenuMeasure(mi)) return TRUE;
        if (mi->CtlType != ODT_MENU) break;
        if (!g_boldFont) { LOGFONTW lf; GetObjectW(g_font, sizeof lf, &lf); lf.lfWeight = FW_BOLD; g_boldFont = CreateFontIndirectW(&lf); }
        HDC dc = GetDC(hwnd); HFONT of = (HFONT)SelectObject(dc, g_boldFont);
        SIZE sz; const wchar_t *t = (const wchar_t *)mi->itemData;
        GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
        SelectObject(dc, of); ReleaseDC(hwnd, dc);
        mi->itemWidth = sz.cx + S(16); mi->itemHeight = sz.cy + S(6);
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType == ODT_BUTTON) { UiDrawButton(di); return TRUE; }
        if (di->CtlType == ODT_LISTBOX && di->CtlID == ID_RESULTS) { UiDrawResult(di); return TRUE; }
        if (di->CtlType == ODT_COMBOBOX && di->CtlID == ID_SEARCH_EDIT && di->itemID != (UINT)-1 && !(di->itemState & ODS_COMBOBOXEDIT)) {
            UiSkinComboRow(di);                                  // the term as usual...
            wchar_t ex[PATHBUF + 64]; RecentExtra((int)di->itemID, ex, PATHBUF + 64);
            if (ex[0]) {                                          // ...then where / how it was searched, dimmer
                HFONT of = (HFONT)SelectObject(di->hDC, g_font);
                SIZE sz; GetTextExtentPoint32W(di->hDC, g_recent[di->itemID], (int)wcslen(g_recent[di->itemID]), &sz);
                RECT r = di->rcItem; r.left += S(10) + sz.cx + S(14); r.right -= S(6);
                SetBkMode(di->hDC, TRANSPARENT); SetTextColor(di->hDC, UI_DIM);
                DrawTextW(di->hDC, ex, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(di->hDC, of);
            }
            return TRUE;
        }
        if (di->CtlType == ODT_COMBOBOX && UiSkinComboRow(di)) return TRUE;
        if (di->CtlType != ODT_MENU) break;
        if (UiMenuDraw(di)) return TRUE;
        FillRect(di->hDC, &di->rcItem, g_brPanel2);
        HFONT of = (HFONT)SelectObject(di->hDC, g_boldFont ? g_boldFont : g_font);
        SetBkMode(di->hDC, TRANSPARENT); SetTextColor(di->hDC, UI_ACC2);
        RECT r = di->rcItem; r.left += S(10);
        DrawTextW(di->hDC, (const wchar_t *)di->itemData, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(di->hDC, of);
        return TRUE;
    }
    case WM_APP_TYPEIDX: {
        TypeIdx *x = (TypeIdx *)lp;
        g_idxBuilding = 0;
        if (x && !_wcsicmp(x->root, g_root)) {
            IdxFree(g_idx); g_idx = x; IdxSave(x); ComputeSelMask();
            wchar_t st[200]; swprintf(st, 200, L"File types found: %u files, %d types.", x->files, x->nt); SetStatus(st);
            if (g_filterOn) RefreshTreeForFilter();
        } else IdxFree(x);
        if (!g_idx && g_filterOn && g_root[0]) StartTypeIndex();
        if (g_typesDlg) PostMessageW(g_typesDlg, WM_APP_TYPEIDX, 0, 0);
        return 0;
    }
    case WM_APP_DELDONE:
        if (!g_running) {
            wchar_t st[160]; swprintf(st, 160, L"Old extraction removed (%llu files).", (unsigned long long)lp); SetStatus(st);
        }
        return 0;
    case WM_APP_TYPEPROG: {
        wchar_t st[200]; swprintf(st, 200, L"Finding the file types in this scan... %u files", (unsigned)wp); SetStatus(st);
        if (g_typesDlg) PostMessageW(g_typesDlg, WM_APP_TYPEPROG, wp, 0);
        return 0;
    }
    case WM_APP_RECENT: {
        BOOL picked = g_recentPicked;
        g_recentOpen = FALSE; g_recentPicked = FALSE;
        int i = (int)SendMessageW(g_sedit, CB_GETCURSEL, 0, 0);
        if (!picked || i == CB_ERR || i < 0 || i >= g_nrecent) return 0;
        wchar_t t[512]; swprintf(t, 512, L"%ls", g_recent[i]);
        SendMessageW(g_sedit, CB_SETCURSEL, (WPARAM)-1, 0);
        SetWindowTextW(g_sedit, t);
        g_ovrOn = TRUE; g_ovrMc = g_recentMc[i]; swprintf(g_ovrScope, PATHBUF, L"%ls", g_recentScope[i]);   // as it was searched
        if (g_root[0]) StartSearch(FALSE); else g_ovrOn = FALSE;
        return 0;
    }
    case WM_APP_ZIPPROG: {
        if (g_zipCancel) return 0;
        wchar_t st[96];
        if ((int)lp < 0) swprintf(st, 96, L"Making ZIP: finding the files... %d so far", (int)wp);
        else swprintf(st, 96, L"Making ZIP: %d of %d files...", (int)wp, (int)lp);
        SetStatus(st);
        return 0;
    }
    case WM_APP_ZIPDONE: {
        ZipJob *j = (ZipJob *)lp;
        InterlockedExchange(&g_zipping, 0);
        ShowZipStop(FALSE);
        if (j->cancelled) SetStatus(L"ZIP stopped - the unfinished ZIP file was deleted.");
        else if (j->ok) {
            wchar_t st[PATHBUF + 200], sk[64] = L"";
            if (j->skipped) swprintf(sk, 64, L" (%d couldn't be read and were skipped)", j->skipped);
            swprintf(st, PATHBUF + 200, L"ZIP saved: %d files, %.1f MB uncompressed%ls.", j->written, j->bytes / 1048576.0, sk);
            SetStatus(st);
            wchar_t m[PATHBUF * 2 + 200];
            swprintf(m, PATHBUF * 2 + 200, L"%ls\n\nSaved to:\n%ls\n\nShow it in Explorer?", st, j->out);
            if (j->note && j->note[0]) {                  // Bulk ZIP: what couldn't be found (the rest is in the ZIP)
                size_t nl = wcslen(j->note) + wcslen(m) + 64; wchar_t *mm = (wchar_t *)xalloc(nl * sizeof(wchar_t));
                if (mm) { swprintf(mm, nl, L"%ls\n\n%ls", j->note, m);
                          if (MessageBoxW(hwnd, mm, L"ns2dig - Bulk ZIP", MB_YESNO | (wcsstr(j->note, L"couldn't be found") ? MB_ICONWARNING : MB_ICONINFORMATION)) == IDYES) ShowInExplorer(j->out, FALSE);
                          xfree(mm); }
            } else
            if (MessageBoxW(hwnd, m, L"ns2dig", MB_YESNO | MB_ICONINFORMATION) == IDYES) ShowInExplorer(j->out, FALSE);
        } else {
            SetStatus(L"ZIP failed.");
            if (j->note && j->note[0]) { wchar_t m2[1200]; swprintf(m2, 1200, L"%ls\n\n%.900ls", j->err, j->note); MessageBoxW(hwnd, m2, L"ns2dig", MB_ICONWARNING); }
            else MessageBoxW(hwnd, j->err, L"ns2dig", MB_ICONWARNING);
        }
        FreeZipJob(j);
        return 0;
    }
    case WM_CLOSE:
        g_stage = "closing";
        if (g_zipping && MessageBoxW(hwnd, L"A ZIP file is still being made. Stop it and close?\n\nThe unfinished ZIP file will be deleted.", L"ns2dig",
                                     MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        if (g_running && MessageBoxW(hwnd, L"Extraction is still running. Stop it and close?\n\nFiles extracted so far are kept.", L"ns2dig",
                                     MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        if (g_running && g_job) TerminateJobObject(g_job, 3);
        InterlockedIncrement(&g_searchGen);
        if (g_zipping && g_zipThread) {          // let the ZIP thread stop cleanly (it checks between files)
            InterlockedExchange(&g_zipCancel, 1);
            SetStatus(L"Stopping the ZIP..."); UpdateWindow(hwnd);
            HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
            WaitForSingleObject(g_zipThread, 30000);
            SetCursor(old);
        }
        g_idxCancel = 1; g_delStop = 1;
        SaveSettings();
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY: ClearResults(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)cmd;
    g_mainTid = GetCurrentThreadId();
    SetUnhandledExceptionFilter(CrashFilter);
    CrashGuardThread();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);
    g_uiDarkMenus = UiInitDarkMode();
    g_brBg = CreateSolidBrush(UI_BG); g_brPanel = CreateSolidBrush(UI_PANEL); g_brPanel2 = CreateSolidBrush(UI_PANEL2); g_brStatus = CreateSolidBrush(UI_STATUSBG);
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = L"ns2dig";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"ns2dig",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 780,
                             NULL, NULL, inst, NULL);
    {
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int ww = S(1200), wh = S(780);
        if (ww > wa.right - wa.left) ww = wa.right - wa.left;
        if (wh > wa.bottom - wa.top) wh = wa.bottom - wa.top;
        SetWindowPos(w, NULL, wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2,
                     ww, wh, SWP_NOZORDER);
    }
    UiDarkTitle(w);
    UiAllowDarkWindow(w);
    g_stage = "startup: showing the window";
    ShowWindow(w, show); UpdateWindow(w);
    g_stage = "running";
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_F3) { FindNextInView(); continue; }
        if (m.message == WM_KEYDOWN && m.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) &&
            (m.hwnd == g_view || GetParent(m.hwnd) == g_sedit || m.hwnd == g_romfs || m.hwnd == g_gopath)) {
            SendMessageW(m.hwnd, EM_SETSEL, 0, -1); continue;
        }
        if (m.message == WM_KEYDOWN && m.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && m.hwnd == g_results) {
            SendMessageW(g_results, LB_SETSEL, TRUE, -1); OpenResult(); continue;
        }
        if (m.message == WM_KEYDOWN && m.wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) && !g_running) {
            SetFocus(g_sedit); SendMessageW(g_sedit, CB_SETEDITSEL, 0, MAKELPARAM(0, -1)); continue;
        }
        if (!IsDialogMessageW(w, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    g_stage = "closing: waiting for background work";
    // Every background thread has been told to stop (WM_CLOSE). Wait for them to actually finish before
    // exiting: if exit clean-up kills a thread while it holds a lock, the program can hang invisibly.
    InterlockedIncrement(&g_searchGen); g_idxCancel = 1; InterlockedExchange(&g_zipCancel, 1);
    for (int waited = 0; g_workers > 0 && waited < 15000; waited += 20) Sleep(20);
    CoUninitialize();
    return 0;
}
