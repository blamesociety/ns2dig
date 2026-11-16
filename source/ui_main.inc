/* ui_main.inc - the main window's look: owner-drawn buttons, tree/list icons, header with preview, status bar.
   Included by alsarc_gui.c just before the layout code, so every global and helper above is available. */

static HBRUSH g_brBg, g_brPanel, g_brPanel2, g_brStatus;
static HIMAGELIST g_iml;                 // file-type icons for the tree
static UiDib g_icDib[UI_IC_COUNT];       // the same icons for owner-drawn lists, header (small size)
static int g_icPx = 16;

// header above the file view: icon or texture preview, name, type tag
static int g_hdrIcon = UI_IC_FILE;
static wchar_t g_hdrName[512] = L"Welcome", g_hdrTag[200] = L"ns2dig";
static HBITMAP g_thumb; static HDC g_thumbDc; static HGDIOBJ g_thumbOld; static int g_thumbW, g_thumbH;
static wchar_t g_thumbNote[120];
static BOOL g_thumbText;
static int g_thumbKind = -1;               // the preview is a text card (no checkerboard behind it)
static volatile LONG g_thumbGen = 0;
static RECT g_hdrRect, g_prevRect;
#define WM_APP_THUMB (WM_APP + 30)
unsigned char *TexThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, int *count, int *srcW, int *srcH);
unsigned char *SndThumbnail(const wchar_t *dumpFile, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN);
unsigned char *VidThumbnail(const wchar_t *dumpFile, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN);
unsigned char *FontThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN);
unsigned char *LytThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN);
unsigned char *LgtThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN);
unsigned char *Vox3DThumbnail(const wchar_t *dumpFile, const wchar_t *kind, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN);
enum { TH_TEX, TH_SND, TH_VID, TH_FONT, TH_TEXT, TH_LYT, TH_LGT, TH_3D };

static void UiMakeIcons(void) {
    HIMAGELIST oldIml = g_iml; g_iml = NULL;          // keep the tree's current list until the new one is in
    for (int i = 0; i < UI_IC_COUNT; i++) UiDibFree(&g_icDib[i]);
    g_icPx = S(16);
    HDC dc = GetDC(g_wnd);
    g_iml = ImageList_Create(g_icPx, g_icPx, ILC_COLOR32, UI_IC_COUNT, 4);
    for (int i = 0; i < UI_IC_COUNT; i++) {
        UiDib d = { 0 };
        if (!UiDibMake(&d, g_icPx, g_icPx, dc)) continue;
        UiDibClear(&d, 0, 0);
        UiDrawIcon(&d.cv, i, 0, 0, (float)g_icPx);
        SelectObject(d.dc, d.old);                      // the image list needs the bitmap unselected
        ImageList_Add(g_iml, d.bmp, NULL);
        d.old = SelectObject(d.dc, d.bmp);
        g_icDib[i] = d;
    }
    ReleaseDC(g_wnd, dc);
    if (g_tree) TreeView_SetImageList(g_tree, g_iml, TVSIL_NORMAL);
    if (oldIml) ImageList_Destroy(oldIml);
}

// which icon a tree entry gets (name as shown in the tree: files without ".json")
static int UiIconForName(const wchar_t *name, BOOL isDir) {
    wchar_t t[TYPELEN + 8];
    if (isDir) {
        const wchar_t *dot = wcsrchr(name, L'.');
        if (!dot) return UI_IC_FOLDER;
        TypeOfName(name, t);
        if (!wcscmp(t, L"genvb")) return UI_IC_LIGHT;
        return UI_IC_ARCHIVE;
    }
    TypeOfName(name, t);
    static const struct { const wchar_t *ext; int ic; } m[] = {
        { L"bfres", UI_IC_MODEL }, { L"bphcl", UI_IC_MODEL }, { L"vgeob", UI_IC_MODEL }, { L"voxel terrain", UI_IC_TERRAIN },
        { L"bphsh", UI_IC_COLLISION }, { L"bphsc", UI_IC_COLLISION },
        { L"bntx", UI_IC_TEXTURE }, { L"xtx", UI_IC_TEXTURE }, { L"genvres", UI_IC_TEXTURE }, { L"raw", UI_IC_TEXTURE },
        { L"mate", UI_IC_TEXTURE }, { L"norm", UI_IC_TEXTURE }, { L"extm", UI_IC_TEXTURE },
        { L"hght", UI_IC_TERRAIN }, { L"tscb", UI_IC_TERRAIN },
        { L"bwav", UI_IC_SOUND }, { L"bars", UI_IC_SOUND }, { L"baatarc", UI_IC_SOUND }, { L"bagst", UI_IC_SOUND }, { L"dapg", UI_IC_SOUND },
        { L"webm", UI_IC_VIDEO },
        { L"msbt", UI_IC_TEXT }, { L"txt", UI_IC_TEXT }, { L"listing", UI_IC_TEXT }, { L"msbp", UI_IC_TEXT },
        { L"bfotf", UI_IC_FONT }, { L"bfttf", UI_IC_FONT }, { L"bffnt", UI_IC_FONT }, { L"bfcpx", UI_IC_FONT },
        { L"bflyt", UI_IC_LAYOUT }, { L"bflan", UI_IC_LAYOUT },
        { L"bkls", UI_IC_LIGHT }, { L"bgenv", UI_IC_LIGHT }, { L"genvb", UI_IC_LIGHT },
        { L"bfevfl", UI_IC_EVENT }, { L"ainb", UI_IC_EVENT }, { L"asb", UI_IC_EVENT },
        { L"bfsha", UI_IC_SHADER }, { L"bnsh", UI_IC_SHADER }, { L"sharcb", UI_IC_SHADER }, { L"sharc", UI_IC_SHADER }, { L"glsl", UI_IC_SHADER },
        { L"gsh", UI_IC_SHADER }, { L"pck", UI_IC_SHADER }, { L"bushvt", UI_IC_SHADER },
        { L"byml", UI_IC_DATA }, { L"byaml", UI_IC_DATA }, { L"changelist", UI_IC_TEXT }, { L"bgyml", UI_IC_DATA }, { L"aamp", UI_IC_DATA }, { L"bslnk", UI_IC_DATA }, { L"belnk", UI_IC_DATA },
        { L"rsizetable", UI_IC_DATA }, { L"npdm", UI_IC_DATA },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) if (!wcscmp(m[i].ext, t)) return m[i].ic;
    if (!wcsncmp(t, L"bagl", 4) || !wcsncmp(t, L"dlp", 3) || !wcscmp(t, L"bofx") || !wcscmp(t, L"bgsdw") || wcsstr(t, L"projgfxbase")) return UI_IC_LIGHT;
    return UI_IC_FILE;
}
static int UiIconForPath(const wchar_t *path, BOOL isDir) {
    const wchar_t *leaf = wcsrchr(path, L'\\'); leaf = leaf ? leaf + 1 : path;
    wchar_t n[MAX_PATH * 2]; swprintf(n, MAX_PATH * 2, L"%ls", leaf);
    size_t l = wcslen(n); if (!isDir && l > 5 && !_wcsicmp(n + l - 5, L".json")) n[l - 5] = 0;
    return UiIconForName(n, isDir);
}

/* ------------------------------------------------------------------ owner-drawn buttons and checkboxes */
typedef struct { WNDPROC old; int hot; int check; int isCheck; int glyph; } UiBtn;
static int UiGlyphFor(int id) {
    switch (id) {
    case ID_EXTRACT: return UI_GL_DOWNLOAD;  case ID_LOAD: return UI_GL_FOLDER;   case ID_SCANLOG: return UI_GL_LOG;
    case ID_ROMFS_BTN: return UI_GL_BROWSE;  case ID_OUT_BTN: return UI_GL_FOLDER; case ID_SEARCH: return UI_GL_SEARCH;
    case ID_STOP: return UI_GL_STOP;         case ID_NEXT: return UI_GL_NEXT;     case ID_COPYRES: return UI_GL_COPY;
    case ID_GOBTN: return UI_GL_ARROW;       case ID_TYPES: return UI_GL_FILTER;  case ID_ZIPSTOP: return UI_GL_STOP;
    case ID_OPENDIR: case ID_OPENROM: return UI_GL_FOLDER;    case ID_VOX3D: return UI_GL_CUBE;    case ID_PLAYSND: return UI_GL_PLAY;
    case ID_VIEWTEX: return UI_GL_IMAGE;     case ID_VIEWLYT: return UI_GL_LAYOUT; case ID_VIEWFNT: return UI_GL_FONT;
    case ID_VIEWLGT: return UI_GL_SUN;       case ID_VIEWEVT: return UI_GL_FLOW;
    case ID_BOOKMARKS: case ID_BMSTAR: return UI_GL_STAR;
    }
    return UI_GL_NONE;
}
// buttons drawn in the accent colour: the main action, and whichever viewers fit the selected file
static BOOL UiIsPrimary(int id) {
    switch (id) {
    case ID_EXTRACT: case ID_SEARCH: return TRUE;
    case ID_VOX3D: case ID_PLAYSND: case ID_VIEWTEX: case ID_VIEWLYT: case ID_VIEWFNT: case ID_VIEWLGT: case ID_VIEWEVT: return TRUE;   // only while enabled
    case ID_BMSTAR: return g_bmOn;                                       // bookmarked: a filled, coloured star
    }
    return FALSE;
}
static LRESULT CALLBACK UiBtnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    UiBtn *b = (UiBtn *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!b) return DefWindowProcW(h, msg, wp, lp);
    WNDPROC old = b->old;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!b->hot) { b->hot = 1; InvalidateRect(h, NULL, FALSE); TRACKMOUSEEVENT tm = { sizeof tm, TME_LEAVE, h, 0 }; TrackMouseEvent(&tm); }
        break;
    case WM_MOUSELEAVE: b->hot = 0; InvalidateRect(h, NULL, FALSE); break;
    case BM_GETCHECK: if (b->isCheck) return b->check ? BST_CHECKED : BST_UNCHECKED; break;
    case BM_SETCHECK: if (b->isCheck) { b->check = wp == BST_CHECKED; InvalidateRect(h, NULL, FALSE); return 0; } break;
    case WM_LBUTTONUP:
        if (b->isCheck && GetCapture() == h) {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) }; RECT rc; GetClientRect(h, &rc);
            if (PtInRect(&rc, pt)) { b->check = !b->check; InvalidateRect(h, NULL, FALSE); }
        }
        break;
    case WM_KEYUP:
        if (b->isCheck && wp == VK_SPACE && (SendMessageW(h, BM_GETSTATE, 0, 0) & BST_PUSHED)) { b->check = !b->check; InvalidateRect(h, NULL, FALSE); }
        break;
    case WM_ENABLE: InvalidateRect(h, NULL, FALSE); break;
    case WM_LBUTTONDBLCLK: msg = WM_LBUTTONDOWN; break;      // a quick second click is another click
    case WM_ERASEBKGND: return 1;
    case WM_NCDESTROY: SetWindowLongPtrW(h, GWLP_USERDATA, 0); free(b); break;
    }
    return CallWindowProcW(old, h, msg, wp, lp);
}
// turn a BUTTON made by MakeCtl into an owner-drawn one (push buttons and checkboxes)
static DWORD UiButtonStyle(DWORD style, int *isCheck) {
    DWORD type = style & BS_TYPEMASK;
    *isCheck = (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX);
    if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || *isCheck) style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    return style;
}
static void UiAttachButton(HWND h, int isCheck, int id) {
    UiBtn *b = (UiBtn *)calloc(1, sizeof *b); if (!b) return;
    b->isCheck = isCheck; b->glyph = isCheck ? UI_GL_NONE : UiGlyphFor(id);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)b);
    b->old = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)UiBtnProc);
}
static void UiDrawButton(DRAWITEMSTRUCT *di) {
    UiBtn *b = (UiBtn *)GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA);
    RECT rc = di->rcItem; int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    BOOL dis = (di->itemState & ODS_DISABLED) != 0, down = (di->itemState & ODS_SELECTED) != 0, focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);
    BOOL hot = b && b->hot;
    int id = (int)GetDlgCtrlID(di->hwndItem);
    wchar_t text[200]; GetWindowTextW(di->hwndItem, text, 200);
    UiDib d = { 0 };
    if (!UiDibMake(&d, w, h, di->hDC)) return;
    UiDibClear(&d, UI_BG, 1);
    HFONT of = (HFONT)SelectObject(d.dc, g_font);
    SetBkMode(d.dc, TRANSPARENT);
    float r = (float)S(6);
    if (b && b->isCheck) {
        int bs = S(16), by = (h - bs) / 2;
        COLORREF box = b->check ? (hot ? UI_ACC_HOT : UI_ACC) : (hot ? UI_HOVER : UI_PANEL2);
        UiRRect(&d.cv, 0.5f, by + 0.5f, bs - 0.5f, by + bs - 0.5f, (float)S(4), b->check ? box : (hot ? UI_LINE2 : UI_LINE2), dis ? 120 : 255);
        if (!b->check) UiRRect(&d.cv, 1.5f, by + 1.5f, bs - 1.5f, by + bs - 1.5f, (float)S(3), box, 255);
        else {
            float k = bs / 16.0f;
            UiLine(&d.cv, 3.8f * k, by + 8.3f * k, 6.8f * k, by + 11.2f * k, 1.9f * k, UI_ACC_INK, 255);
            UiLine(&d.cv, 6.8f * k, by + 11.2f * k, 12.3f * k, by + 5.0f * k, 1.9f * k, UI_ACC_INK, 255);
        }
        RECT tr = { bs + S(8), 0, w, h };
        SetTextColor(d.dc, dis ? UI_DIM : UI_INK);
        DrawTextW(d.dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (focus) { RECT fr = { bs + S(5), S(2), w - 1, h - S(2) }; UiDibOpaque(&d); DrawFocusRect(d.dc, &fr); }
    } else {
        BOOL prim = UiIsPrimary(id) && !dis;
        COLORREF bg = prim ? (down ? UI_ACC : hot ? UI_ACC_HOT : UI_ACC) : (down ? UI_PANEL2 : hot ? UI_HOVER : UI_PANEL2);
        COLORREF border = prim ? bg : (hot ? UI_LINE2 : UI_LINE);
        COLORREF fg = dis ? UI_DIM : prim ? UI_ACC_INK : UI_INK;
        UiRRect(&d.cv, 0.5f, 0.5f, w - 0.5f, h - 0.5f, r, border, 255);
        UiRRect(&d.cv, 1.5f, 1.5f, w - 1.5f, h - 1.5f, r - 1, bg, 255);
        if (focus && !prim) UiRRect(&d.cv, 0.5f, 0.5f, w - 0.5f, h - 0.5f, r, UI_ACC, 90);
        SIZE ts = { 0 }; GetTextExtentPoint32W(d.dc, text, (int)wcslen(text), &ts);
        int glyph = b ? b->glyph : 0; if (id == ID_BMSTAR && g_bmOn) glyph = UI_GL_STARF;
        int gs = glyph ? S(16) : 0, gap = gs && text[0] ? S(7) : 0;
        int total = gs + gap + ts.cx, x = (w - total) / 2; if (x < S(8) && text[0]) x = S(8);
        if (gs) UiDrawGlyph(&d.cv, glyph, (float)x, (h - gs) / 2.0f, (float)gs, fg);
        UiDibOpaque(&d);
        RECT tr = { x + gs + gap, 0, w - S(6), h };
        SetTextColor(d.dc, fg);
        DrawTextW(d.dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(d.dc, of);
    BitBlt(di->hDC, rc.left, rc.top, w, h, d.dc, 0, 0, SRCCOPY);
    UiDibFree(&d);
}

/* ------------------------------------------------------------------ progress bars: a rounded track and fill */
static LRESULT CALLBACK UiProgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc); int w = rc.right, hh = rc.bottom;
        UiDib d = { 0 };
        if (w > 0 && hh > 0 && UiDibMake(&d, w, hh, dc)) {
            UiDibClear(&d, UI_STATUSBG, 1);
            int lo = (int)SendMessageW(h, PBM_GETRANGE, TRUE, 0), hi = (int)SendMessageW(h, PBM_GETRANGE, FALSE, 0);
            int pos = (int)SendMessageW(h, PBM_GETPOS, 0, 0);
            float th = (float)hh, y0 = 0;
            UiRRect(&d.cv, 0, y0, (float)w, y0 + th, th / 2, UI_PANEL2, 255);
            if (hi > lo && pos > lo) {
                float fw = (float)w * (pos - lo) / (hi - lo); if (fw > w) fw = (float)w; if (fw < th) fw = th;
                UiRRect(&d.cv, 0, y0, fw, y0 + th, th / 2, UI_ACC, 255);
            }
            BitBlt(dc, 0, 0, w, hh, d.dc, 0, 0, SRCCOPY);
            UiDibFree(&d);
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == PBM_SETPOS || msg == PBM_DELTAPOS || msg == PBM_STEPIT || msg == PBM_SETRANGE32) {
        LRESULT r = DefSubclassProc(h, msg, wp, lp); InvalidateRect(h, NULL, FALSE); return r;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ dark controls */
static LRESULT CALLBACK UiTreeScrollProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
static void UiStyleControls(void) {
    UiDarkControl(g_tree, L"DarkMode_Explorer");
    SetWindowSubclass(g_tree, UiTreeScrollProc, 9, 0);
    TreeView_SetBkColor(g_tree, UI_PANEL);
    TreeView_SetTextColor(g_tree, UI_INK);
    TreeView_SetLineColor(g_tree, UI_LINE2);
    SendMessageW(g_tree, TVM_SETEXTENDEDSTYLE, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    TreeView_SetItemHeight(g_tree, S(26));
    UiDarkControl(g_results, L"DarkMode_Explorer");
    UiDarkControl(g_view, L"DarkMode_Explorer");
    UiDarkControl(g_out, L"DarkMode_CFD");
    UiDarkControl(g_sedit, L"DarkMode_CFD");
    SetWindowSubclass(g_progress, UiProgProc, 1, 0);
    SetWindowSubclass(g_sprog, UiProgProc, 1, 0);
}
static LRESULT UiCtlColor(UINT msg, HDC dc, HWND ctl) {
    if (ctl == g_status) { SetTextColor(dc, UI_MUT); SetBkColor(dc, UI_STATUSBG); return (LRESULT)g_brStatus; }
    if (msg == WM_CTLCOLORSTATIC && ctl == g_view) { SetTextColor(dc, UI_INK); SetBkColor(dc, UI_PANEL); return (LRESULT)g_brPanel; }
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) { SetTextColor(dc, UI_INK); SetBkColor(dc, UI_PANEL); return (LRESULT)g_brPanel; }
    if (ctl == g_viewlbl) { SetTextColor(dc, UI_MUT); SetBkColor(dc, UI_BG); return (LRESULT)g_brBg; }
    BOOL muted = ctl == GetDlgItem(g_wnd, ID_SCRIPT) || ctl == g_reslbl;
    SetTextColor(dc, muted ? UI_MUT : UI_INK); SetBkColor(dc, UI_BG);
    return (LRESULT)g_brBg;
}

/* ------------------------------------------------------------------ tree: icons and the selected row */
// the tree slides its picture sideways when scrolled left/right and only redraws the new strip, which leaves
// copies of the selected row's coral bar behind: redraw all of it after any sideways scroll
static LRESULT CALLBACK UiTreeScrollProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_HSCROLL || msg == WM_MOUSEHWHEEL || (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT)) ||
        (msg == WM_KEYDOWN && (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_HOME || wp == VK_END))) {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        InvalidateRect(h, NULL, FALSE);
        return r;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, UiTreeScrollProc, 9);
    return DefSubclassProc(h, msg, wp, lp);
}
static LRESULT UiTreeCustomDraw(NMTVCUSTOMDRAW *cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        if (cd->nmcd.uItemState & CDIS_SELECTED) { cd->clrText = UI_INK; return CDRF_NOTIFYPOSTPAINT; }
        cd->clrText = UI_INK;
        return CDRF_DODEFAULT;
    case CDDS_ITEMPOSTPAINT: {
        HTREEITEM it = (HTREEITEM)cd->nmcd.dwItemSpec;
        RECT tr; if (!TreeView_GetItemRect(g_tree, it, &tr, TRUE)) return CDRF_DODEFAULT;
        RECT row = cd->nmcd.rc;
        int w = row.right - row.left, h = row.bottom - row.top;
        if (w <= 0 || h <= 0) return CDRF_DODEFAULT;
        TVITEMW tv = { 0 }; wchar_t buf[MAX_PATH * 2];
        tv.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_CHILDREN | TVIF_STATE; tv.stateMask = TVIS_EXPANDED;
        tv.hItem = it; tv.pszText = buf; tv.cchTextMax = MAX_PATH * 2;
        TreeView_GetItem(g_tree, &tv);
        HDC dc = cd->nmcd.hdc;
        UiDib d = { 0 };
        if (!UiDibMake(&d, w, h, dc)) return CDRF_DODEFAULT;
        UiDibClear(&d, UI_SEL, 1);                                    // the whole row, from the left edge
        UiRRect(&d.cv, 0, 0, (float)S(3), (float)h, 0, UI_ACC, 255);   // coral bar at the far left
        int iconX = tr.left - row.left - g_icPx - S(3), iconY = (h - g_icPx) / 2;
        if (tv.cChildren) {                                          // redraw the expand/collapse chevron
            int indent = (int)SendMessageW(g_tree, TVM_GETINDENT, 0, 0);
            float cx = (float)(iconX - indent / 2), cy = h / 2.0f, a = (float)S(4), lw = (float)S(1) * 1.4f;
            if (tv.state & TVIS_EXPANDED) { UiLine(&d.cv, cx - a, cy - a / 2, cx, cy + a / 2, lw, UI_INK, 255); UiLine(&d.cv, cx, cy + a / 2, cx + a, cy - a / 2, lw, UI_INK, 255); }
            else { UiLine(&d.cv, cx - a / 2, cy - a, cx + a / 2, cy, lw, UI_INK, 255); UiLine(&d.cv, cx + a / 2, cy, cx - a / 2, cy + a, lw, UI_INK, 255); }
        }
        if (tv.iImage >= 0 && tv.iImage < UI_IC_COUNT) UiDrawIcon(&d.cv, tv.iImage, (float)iconX, (float)iconY, (float)g_icPx);
        UiDibOpaque(&d);
        HFONT of = (HFONT)SelectObject(d.dc, g_font);
        SetBkMode(d.dc, TRANSPARENT); SetTextColor(d.dc, UI_INK);
        RECT txt = { tr.left - row.left + S(2), 0, w - S(4), h };
        DrawTextW(d.dc, buf, -1, &txt, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(d.dc, of);
        BitBlt(dc, row.left, row.top, w, h, d.dc, 0, 0, SRCCOPY);
        UiDibFree(&d);
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

/* ------------------------------------------------------------------ search results list (owner-drawn) */
static void UiDrawResult(DRAWITEMSTRUCT *di) {
    if (di->itemID == (UINT)-1) return;
    HDC dc = di->hDC; RECT rc = di->rcItem;
    BOOL sel = (di->itemState & ODS_SELECTED) != 0;
    HBRUSH br = CreateSolidBrush(sel ? UI_SEL : UI_PANEL); FillRect(dc, &rc, br); DeleteObject(br);
    if (sel) { RECT bar = { rc.left, rc.top, rc.left + S(3), rc.bottom }; HBRUSH ab = CreateSolidBrush(UI_ACC); FillRect(dc, &bar, ab); DeleteObject(ab); }
    int len = (int)SendMessageW(di->hwndItem, LB_GETTEXTLEN, di->itemID, 0);
    wchar_t sbuf[1024]; wchar_t *t = len < 1024 ? sbuf : (wchar_t *)xalloc((len + 1) * sizeof(wchar_t));
    if (!t) return;
    SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)t);
    Result *r = (Result *)di->itemData;
    int ic = (r && r != (Result *)LB_ERR && r->path) ? UiIconForPath(r->path, FALSE) : UI_IC_FILE;
    int ix = rc.left + S(8), iy = rc.top + (rc.bottom - rc.top - g_icPx) / 2;
    if (g_icDib[ic].dc) UiDibBlend(dc, ix, iy, &g_icDib[ic]);
    HFONT of = (HFONT)SelectObject(dc, g_font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, UI_INK);
    RECT tr = { ix + g_icPx + S(6), rc.top, rc.right + 100000, rc.bottom };
    DrawTextW(dc, t, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_EXPANDTABS);
    SelectObject(dc, of);
    if (t != sbuf) xfree(t);
    if (di->itemState & ODS_FOCUS) { RECT fr = rc; InflateRect(&fr, -1, -1); SetTextColor(dc, UI_INK); DrawFocusRect(dc, &fr); }
}

/* ------------------------------------------------------------------ header above the file view */
static void UiThumbFree(void) {
    if (g_thumbDc) { SelectObject(g_thumbDc, g_thumbOld); DeleteDC(g_thumbDc); g_thumbDc = NULL; }
    if (g_thumb) { DeleteObject(g_thumb); g_thumb = NULL; }
    g_thumbNote[0] = 0; g_thumbText = FALSE; g_thumbKind = -1;
}
typedef struct { LONG gen; int kind; wchar_t geom[16]; wchar_t dump[PATHBUF]; char inner[PATHBUF]; int boxW, boxH; unsigned char *px; int w, h, count, sw, sh; wchar_t note[120]; } ThumbJob;
static DWORD WINAPI UiThumbWorker(LPVOID p) {
    ThumbJob *j = (ThumbJob *)p;
    InterlockedIncrement(&g_workers);
    if (j->gen == g_thumbGen) switch (j->kind) {
    case TH_TEX:  j->px = TexThumbnail(j->dump, j->inner[0] ? j->inner : NULL, j->boxW, j->boxH, &j->w, &j->h, &j->count, &j->sw, &j->sh); break;
    case TH_SND:  j->px = SndThumbnail(j->dump, j->boxW, j->boxH, &j->w, &j->h, j->note, 120); break;
    case TH_VID:  j->px = VidThumbnail(j->dump, j->boxW, j->boxH, &j->w, &j->h, j->note, 120); break;
    case TH_FONT: j->px = FontThumbnail(j->dump, j->inner, j->boxW, j->boxH, &j->w, &j->h, j->note, 120); break;
    case TH_LYT:  j->px = LytThumbnail(j->dump, j->inner[0] ? j->inner : NULL, j->boxW, j->boxH, &j->w, &j->h, j->note, 120); break;
    case TH_LGT:  j->px = LgtThumbnail(j->dump, j->inner, j->boxW, j->boxH, &j->w, &j->h, j->note, 120); break;
    case TH_3D:   j->px = Vox3DThumbnail(j->dump, j->geom, j->boxW, j->boxH, &j->w, &j->h, j->note, 120); break;
    }
    if (!PostMessageW(g_wnd, WM_APP_THUMB, 0, (LPARAM)j)) { free(j->px); free(j); }
    InterlockedDecrement(&g_workers);
    return 0;
}
// quietly find the dump file (and path inside its archive) for an output file; no questions asked
static BOOL UiDumpQuiet(const wchar_t *outFile, wchar_t *dump, char *inner, size_t innerN) {
    BOOL known; inner[0] = 0;
    int k = DumpOrigNoAsk(outFile, FALSE, dump, &known);
    if (k != 1 && k != 3) return FALSE;
    if (k == 3) {
        wchar_t src[PATHBUF], scan[PATHBUF]; ScanNameOf(g_root, scan, PATHBUF); if (!ScanSource(scan, src)) return FALSE;
        size_t sl = wcslen(src); while (sl > 3 && src[sl - 1] == L'\\') src[--sl] = 0;
        const wchar_t *archRel = dump + sl + 1, *rel = outFile + wcslen(g_root) + 1;
        size_t al = wcslen(archRel);
        if (wcslen(rel) <= al + 1 || _wcsnicmp(rel, archRel, al)) return FALSE;
        wchar_t in[PATHBUF]; swprintf(in, PATHBUF, L"%ls", rel + al + 1);
        size_t l = wcslen(in); if (l > 5 && !_wcsicmp(in + l - 5, L".json")) in[l - 5] = 0;
        for (wchar_t *q = in; *q; q++) if (*q == L'\\') *q = L'/';
        WideCharToMultiByte(CP_UTF8, 0, in, -1, inner, (int)innerN, NULL, NULL);
    }
    return TRUE;
}
static void UiInvalidateHeader(void) { if (g_hdrRect.right > g_hdrRect.left) InvalidateRect(g_wnd, &g_hdrRect, FALSE); }

// message files: the first message, as a small card of text (made from the JSON straight away)
static void UiTextCard(const wchar_t *path) {
    wchar_t lp[PATHBUF + 8]; LongPath(path, lp, PATHBUF + 8);
    HANDLE h = CreateFileW(lp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[65536]; DWORD got = 0; ReadFile(h, buf, sizeof buf - 1, &got, NULL); CloseHandle(h); buf[got] = 0;
    // first  "label": "text"  whose label doesn't start with _
    char *p = buf, msg[2048]; msg[0] = 0; int count = 0;
    while ((p = strchr(p, '"')) != NULL) {
        char *ke = strchr(p + 1, '"'); if (!ke) break;
        char *c = ke + 1; while (*c == ' ') c++;
        if (*c != ':') { p = ke + 1; continue; }
        c++; while (*c == ' ') c++;
        if (*c == '"' && p[1] != '_') {
            count++;
            if (!msg[0]) {
                int o = 0; c++;
                while (*c && *c != '"' && o < (int)sizeof msg - 2) {
                    if (*c == '\\' && c[1]) { c++; if (*c == 'n') msg[o++] = '\n'; else if (*c == 't') msg[o++] = ' '; else if (*c == 'u') { c += 4; msg[o++] = '?'; } else msg[o++] = *c; }
                    else msg[o++] = *c;
                    c++;
                }
                msg[o] = 0;
            }
        }
        p = ke + 1;
    }
    if (!msg[0]) return;
    int bw = g_prevRect.right - g_prevRect.left - S(16), bh = g_prevRect.bottom - g_prevRect.top - S(30);
    if (bw < 8 || bh < 8) return;
    wchar_t wmsg[2048]; MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 2048);
    HDC sdc = GetDC(g_wnd);
    UiDib d = { 0 };
    if (UiDibMake(&d, bw, bh, sdc)) {
        UiDibClear(&d, 0, 0);
        HFONT of = (HFONT)SelectObject(d.dc, g_smallFont ? g_smallFont : g_font);
        SetBkMode(d.dc, TRANSPARENT); SetTextColor(d.dc, RGB(255, 255, 255));
        RECT r = { 0, 0, bw, bh };
        DrawTextW(d.dc, wmsg, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS | DT_EDITCONTROL);
        SelectObject(d.dc, of);
        GdiFlush();
        for (int i = 0; i < bw * bh; i++) {          // white text on transparent: brightness becomes coverage, tinted ink
            unsigned a = d.cv.px[i] & 255;
            d.cv.px[i] = a ? ((a << 24) | ((GetRValue(UI_INK) * a / 255) << 16) | ((GetGValue(UI_INK) * a / 255) << 8) | (GetBValue(UI_INK) * a / 255)) : 0;
        }
        g_thumbDc = d.dc; g_thumb = d.bmp; g_thumbOld = d.old; g_thumbW = bw; g_thumbH = bh;   // the header owns the bitmap now
        g_thumbText = TRUE; g_thumbKind = TH_TEXT;
        swprintf(g_thumbNote, 120, L"%d message%ls", count, count == 1 ? L"" : L"s");
    }
    ReleaseDC(g_wnd, sdc);
}

// what the header shows. path: an output file (.json) or folder; NULL with a name/tag for other states
static void HdrSet(const wchar_t *path, BOOL isDir, const wchar_t *name, const wchar_t *tag) {
    InterlockedIncrement(&g_thumbGen);
    UiThumbFree();
    if (!path) {
        swprintf(g_hdrName, 512, L"%ls", name ? name : L""); swprintf(g_hdrTag, 200, L"%ls", tag ? tag : L"");
        g_hdrIcon = UI_IC_FOLDER;
        UiInvalidateHeader();
        return;
    }
    const wchar_t *leaf = wcsrchr(path, L'\\'); leaf = leaf ? leaf + 1 : path;
    swprintf(g_hdrName, 512, L"%ls", leaf);
    size_t l = wcslen(g_hdrName); if (!isDir && l > 5 && !_wcsicmp(g_hdrName + l - 5, L".json")) g_hdrName[l - 5] = 0;
    g_hdrIcon = UiIconForName(g_hdrName, isDir);
    if (!_wcsicmp(path, g_root)) { const wchar_t *bs = wcsrchr(g_root, L'\\'); swprintf(g_hdrName, 512, L"%ls", bs ? bs + 1 : g_root); g_hdrIcon = UI_IC_FOLDER; swprintf(g_hdrTag, 200, L"Scan"); }
    else if (isDir) swprintf(g_hdrTag, 200, L"%ls", g_hdrIcon == UI_IC_FOLDER ? L"Folder" : g_hdrIcon == UI_IC_LIGHT ? L"Lighting archive" : L"Archive (opened as a folder)");
    else {
        wchar_t t[TYPELEN + 8]; TypeOfName(g_hdrName, t);
        const wchar_t *d = TypeDesc(t);
        if (d && d[0]) {
            swprintf(g_hdrTag, 200, L"%ls", d);
            wchar_t *dash = wcsstr(g_hdrTag, L" - "); if (dash) *dash = 0;   // "Sound - Play" -> "Sound"
        } else swprintf(g_hdrTag, 200, L"%ls file", t);
    }
    // a preview, made in the background: textures, sounds (waveform), videos (middle frame), fonts (a sample);
    // text files get a card with their first message straight away
    int kind = -1; const wchar_t *geom = L"";
    if (!isDir) {
        if (IsTerrainHeightOut(path, FALSE)) { kind = TH_3D; geom = L"terrain"; }       // heights: shaded 3D rather than grey
        else if (IsTerrainSceneOut(path, FALSE)) { kind = TH_3D; geom = L"terrainworld"; }   // the whole terrain, stitched
        else if (IsBfresModelOut(path, FALSE)) { kind = TH_3D; geom = L"bfres"; }
        else if (IsPhiveOut(path, FALSE)) { kind = TH_3D; geom = L"phive"; }
        else if (IsClothOut(path, FALSE)) { kind = TH_3D; geom = L"cloth"; }
        else if (IsTexOut(path, FALSE)) kind = TH_TEX;
        else if (EndsWithI(path, L".webm.json")) kind = TH_VID;
        else if (IsSoundOut(path)) kind = TH_SND;
        else if (IsFontOut(path, FALSE)) kind = TH_FONT;
        else if (EndsWithI(path, L".msbt.json")) { UiTextCard(path); UiInvalidateHeader(); return; }
        else if (IsLytOut(path, FALSE)) kind = TH_LYT;
        else if (IsLgtOut(path, FALSE)) kind = TH_LGT;
    } else if (IsLgtOut(path, TRUE)) kind = TH_LGT;
    if (kind >= 0 && g_prevRect.right > g_prevRect.left) {
        ThumbJob *j = (ThumbJob *)calloc(1, sizeof *j);
        BOOL ok = FALSE;
        if (j && isDir) { BOOL known; ok = DumpOrigNoAsk(path, TRUE, j->dump, &known) == 1; }   // a lighting archive opened as a folder
        else if (j) ok = UiDumpQuiet(path, j->dump, j->inner, sizeof j->inner);
        if (ok && kind == TH_FONT && !j->inner[0]) {        // a loose font file: its folder, and which font in it (as the viewer does)
            wchar_t *sl = wcsrchr(j->dump, L'\\');
            if (!sl) ok = FALSE;
            else {
                char nm[300]; snprintf(nm, sizeof nm, "%ls", sl + 1); *sl = 0;
                size_t nl = strlen(nm); if (nl > 3 && !_stricmp(nm + nl - 3, ".zs")) nm[nl -= 3] = 0;
                if (strstr(nm, ".bfcpx")) { char *dt = strchr(nm, '.'); if (dt) *dt = 0; snprintf(j->inner, sizeof j->inner, "fcpx/%s.bfcpx", nm); }
                else if (strstr(nm, ".bffnt")) snprintf(j->inner, sizeof j->inner, "font/%s", nm);
                else snprintf(j->inner, sizeof j->inner, "scft/%s", nm);
            }
        }
        if (ok && (kind == TH_SND || kind == TH_VID || kind == TH_3D) && j->inner[0]) ok = FALSE;     // only loose files for these
        if (ok) swprintf(j->geom, 16, L"%ls", geom);
        if (ok) {
            j->gen = g_thumbGen; j->kind = kind;
            j->boxW = g_prevRect.right - g_prevRect.left - S(12);
            j->boxH = g_prevRect.bottom - g_prevRect.top - (kind == TH_TEX ? S(8) : S(28));
            swprintf(g_thumbNote, 120, L"loading preview...");
            HANDLE th = CreateThread(NULL, 0, UiThumbWorker, j, 0, NULL);
            if (th) CloseHandle(th); else { free(j); g_thumbNote[0] = 0; }
        } else free(j);
    }
    UiInvalidateHeader();
}
static void UiThumbArrived(ThumbJob *j) {
    if (j->gen == g_thumbGen) {
        UiThumbFree();
        if (j->px) {
            HDC dc = GetDC(g_wnd);
            BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
            bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = j->w; bi.bmiHeader.biHeight = -j->h;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
            void *bits = NULL;
            g_thumb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
            if (g_thumb) {
                memcpy(bits, j->px, (size_t)j->w * j->h * 4);
                g_thumbDc = CreateCompatibleDC(dc); g_thumbOld = SelectObject(g_thumbDc, g_thumb);
                g_thumbW = j->w; g_thumbH = j->h; g_thumbKind = j->kind;
                if (j->kind != TH_TEX) swprintf(g_thumbNote, 120, L"%ls", j->note);
                else if (j->count > 1) swprintf(g_thumbNote, 120, L"%d x %d - 1 of %d", j->sw, j->sh, j->count);
                else swprintf(g_thumbNote, 120, L"%d x %d", j->sw, j->sh);
            }
            ReleaseDC(g_wnd, dc);
        } else swprintf(g_thumbNote, 120, L"no preview");
        UiInvalidateHeader();
    }
    free(j->px); free(j);
}
static void UiPaintHeader(HDC dc) {
    RECT hr = g_hdrRect; if (hr.right <= hr.left) return;
    int w = hr.right - hr.left, h = hr.bottom - hr.top;
    UiDib d = { 0 };
    if (!UiDibMake(&d, w, h, dc)) return;
    UiDibClear(&d, UI_BG, 1);
    RECT pv = g_prevRect; OffsetRect(&pv, -hr.left, -hr.top);
    float r = (float)S(9);
    UiRRect(&d.cv, pv.left + 0.5f, pv.top + 0.5f, pv.right - 0.5f, pv.bottom - 0.5f, r, UI_LINE, 255);
    UiRRect(&d.cv, pv.left + 1.5f, pv.top + 1.5f, pv.right - 1.5f, pv.bottom - 1.5f, r - 1, UI_PANEL, 255);
    int pw = pv.right - pv.left, ph = pv.bottom - pv.top;
    if (!g_thumb) {                              // big file-type icon in the middle of the box
        float isz = (float)(ph * 5 / 10);
        UiDrawIcon(&d.cv, g_hdrIcon, pv.left + (pw - isz) / 2, pv.top + (ph - isz) / 2 - (g_thumbNote[0] ? S(6) : 0), isz);
    } else {                                     // checkerboard behind textures so transparency shows
        int tx = pv.left + (pw - g_thumbW) / 2, ty = pv.top + (ph - g_thumbH) / 2 - S(6);
        if (g_thumbText) { tx = pv.left + S(8); ty = pv.top + S(6); }
        if (g_thumbKind == TH_TEX && !g_thumbText) for (int y = 0; y < g_thumbH; y++) for (int x = 0; x < g_thumbW; x++) {
            int c = (((x / S(6)) ^ (y / S(6))) & 1) ? 0x3A : 0x2C;
            if (ty + y >= 0 && ty + y < h && tx + x >= 0 && tx + x < w) d.cv.px[(size_t)(ty + y) * w + tx + x] = 0xFF000000u | (c << 16) | (c << 8) | c;
        }
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(d.dc, tx, ty, g_thumbW, g_thumbH, g_thumbDc, 0, 0, g_thumbW, g_thumbH, bf);
    }
    UiDibOpaque(&d);
    SetBkMode(d.dc, TRANSPARENT);
    if (g_thumbNote[0]) {
        HFONT of = (HFONT)SelectObject(d.dc, g_smallFont ? g_smallFont : g_font);
        SetTextColor(d.dc, UI_MUT);
        RECT nr = { pv.left + S(4), pv.bottom - S(22), pv.right - S(4), pv.bottom - S(4) };
        DrawTextW(d.dc, g_thumbNote, -1, &nr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(d.dc, of);
    }
    // name, then the type as an amber tag
    int tx = pv.right + S(16), tw = w - tx - S(4);
    HFONT of = (HFONT)SelectObject(d.dc, g_titleFont ? g_titleFont : g_font);
    SetTextColor(d.dc, UI_INK);
    RECT nr = { tx, pv.top + S(2), tx + tw, pv.top + S(32) };
    DrawTextW(d.dc, g_hdrName, -1, &nr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(d.dc, g_smallFont ? g_smallFont : g_font);
    if (g_hdrTag[0]) {
        SIZE ts; GetTextExtentPoint32W(d.dc, g_hdrTag, (int)wcslen(g_hdrTag), &ts);
        int pwid = ts.cx + S(14); if (pwid > tw) pwid = tw;
        float ty0 = (float)(pv.top + S(36)), ty1 = ty0 + ts.cy + S(5);
        UiRRect(&d.cv, (float)tx, ty0, (float)(tx + pwid), ty1, (float)S(4), UI_TAGBG, 255);
        UiDibOpaque(&d);
        SetTextColor(d.dc, UI_ACC2);
        RECT tr = { tx + S(7), (int)ty0, tx + pwid - S(7), (int)ty1 };
        DrawTextW(d.dc, g_hdrTag, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(d.dc, of);
    BitBlt(dc, hr.left, hr.top, w, h, d.dc, 0, 0, SRCCOPY);
    UiDibFree(&d);
}

/* ------------------------------------------------------------------ status bar */
static void UiPaintStatusBar(HDC dc) {
    if (g_sbRect.bottom <= g_sbRect.top) return;
    FillRect(dc, &g_sbRect, g_brStatus);
    RECT line = { g_sbRect.left, g_sbRect.top, g_sbRect.right, g_sbRect.top + 1 };
    HBRUSH lb = CreateSolidBrush(UI_LINE); FillRect(dc, &line, lb); DeleteObject(lb);
    HFONT of = (HFONT)SelectObject(dc, g_smallFont ? g_smallFont : g_font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, UI_DIM);
    RECT r = g_sbRect; r.right -= S(12); r.top += S(4); r.bottom = r.top + S(24);
    DrawTextW(dc, L"Build " TEXT(BUILD_STAMP), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    // a green "ready" dot, amber while working
    int cy = g_sbRect.top + S(16), cx = g_sbRect.left + S(14), rr = S(4);
    COLORREF dotc = (g_running || g_searching || g_zipping) ? UI_ACC2 : RGB(0x5C, 0xCF, 0x7A);
    HBRUSH db = CreateSolidBrush(dotc); HGDIOBJ ob = SelectObject(dc, db); HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(db);
    SelectObject(dc, of);
}

/* ------------------------------------------------------------------ dark popup menus (every item owner-drawn) */
#define UI_MENU_MAGIC 0x554D4E55u
typedef struct { DWORD magic; wchar_t *text; int sep; } UiMenuItem;
static UiMenuItem *UiMenuData(ULONG_PTR d) { UiMenuItem *m = (UiMenuItem *)d; return (m && m->magic == UI_MENU_MAGIC) ? m : NULL; }
static void UiMenuPrepare(HMENU m) {
    MENUINFO mi = { sizeof mi }; mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS; mi.hbrBack = g_brPanel2;
    SetMenuInfo(m, &mi);
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        wchar_t t[512] = L"";
        MENUITEMINFOW ii = { sizeof ii }; ii.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_SUBMENU | MIIM_STRING; ii.dwTypeData = t; ii.cch = 511;
        if (!GetMenuItemInfoW(m, i, TRUE, &ii)) continue;
        if (ii.hSubMenu) UiMenuPrepare(ii.hSubMenu);
        if (ii.fType & MFT_OWNERDRAW) continue;                    // headings: already drawn by us
        UiMenuItem *u = (UiMenuItem *)calloc(1, sizeof *u); if (!u) continue;
        u->magic = UI_MENU_MAGIC; u->sep = (ii.fType & MFT_SEPARATOR) != 0; u->text = u->sep ? NULL : xwcsdup(t);
        MENUITEMINFOW si = { sizeof si }; si.fMask = MIIM_FTYPE | MIIM_DATA;
        si.fType = (ii.fType & ~(MFT_STRING | MFT_SEPARATOR)) | MFT_OWNERDRAW; si.dwItemData = (ULONG_PTR)u;
        SetMenuItemInfoW(m, i, TRUE, &si);
    }
}
static void UiMenuFree(HMENU m) {
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        MENUITEMINFOW ii = { sizeof ii }; ii.fMask = MIIM_DATA | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(m, i, TRUE, &ii)) continue;
        if (ii.hSubMenu) UiMenuFree(ii.hSubMenu);
        UiMenuItem *u = UiMenuData(ii.dwItemData);
        if (u) { xfree(u->text); u->magic = 0; free(u); }
    }
}
static BOOL UiMenuMeasure(MEASUREITEMSTRUCT *mi) {
    UiMenuItem *u = UiMenuData(mi->itemData); if (!u) return FALSE;
    if (u->sep) { mi->itemWidth = S(10); mi->itemHeight = S(9); return TRUE; }
    HDC dc = GetDC(g_wnd); HFONT of = (HFONT)SelectObject(dc, g_font);
    SIZE sz = { 0 }; GetTextExtentPoint32W(dc, u->text, (int)wcslen(u->text), &sz);
    SelectObject(dc, of); ReleaseDC(g_wnd, dc);
    mi->itemWidth = sz.cx + S(40); mi->itemHeight = S(28);
    return TRUE;
}
static BOOL UiMenuDraw(DRAWITEMSTRUCT *di) {
    UiMenuItem *u = UiMenuData(di->itemData); if (!u) return FALSE;
    RECT r = di->rcItem; HDC dc = di->hDC;
    FillRect(dc, &r, g_brPanel2);
    if (u->sep) { RECT l = { r.left + S(8), (r.top + r.bottom) / 2, r.right - S(8), (r.top + r.bottom) / 2 + 1 }; HBRUSH b = CreateSolidBrush(UI_LINE2); FillRect(dc, &l, b); DeleteObject(b); return TRUE; }
    BOOL dis = (di->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0, sel = (di->itemState & ODS_SELECTED) && !dis;
    if (sel) {
        RECT h = { r.left + S(4), r.top + S(1), r.right - S(4), r.bottom - S(1) };
        HBRUSH b = CreateSolidBrush(UI_SEL); FillRect(dc, &h, b); DeleteObject(b);
        RECT bar = { h.left, h.top + S(5), h.left + S(3), h.bottom - S(5) }; HBRUSH ab = CreateSolidBrush(UI_ACC); FillRect(dc, &bar, ab); DeleteObject(ab);
    }
    HFONT of = (HFONT)SelectObject(dc, g_font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, dis ? UI_DIM : UI_INK);
    RECT tr = { r.left + S(18), r.top, r.right - S(10), r.bottom };
    DrawTextW(dc, u->text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_EXPANDTABS | ((di->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
    SelectObject(dc, of);
    return TRUE;
}

/* ------------------------------------------------------------------ rounded text boxes and drop-downs
   Edit boxes: the rounded frame is painted in the non-client area (the text sits in the middle).
   Combo boxes: their own painting is replaced by a rounded box and a chevron; the edit inside draws the text. */
static BOOL UiHasFocusIn(HWND h) { HWND f = GetFocus(); return f && (f == h || IsChild(h, f)); }
static void UiPaintBox(HDC dc, int w, int h, BOOL focus, BOOL chevron, int clientL, int clientT, int clientR, int clientB) {
    UiDib d = { 0 };
    if (!UiDibMake(&d, w, h, dc)) return;
    UiDibClear(&d, UI_BG, 1);
    float r = (float)S(7);
    UiRRect(&d.cv, 0.5f, 0.5f, w - 0.5f, h - 0.5f, r, focus ? UI_ACC : UI_LINE2, 255);
    UiRRect(&d.cv, 1.5f, 1.5f, w - 1.5f, h - 1.5f, r - 1, UI_PANEL, 255);
    if (chevron) {
        float cx = w - S(14), cy = h / 2.0f, a = (float)S(4);
        UiLine(&d.cv, cx - a, cy - a / 2, cx, cy + a / 2, S(1) * 1.5f, UI_MUT, 255);
        UiLine(&d.cv, cx, cy + a / 2, cx + a, cy - a / 2, S(1) * 1.5f, UI_MUT, 255);
    }
    if (clientR > clientL) ExcludeClipRect(dc, clientL, clientT, clientR, clientB);
    BitBlt(dc, 0, 0, w, h, d.dc, 0, 0, SRCCOPY);
    UiDibFree(&d);
}
static LRESULT CALLBACK UiEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_NCCALCSIZE: {
        RECT *r = (RECT *)lp;                     // same for both forms of the message: the first rect
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        HDC dc = GetDC(h); HGDIOBJ o = SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW tm; GetTextMetricsW(dc, &tm); SelectObject(dc, o); ReleaseDC(h, dc);
        int hh = r->bottom - r->top, padY = (hh - tm.tmHeight) / 2; if (padY < 1) padY = 1;
        r->left += S(10); r->right -= S(8); r->top += padY; r->bottom -= padY;
        if (r->bottom < r->top) r->bottom = r->top;
        return 0;
    }
    case WM_NCPAINT: {
        RECT wr, cr; GetWindowRect(h, &wr); GetClientRect(h, &cr);
        POINT o = { 0, 0 }; ClientToScreen(h, &o);
        int cl = o.x - wr.left, ct = o.y - wr.top;
        HDC dc = GetWindowDC(h);
        UiPaintBox(dc, wr.right - wr.left, wr.bottom - wr.top, UiHasFocusIn(h), FALSE, cl, ct, cl + cr.right, ct + cr.bottom);
        ReleaseDC(h, dc);
        return 0;
    }
    case WM_SETFOCUS: case WM_KILLFOCUS: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_DRAWFRAME);
        return r;
    }
    }
    return DefSubclassProc(h, msg, wp, lp);
}
static LRESULT CALLBACK UiComboProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (UI_DROP_MSG(msg)) { LRESULT r = UiDropNoSlide(h, msg, wp, lp); InvalidateRect(h, NULL, FALSE); return r; }
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        COMBOBOXINFO ci = { sizeof ci }; GetComboBoxInfo(h, &ci);
        RECT er = ci.rcItem;                     // where the edit sits: leave it alone
        UiPaintBox(dc, rc.right, rc.bottom, UiHasFocusIn(h), TRUE, er.left, er.top, er.right, er.bottom);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLOREDIT: { HDC dc = (HDC)wp; SetTextColor(dc, UI_INK); SetBkColor(dc, UI_PANEL); return (LRESULT)g_brPanel; }
    }
    return DefSubclassProc(h, msg, wp, lp);
}
static void UiStyleInputs(void) {
    HWND eds[] = { g_romfs, g_gopath };
    for (int i = 0; i < 2; i++) {
        LONG_PTR ex = GetWindowLongPtrW(eds[i], GWL_EXSTYLE); SetWindowLongPtrW(eds[i], GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
        LONG_PTR st = GetWindowLongPtrW(eds[i], GWL_STYLE); SetWindowLongPtrW(eds[i], GWL_STYLE, st & ~WS_BORDER);
        SetWindowSubclass(eds[i], UiEditProc, 2, 0);
        SetWindowPos(eds[i], NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    HWND cbs[] = { g_out, g_sedit };
    for (int i = 0; i < 2; i++) {
        SetWindowSubclass(cbs[i], UiComboProc, 3, 0);
        COMBOBOXINFO ci = { sizeof ci };
        if (GetComboBoxInfo(cbs[i], &ci) && ci.hwndItem) SendMessageW(ci.hwndItem, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(S(4), 0));
        UiStyleDropList(cbs[i]);
    }
}
// size a drop-down so its closed height is exactly rh (Windows adds borders to the item height)
static void UiComboHeight(HWND cb, int rh) {
    SendMessageW(cb, CB_SETITEMHEIGHT, (WPARAM)-1, rh - S(6));
    RECT r; GetWindowRect(cb, &r);
    int got = r.bottom - r.top;
    if (got != rh) SendMessageW(cb, CB_SETITEMHEIGHT, (WPARAM)-1, rh - S(6) + (rh - got));
}
// repaint a box's frame when the focus moves in or out of it
static void UiInputFocusChanged(HWND ctl) {
    if (ctl == g_romfs || ctl == g_gopath) SetWindowPos(ctl, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_DRAWFRAME);
    else if (ctl == g_out || ctl == g_sedit) InvalidateRect(ctl, NULL, FALSE);
}

/* ------------------------------------------------------------------ coloured JSON in the text view
   The view stays a normal read-only edit box (search, selection, copy all work as before); when it holds a JSON
   file its visible lines are painted here with colours: keys amber, strings green, numbers coral, true/false/null
   blue, brackets and commas muted. */
static BOOL g_viewJson;
#define UI_JSON_KEY   UI_ACC2
#define UI_JSON_STR   RGB(0x9F, 0xD0, 0xA4)
#define UI_JSON_NUM   RGB(0xFF, 0x8F, 0x84)
#define UI_JSON_LIT   RGB(0x7F, 0xB8, 0xFF)
static int UiJsonClass(const wchar_t *t, int n, int i, int *end) {   // 0 plain 1 key 2 string 3 number 4 literal 5 punct
    wchar_t c = t[i];
    if (c == L'"') {
        int j = i + 1; while (j < n && t[j] != L'"') { if (t[j] == L'\\') j++; j++; }
        if (j < n) j++;
        *end = j;
        int k = j; while (k < n && (t[k] == L' ' || t[k] == L'\t')) k++;
        return (k < n && t[k] == L':') ? 1 : 2;
    }
    if ((c >= L'0' && c <= L'9') || (c == L'-' && i + 1 < n && t[i + 1] >= L'0' && t[i + 1] <= L'9')) {
        int j = i + 1; while (j < n && ((t[j] >= L'0' && t[j] <= L'9') || t[j] == L'.' || t[j] == L'e' || t[j] == L'E' || t[j] == L'+' || t[j] == L'-')) j++;
        *end = j; return 3;
    }
    if (c == L'{' || c == L'}' || c == L'[' || c == L']' || c == L',' || c == L':') { *end = i + 1; return 5; }
    static const wchar_t *lits[] = { L"true", L"false", L"null" };
    for (int k = 0; k < 3; k++) { int l = (int)wcslen(lits[k]); if (i + l <= n && !wcsncmp(t + i, lits[k], l)) { *end = i + l; return 4; } }
    int j = i + 1; while (j < n && t[j] != L'"' && !(t[j] >= L'0' && t[j] <= L'9') && !wcschr(L"{}[],:tfn-", t[j])) j++;
    *end = j; return 0;
}
static LRESULT CALLBACK UiViewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_SETTEXT: g_viewJson = FALSE; break;
    case WM_ERASEBKGND: if (g_viewJson) return 1; break;
    case WM_PAINT: {
        if (!g_viewJson) break;
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        HideCaret(h);
        RECT rc; GetClientRect(h, &rc);
        HDC md = CreateCompatibleDC(dc); HBITMAP mb = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(md, mb);
        FillRect(md, &rc, g_brPanel);
        HFONT of = (HFONT)SelectObject(md, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        TEXTMETRICW tm; GetTextMetricsW(md, &tm);
        SetBkMode(md, TRANSPARENT);
        DWORD ss = 0, se = 0; SendMessageW(h, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
        int first = (int)SendMessageW(h, EM_GETFIRSTVISIBLELINE, 0, 0), lines = (int)SendMessageW(h, EM_GETLINECOUNT, 0, 0);
        int lh = tm.tmHeight; if (lh < 1) lh = 1;
        int y0 = 0; { int i0 = (int)SendMessageW(h, EM_LINEINDEX, first, 0); LRESULT p0 = SendMessageW(h, EM_POSFROMCHAR, i0, 0); if (p0 != -1) y0 = (short)HIWORD(p0); }
        HBRUSH selBr = CreateSolidBrush(UI_ACC);
        wchar_t sbuf[1024];
        for (int ln = first; ln < lines; ln++) {
            int y = y0 + (ln - first) * lh; if (y >= rc.bottom) break;
            int idx = (int)SendMessageW(h, EM_LINEINDEX, ln, 0); if (idx < 0) break;
            int len = (int)SendMessageW(h, EM_LINELENGTH, idx, 0);
            LRESULT pos = SendMessageW(h, EM_POSFROMCHAR, idx, 0);
            int x = pos != -1 ? (short)LOWORD(pos) : 0;
            wchar_t *t = len < 1023 ? sbuf : (wchar_t *)xalloc(((size_t)len + 8) * sizeof(wchar_t));
            if (!t) break;
            *(WORD *)t = (WORD)(len < 65535 ? len + 1 : 65535);
            int got = len ? (int)SendMessageW(h, EM_GETLINE, ln, (LPARAM)t) : 0; t[got] = 0;
            int startCh = 0;
            if (got > 2000 && x < 0 && tm.tmAveCharWidth > 0) {          // very long line scrolled sideways: skip what's off screen
                startCh = -x / tm.tmAveCharWidth - 4; if (startCh < 0) startCh = 0; if (startCh > got) startCh = got;
                x += startCh * tm.tmAveCharWidth;
            }
            int lineX = x;
            for (int i = startCh; i < got && x < rc.right; ) {
                int end = i + 1, k = UiJsonClass(t, got, i, &end);
                if (end <= i) end = i + 1;
                SetTextColor(md, k == 1 ? UI_JSON_KEY : k == 2 ? UI_JSON_STR : k == 3 ? UI_JSON_NUM : k == 4 ? UI_JSON_LIT : k == 5 ? UI_MUT : UI_INK);
                TextOutW(md, x, y, t + i, end - i);
                SIZE sz; GetTextExtentPoint32W(md, t + i, end - i, &sz); x += sz.cx;
                i = end;
            }
            // the selection (search match, drag, Ctrl+A): solid coral with dark text, drawn over the colours
            if (se > ss && (int)se > idx && (int)ss <= idx + got) {
                int a = (int)ss - idx, b = (int)se - idx; if (a < startCh) a = startCh; if (b > got) b = got;
                SIZE s1 = { 0 }, s2 = { 0 };
                if (a > startCh) GetTextExtentPoint32W(md, t + startCh, a - startCh, &s1);
                GetTextExtentPoint32W(md, t + startCh, b - startCh, &s2);
                RECT sr = { lineX + s1.cx, y, lineX + s2.cx + ((int)se > idx + got ? tm.tmAveCharWidth / 2 : 0), y + lh };
                FillRect(md, &sr, selBr);
                if (b > a) { SetTextColor(md, UI_ACC_INK); TextOutW(md, sr.left, y, t + a, b - a); }
            }
            if (t != sbuf) xfree(t);
        }
        DeleteObject(selBr);
        SelectObject(md, of);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, md, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(md, ob); DeleteObject(mb); DeleteDC(md);
        ShowCaret(h);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_MOUSEMOVE: case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_VSCROLL: case WM_HSCROLL: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case EM_SETSEL: case EM_LINESCROLL: case EM_SCROLLCARET: case EM_REPLACESEL: {
        if (!g_viewJson) break;
        // the edit box draws some changes (selection, scrolling) itself: repaint with colours afterwards
        if (msg == WM_MOUSEMOVE && !(wp & MK_LBUTTON)) break;
        DWORD b0 = 0, e0 = 0; SendMessageW(h, EM_GETSEL, (WPARAM)&b0, (LPARAM)&e0);
        DefSubclassProc(h, WM_SETREDRAW, FALSE, 0);
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        DefSubclassProc(h, WM_SETREDRAW, TRUE, 0);
        // with drawing held off the edit box doesn't move its caret: put it at the end of the selection that moved
        if (GetFocus() == h) {
            DWORD b1 = 0, e1 = 0; SendMessageW(h, EM_GETSEL, (WPARAM)&b1, (LPARAM)&e1);
            static DWORD caretAt = 0;
            if (msg == WM_LBUTTONDOWN || b1 == e1) caretAt = e1;
            else if (b1 != b0 && e1 == e0) caretAt = b1;
            else if (e1 != e0 && b1 == b0) caretAt = e1;
            else if (b1 != b0 || e1 != e0) caretAt = (caretAt == b0) ? b1 : e1;
            if (caretAt < b1 || caretAt > e1) caretAt = e1;
            LRESULT p = SendMessageW(h, EM_POSFROMCHAR, caretAt, 0);
            if (p == -1 && caretAt > 0) {             // just past the last character: after the one before it
                LRESULT q = SendMessageW(h, EM_POSFROMCHAR, caretAt - 1, 0);
                if (q != -1) {
                    wchar_t c[2] = { 0 }; int li = (int)SendMessageW(h, EM_LINEFROMCHAR, caretAt - 1, 0), ix = (int)SendMessageW(h, EM_LINEINDEX, li, 0);
                    wchar_t lb[4096]; *(WORD *)lb = 4095; int gl = (int)SendMessageW(h, EM_GETLINE, li, (LPARAM)lb);
                    int k = (int)(caretAt - 1) - ix; c[0] = (k >= 0 && k < gl) ? lb[k] : L' ';
                    HDC dc = GetDC(h); HGDIOBJ of = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
                    SIZE sz; GetTextExtentPoint32W(dc, c, 1, &sz); SelectObject(dc, of); ReleaseDC(h, dc);
                    p = MAKELPARAM((short)LOWORD(q) + sz.cx, HIWORD(q));
                }
            }
            if (p != -1) SetCaretPos((short)LOWORD(p), (short)HIWORD(p));
        }
        InvalidateRect(h, NULL, FALSE);
        UpdateWindow(h);
        return r;
    }
    }
    return DefSubclassProc(h, msg, wp, lp);
}
static void UiSetViewJson(BOOL on) { g_viewJson = on; InvalidateRect(g_view, NULL, FALSE); }
