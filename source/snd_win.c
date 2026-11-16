// snd_win.c - sound player window for BWAV / BARS files (Windows waveOut, built into Windows).
// SndPlayerOpen() reads the file from the game dump (zstd-compressed .zs files are fine), lists its
// sounds and plays them, honouring loop points. Nothing is written to disk unless "Save as WAV" is used.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void UiSkinWindow(HWND top);
#include <mmsystem.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>
#include "zstd.h"
#include "snd_core.h"

#define WM_SND_ENDED (WM_APP + 70)
#define SND_TIMER 1
enum { SN_LIST = 3001, SN_PLAY, SN_STOP, SN_LOOP, SN_POS, SN_TIME, SN_INFO, SN_SAVE, SN_HELP };

#define SND_NBUF 4
#define SND_BUFFRAMES 4096

typedef struct {
    HWND wnd, info, list, play, stop, loop, pos, time, save, help;
    HFONT font; int dpi;
    wchar_t path[2048], title[600];
    SndFile file; int fileOk;
    // current sound
    int cur;                      // entry index decoded into pcm, -1 none
    int16_t *pcm; int pch; int rate; size_t frames;
    size_t loopStart, loopEnd;    // loopEnd 0 = no loop points
    // playback thread
    HANDLE thread; volatile LONG stopReq, playing, loopOn;
    volatile LONG64 playPos;      // frame being heard (approximately)
    volatile LONG64 seekTo;       // -1 = none
    int dragging;
    wchar_t streamNote[1200];     // where the full sound of a prefetch-only entry came from (or was looked for)
} SP;

static unsigned char *SPRead(const wchar_t *path, size_t *size, char *err, size_t errn);
// first existing stream file for a prefetch-only entry (Stream\<name>.bwav(.zs) beside the .bars or one folder up); 0 if none
static int SPFindStream(const SP *s, const char *name, int from, wchar_t *cand, int candN, int *idx) {
    wchar_t dir[2048]; swprintf(dir, 2048, L"%ls", s->path);
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    static const wchar_t *sub[] = { L"%ls\\Stream\\%hs.bwav", L"%ls\\Stream\\%hs.bwav.zs", L"%ls\\..\\Stream\\%hs.bwav", L"%ls\\..\\Stream\\%hs.bwav.zs",
                                    L"%ls\\%hs.bwav", L"%ls\\%hs.bwav.zs" };
    for (int k = from; k < 6; k++) {
        swprintf(cand, candN, sub[k], dir, name);
        if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) { if (idx) *idx = k; return (int)wcslen(dir) + 1; }
    }
    return 0;
}

static int SPS(SP *s, int x) { return MulDiv(x, s->dpi, 96); }

/* ------------------------------------------------------------------ playback thread */
static DWORD WINAPI SPThread(LPVOID p) {
    SP *s = (SP *)p;
    WAVEFORMATEX wf; ZeroMemory(&wf, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = (WORD)s->pch; wf.nSamplesPerSec = (DWORD)s->rate;
    wf.wBitsPerSample = 16; wf.nBlockAlign = (WORD)(2 * s->pch); wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    HWAVEOUT wo;
    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(ev); InterlockedExchange(&s->playing, 0); PostMessageW(s->wnd, WM_SND_ENDED, 1, 0); return 0;
    }
    WAVEHDR hdr[SND_NBUF]; int16_t *buf[SND_NBUF]; size_t startPos[SND_NBUF]; unsigned long long seq[SND_NBUF], nextSeq = 1;
    for (int i = 0; i < SND_NBUF; i++) {
        buf[i] = (int16_t *)malloc(SND_BUFFRAMES * s->pch * 2);
        ZeroMemory(&hdr[i], sizeof(WAVEHDR)); hdr[i].lpData = (LPSTR)buf[i]; hdr[i].dwBufferLength = SND_BUFFRAMES * s->pch * 2;
        waveOutPrepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
        hdr[i].dwFlags |= WHDR_DONE;                 // free to fill
        startPos[i] = 0; seq[i] = 0;
    }
    size_t pos = (size_t)s->playPos; if (pos >= s->frames) pos = 0;
    int finished = 0;
    while (!s->stopReq) {
        LONG64 sk = InterlockedExchange64(&s->seekTo, -1);
        if (sk >= 0) {                               // jump: drop what's queued and refill from there
            waveOutReset(wo);
            pos = (size_t)sk < s->frames ? (size_t)sk : 0; finished = 0;
            for (int i = 0; i < SND_NBUF; i++) { hdr[i].dwFlags |= WHDR_DONE; seq[i] = 0; }
            InterlockedExchange64(&s->playPos, (LONG64)pos);
        }
        for (int i = 0; i < SND_NBUF && !finished; i++) {
            if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
            size_t n = 0;
            startPos[i] = pos;
            while (n < SND_BUFFRAMES) {
                int looping = s->loopOn && s->loopEnd;
                size_t end = looping ? s->loopEnd : s->frames;
                if (pos >= end) { if (looping) { pos = s->loopStart; continue; } break; }
                size_t take = end - pos; if (take > SND_BUFFRAMES - n) take = SND_BUFFRAMES - n;
                memcpy(buf[i] + n * s->pch, s->pcm + pos * s->pch, take * s->pch * 2);
                n += take; pos += take;
            }
            if (!n) { finished = 1; break; }
            hdr[i].dwBufferLength = (DWORD)(n * s->pch * 2);
            hdr[i].dwFlags &= ~WHDR_DONE;
            seq[i] = nextSeq++;
            waveOutWrite(wo, &hdr[i], sizeof(WAVEHDR));
        }
        // what's heard now = start of the oldest buffer still queued (buffers finish in the order sent)
        int oldest = -1;
        for (int i = 0; i < SND_NBUF; i++)
            if (!(hdr[i].dwFlags & WHDR_DONE) && seq[i] && (oldest < 0 || seq[i] < seq[oldest])) oldest = i;
        if (oldest >= 0) InterlockedExchange64(&s->playPos, (LONG64)startPos[oldest]);
        else if (finished) break;                    // everything played
        WaitForSingleObject(ev, 50);
    }
    waveOutReset(wo);
    for (int i = 0; i < SND_NBUF; i++) { waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR)); free(buf[i]); }
    waveOutClose(wo);
    CloseHandle(ev);
    int natural = !s->stopReq;
    InterlockedExchange(&s->playing, 0);
    if (natural) { InterlockedExchange64(&s->playPos, 0); PostMessageW(s->wnd, WM_SND_ENDED, 0, 0); }
    return 0;
}

static void SPStop(SP *s) {
    if (s->thread) {
        InterlockedExchange(&s->stopReq, 1);
        WaitForSingleObject(s->thread, 5000);
        CloseHandle(s->thread); s->thread = NULL;
    }
    InterlockedExchange(&s->playing, 0);
}

/* ------------------------------------------------------------------ UI helpers */
static void SPFmtTime(wchar_t *o, size_t n, double sec) {
    if (sec < 0) sec = 0;
    int m = (int)(sec / 60); double r = sec - m * 60;
    swprintf(o, n, L"%d:%04.1f", m, r);
}

static void SPUpdateTime(SP *s) {
    wchar_t a[32], b[32], t[80];
    double cur = s->rate ? (double)s->playPos / s->rate : 0, tot = s->rate ? (double)s->frames / s->rate : 0;
    SPFmtTime(a, 32, cur); SPFmtTime(b, 32, tot);
    swprintf(t, 80, L"%ls / %ls", a, b);
    SetWindowTextW(s->time, t);
    if (!s->dragging && s->frames) SendMessageW(s->pos, TBM_SETPOS, TRUE, (LPARAM)((double)s->playPos * 1000 / s->frames));
}

static void SPShowInfo(SP *s) {
    wchar_t t[1400], extra[400] = L"";
    if (!s->fileOk) { swprintf(t, 1400, L"Couldn't read this file:\r\n%hs", s->file.err); SetWindowTextW(s->info, t); return; }
    int sel = (int)SendMessageW(s->list, LB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < s->file.n && s->file.e[sel].ok) {
        SndEntry *e = &s->file.e[sel];
        wchar_t len[32], ls[32], le[32];
        SPFmtTime(len, 32, (double)e->samples / e->rate);
        if (e->loopEnd) { SPFmtTime(ls, 32, (double)e->loopStart / e->rate); SPFmtTime(le, 32, (double)e->loopEnd / e->rate); swprintf(extra, 400, L"Loops from %ls to %ls", ls, le); }
        else swprintf(extra, 400, L"Doesn't loop");
        swprintf(t, 1400, L"%ls\r\n%d sound%ls in this file\r\n\r\nSelected: %hs\r\n%ls, %d channel%ls, %d Hz, %ls\r\n%ls",
                 s->title, s->file.n, s->file.n == 1 ? L"" : L"s", e->name, len, e->nch, e->nch == 1 ? L"" : L"s", e->rate,
                 e->codec == 1 ? L"DSP-ADPCM" : L"PCM 16-bit", extra);
        if (e->prefetch) {
            size_t l = wcslen(t);
            if (sel == s->cur && s->streamNote[0]) swprintf(t + l, 1400 - l, L"\r\n%ls", s->streamNote);
            else swprintf(t + l, 1400 - l, L"\r\nThis file holds only the start of the sound; the full sound is read from the Stream folder when you play it.");
        }
    } else swprintf(t, 1400, L"%ls\r\n%d sound%ls in this file", s->title, s->file.n, s->file.n == 1 ? L"" : L"s");
    SetWindowTextW(s->info, t);
}

// decode the selected entry (if not already) - returns 0 if it can't be played
static int SPLoadSel(SP *s) {
    int sel = (int)SendMessageW(s->list, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= s->file.n || !s->file.e[sel].ok) return 0;
    if (sel == s->cur && s->pcm) return 1;
    SPStop(s);
    free(s->pcm); s->pcm = NULL; s->cur = -1;
    SndEntry *e = &s->file.e[sel];
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    s->streamNote[0] = 0;
    SndEntry use = *e;
    if (e->prefetch) {
        // only the start is in this BARS: look for the full sound in a Stream folder nearby
        wchar_t cand[2300]; int k = 0, found = 0, rel;
        while (!found && (rel = SPFindStream(s, e->name, k, cand, 2300, &k)) > 0) {
            k++;
            size_t n = 0; char err[200];
            unsigned char *d = SPRead(cand, &n, err, sizeof err);
            if (!d) continue;
            SndFile sf;
            if (SndOpenData(&sf, d, n) && sf.n >= 1 && sf.e[0].ok && !sf.e[0].prefetch) {
                s->pcm = SndDecode(&sf, &sf.e[0], &s->pch);
                if (s->pcm) {
                    use = sf.e[0]; found = 1;
                    swprintf(s->streamNote, 1200, L"Full sound read from %ls", cand + rel);
                }
            }
            SndClose(&sf);
        }
        if (!found) {
            use.samples = e->stored;
            if (use.loopEnd > use.samples) { use.loopEnd = 0; use.loopStart = 0; }
            swprintf(s->streamNote, 1200, L"Only the first %.2f s of %.2f s is stored in this file, and the full sound wasn't found at Stream\\%hs.bwav",
                     (double)e->stored / e->rate, (double)e->samples / e->rate, e->name);
        }
    }
    if (!s->pcm) s->pcm = SndDecode(&s->file, &use, &s->pch);
    SetCursor(old);
    if (!s->pcm) { MessageBoxW(s->wnd, L"Out of memory decoding this sound.", L"Sound player", MB_ICONWARNING); return 0; }
    s->cur = sel; s->rate = use.rate; s->frames = use.samples;
    s->loopStart = use.loopStart; s->loopEnd = use.loopEnd;
    SPShowInfo(s);
    s->playPos = 0;
    EnableWindow(s->loop, e->loopEnd != 0);
    SPUpdateTime(s);
    return 1;
}

static void SPPlay(SP *s) {
    if (!SPLoadSel(s)) return;
    SPStop(s);
    if ((size_t)s->playPos >= s->frames) s->playPos = 0;
    InterlockedExchange(&s->stopReq, 0);
    InterlockedExchange64(&s->seekTo, -1);
    InterlockedExchange(&s->loopOn, SendMessageW(s->loop, BM_GETCHECK, 0, 0) == BST_CHECKED);
    InterlockedExchange(&s->playing, 1);
    s->thread = CreateThread(NULL, 0, SPThread, s, 0, NULL);
    if (!s->thread) InterlockedExchange(&s->playing, 0);
    SetWindowTextW(s->play, L"Restart");
}

static void SPSaveWav(SP *s) {
    if (!SPLoadSel(s)) return;
    SndEntry *e = &s->file.e[s->cur];
    wchar_t name[700];
    const wchar_t *leaf = wcsrchr(s->path, L'\\'); leaf = leaf ? leaf + 1 : s->path;
    if (s->file.isBars) swprintf(name, 700, L"%hs.wav", e->name);
    else { swprintf(name, 700, L"%ls", leaf); wchar_t *d = wcsstr(name, L".bwav"); if (d) *d = 0; wcscat(name, L".wav"); }
    for (wchar_t *q = name; *q; q++) if (wcschr(L"\\/:*?\"<>|", *q)) *q = L'_';
    wchar_t out[2048]; swprintf(out, 2048, L"%ls", name);
    OPENFILENAMEW of; ZeroMemory(&of, sizeof(of));
    of.lStructSize = sizeof(of); of.hwndOwner = s->wnd; of.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0";
    of.lpstrFile = out; of.nMaxFile = 2048; of.lpstrDefExt = L"wav"; of.lpstrTitle = L"Save sound as WAV";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return;
    FILE *f = _wfopen(out, L"wb");
    if (!f) { MessageBoxW(s->wnd, L"Couldn't create the file.", L"Save as WAV", MB_ICONWARNING); return; }
    uint32_t dataBytes = (uint32_t)(s->frames * s->pch * 2);
    uint32_t h[11];
    unsigned char hb[44]; memcpy(hb, "RIFF", 4); uint32_t v = 36 + dataBytes; memcpy(hb + 4, &v, 4); memcpy(hb + 8, "WAVEfmt ", 8);
    v = 16; memcpy(hb + 16, &v, 4); uint16_t w = 1; memcpy(hb + 20, &w, 2); w = (uint16_t)s->pch; memcpy(hb + 22, &w, 2);
    v = (uint32_t)s->rate; memcpy(hb + 24, &v, 4); v = (uint32_t)(s->rate * s->pch * 2); memcpy(hb + 28, &v, 4);
    w = (uint16_t)(s->pch * 2); memcpy(hb + 32, &w, 2); w = 16; memcpy(hb + 34, &w, 2); memcpy(hb + 36, "data", 4); memcpy(hb + 40, &dataBytes, 4);
    (void)h;
    fwrite(hb, 1, 44, f); fwrite(s->pcm, 1, dataBytes, f);
    int bad = ferror(f); fclose(f);
    if (bad) MessageBoxW(s->wnd, L"Writing the file failed (disk full?).", L"Save as WAV", MB_ICONWARNING);
}

static void SPLayout(SP *s) {
    RECT rc; GetClientRect(s->wnd, &rc);
    int m = SPS(s, 8), gap = SPS(s, 6), rh = SPS(s, 26), w = rc.right - 2 * m, y = m;
    int infoH = SPS(s, 150);
    MoveWindow(s->info, m, y, w, infoH, TRUE); y += infoH + gap;
    int bottom = rh * 2 + gap * 3 + SPS(s, 20);
    int listH = rc.bottom - y - bottom - m; if (listH < SPS(s, 40)) listH = SPS(s, 40);
    MoveWindow(s->list, m, y, w, listH, TRUE); y += listH + gap;
    int bw = SPS(s, 90), tw = SPS(s, 120);
    MoveWindow(s->pos, m, y, w - tw - gap, rh, TRUE);
    MoveWindow(s->time, m + w - tw, y + SPS(s, 4), tw, rh - SPS(s, 4), TRUE); y += rh + gap;
    MoveWindow(s->play, m, y, bw, rh, TRUE);
    MoveWindow(s->stop, m + bw + gap, y, bw, rh, TRUE);
    MoveWindow(s->loop, m + 2 * (bw + gap), y + SPS(s, 2), SPS(s, 170), rh - SPS(s, 2), TRUE);
    int sw = SPS(s, 130);
    MoveWindow(s->save, m + w - sw, y, sw, rh, TRUE); y += rh + gap;
    MoveWindow(s->help, m, y, w, SPS(s, 20), TRUE);
}

static HWND SPCtl(SP *s, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, s->wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)s->font, TRUE);
    return h;
}

static LRESULT CALLBACK SPProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SP *s = (SP *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!s && msg != WM_CREATE) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        s = (SP *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
        s->wnd = hwnd; s->cur = -1; s->seekTo = -1;
        s->info = SPCtl(s, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, SN_INFO);
        s->list = SPCtl(s, L"LISTBOX", L"", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, SN_LIST);
        s->pos  = SPCtl(s, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, SN_POS);
        SendMessageW(s->pos, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000)); SendMessageW(s->pos, TBM_SETPAGESIZE, 0, 50);
        s->time = SPCtl(s, L"STATIC", L"0:00.0 / 0:00.0", SS_RIGHT, 0, SN_TIME);
        s->play = SPCtl(s, L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, SN_PLAY);
        s->stop = SPCtl(s, L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, 0, SN_STOP);
        s->loop = SPCtl(s, L"BUTTON", L"Loop (at its loop points)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, SN_LOOP);
        SendMessageW(s->loop, BM_SETCHECK, BST_CHECKED, 0);
        s->save = SPCtl(s, L"BUTTON", L"Save as WAV...", BS_PUSHBUTTON | WS_TABSTOP, 0, SN_SAVE);
        s->help = SPCtl(s, L"STATIC", L"Double-click a sound to play it. Drag the slider to jump.", SS_LEFT, 0, SN_HELP);
        if (s->fileOk) {
            for (int i = 0; i < s->file.n; i++) {
                SndEntry *e = &s->file.e[i];
                wchar_t t[300], len[32], sfp[2300];
                if (e->ok) {
                    SPFmtTime(len, 32, (double)e->samples / e->rate);
                    swprintf(t, 300, L"%hs    (%ls, %ls%ls%ls)", e->name, len, e->nch == 1 ? L"mono" : e->nch == 2 ? L"stereo" : L"multi-channel", e->loopEnd ? L", loops" : L"", !e->prefetch ? L"" : SPFindStream(s, e->name, 0, sfp, 2300, NULL) ? L", full sound in Stream folder" : L", only the start - full sound missing");
                } else swprintf(t, 300, L"%hs    (can't be read)", e->name);
                SendMessageW(s->list, LB_ADDSTRING, 0, (LPARAM)t);
            }
            SendMessageW(s->list, LB_SETCURSEL, 0, 0);
        } else {
            EnableWindow(s->play, FALSE); EnableWindow(s->stop, FALSE); EnableWindow(s->save, FALSE); EnableWindow(s->pos, FALSE);
        }
        SPShowInfo(s);
        SPLayout(s);
        SetTimer(hwnd, SND_TIMER, 100, NULL);
        if (s->fileOk && s->file.n == 1) PostMessageW(hwnd, WM_COMMAND, SN_PLAY, 0);   // single sound: start playing
        return 0;
    }
    case WM_SIZE: SPLayout(s); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = SPS(s, 520); ((MINMAXINFO *)lp)->ptMinTrackSize.y = SPS(s, 340); return 0;
    case WM_TIMER: if (s->pcm) SPUpdateTime(s); return 0;
    case WM_SND_ENDED:
        if (s->thread) { WaitForSingleObject(s->thread, 2000); CloseHandle(s->thread); s->thread = NULL; }
        SetWindowTextW(s->play, L"Play");
        if (wp == 1) MessageBoxW(hwnd, L"Couldn't open the sound output (no audio device?).", L"Sound player", MB_ICONWARNING);
        SPUpdateTime(s);
        return 0;
    case WM_HSCROLL:
        if ((HWND)lp == s->pos && s->frames) {
            int code = LOWORD(wp);
            if (code == TB_THUMBTRACK) { s->dragging = 1; return 0; }
            s->dragging = 0;
            LONG64 to = (LONG64)SendMessageW(s->pos, TBM_GETPOS, 0, 0) * (LONG64)s->frames / 1000;
            if (s->playing) InterlockedExchange64(&s->seekTo, to);
            InterlockedExchange64(&s->playPos, to);
            SPUpdateTime(s);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case SN_PLAY: s->playPos = s->playing ? 0 : s->playPos; SPPlay(s); break;
        case SN_STOP: SPStop(s); s->playPos = 0; SetWindowTextW(s->play, L"Play"); SPUpdateTime(s); break;
        case SN_LOOP: InterlockedExchange(&s->loopOn, SendMessageW(s->loop, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        case SN_SAVE: SPSaveWav(s); break;
        case SN_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                int was = s->playing;
                SPShowInfo(s);
                if (SPLoadSel(s) && was) { s->playPos = 0; SPPlay(s); }
                else if (!was) SetWindowTextW(s->play, L"Play");
            } else if (HIWORD(wp) == LBN_DBLCLK) { SPLoadSel(s); s->playPos = 0; SPPlay(s); }
            break;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, SND_TIMER);
        SPStop(s);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        { MSG mm; while (PeekMessageW(&mm, hwnd, WM_SND_ENDED, WM_SND_ENDED, PM_REMOVE)) { } }
        free(s->pcm);
        SndClose(&s->file);
        free(s);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// read a whole file (long paths ok); zstd-compressed files are decompressed
static unsigned char *SPRead(const wchar_t *path, size_t *size, char *err, size_t errn) {
    wchar_t lp[2200];
    if (wcsncmp(path, L"\\\\", 2) == 0) swprintf(lp, 2200, L"%ls", path); else swprintf(lp, 2200, L"\\\\?\\%ls", path);
    HANDLE h = CreateFileW(lp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) { snprintf(err, errn, "The file couldn't be opened."); return NULL; }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 0x7FFFFFFFLL) { CloseHandle(h); snprintf(err, errn, "The file is empty or too big."); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)sz.QuadPart);
    DWORD got = 0;
    if (!b || !ReadFile(h, b, (DWORD)sz.QuadPart, &got, NULL)) { free(b); CloseHandle(h); snprintf(err, errn, "Reading the file failed."); return NULL; }
    CloseHandle(h);
    *size = got;
    if (got >= 4 && b[0] == 0x28 && b[1] == 0xB5 && b[2] == 0x2F && b[3] == 0xFD) {
        unsigned long long n = ZSTD_getFrameContentSize(b, got);
        if (n == ZSTD_CONTENTSIZE_ERROR || n == ZSTD_CONTENTSIZE_UNKNOWN || n > 0x7FFFFFFFULL) {
            // unknown size: stream it
            ZSTD_DStream *ds = ZSTD_createDStream(); ZSTD_initDStream(ds);
            size_t cap = (size_t)got * 4 + 65536, len = 0; unsigned char *o = (unsigned char *)malloc(cap);
            ZSTD_inBuffer in = { b, got, 0 };
            while (o) {
                if (len == cap) { cap *= 2; unsigned char *no = (unsigned char *)realloc(o, cap); if (!no) { free(o); o = NULL; break; } o = no; }
                ZSTD_outBuffer out = { o, cap, len };
                size_t r = ZSTD_decompressStream(ds, &out, &in);
                len = out.pos;
                if (ZSTD_isError(r)) { snprintf(err, errn, "Decompressing failed: %s", ZSTD_getErrorName(r)); free(o); o = NULL; break; }
                if (r == 0 && in.pos == in.size) break;
                if (in.pos == in.size && out.pos < out.size) break;
            }
            ZSTD_freeDStream(ds); free(b);
            if (!o) return NULL;
            *size = len; return o;
        }
        unsigned char *o = (unsigned char *)malloc((size_t)n + 1);
        if (!o) { free(b); snprintf(err, errn, "Out of memory."); return NULL; }
        size_t r = ZSTD_decompress(o, (size_t)n, b, got);
        free(b);
        if (ZSTD_isError(r)) {
            snprintf(err, errn, "Decompressing failed: %s%s", ZSTD_getErrorName(r),
                     ZSTD_getErrorCode(r) == ZSTD_error_dictionary_wrong ? " (this file needs a zstd dictionary, which the player can't load yet)" : "");
            free(o); return NULL;
        }
        *size = r; return o;
    }
    return b;
}

void SndPlayerOpen(HWND owner, const wchar_t *dumpFile, const wchar_t *title, HFONT font, int dpi) {
    static int registered = 0;
    HINSTANCE inst = GetModuleHandleW(NULL);
    if (!registered) {
        WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = SPProc; wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1)); wc.lpszClassName = L"AlsarcSoundPlayer";
        RegisterClassExW(&wc);
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES }; InitCommonControlsEx(&icc);
        registered = 1;
    }
    SP *s = (SP *)calloc(1, sizeof(SP));
    if (!s) return;
    s->font = font; s->dpi = dpi > 0 ? dpi : 96;
    swprintf(s->path, 2048, L"%ls", dumpFile);
    swprintf(s->title, 600, L"%ls", title);
    size_t n = 0;
    unsigned char *d = SPRead(dumpFile, &n, s->file.err, sizeof s->file.err);
    if (d) {
        char keep[200]; keep[0] = 0;
        s->fileOk = SndOpenData(&s->file, d, n);   // takes ownership of d
        (void)keep;
    }
    wchar_t wt[700]; const wchar_t *leaf = wcsrchr(title, L'/'); swprintf(wt, 700, L"Sound - %ls", leaf ? leaf + 1 : title);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = MulDiv(640, s->dpi, 96), h = MulDiv(490, s->dpi, 96);
    HWND wnd = CreateWindowExW(0, L"AlsarcSoundPlayer", wt, WS_OVERLAPPEDWINDOW,
                               wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h, NULL, NULL, inst, s);
    (void)owner;
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    if (wnd) { ShowWindow(wnd, SW_SHOWNORMAL); UpdateWindow(wnd); }
    else { SndClose(&s->file); free(s); }
}

/* ------------------------------------------------------------------ preview for the main window */
// a waveform picture of the first sound in a BWAV / BARS (the full sound from its stream file when the BARS only
// has the start), as premultiplied BGRA sized boxW x boxH; note gets e.g. "0:01.0 - 4 sounds"
unsigned char *SndThumbnail(const wchar_t *dumpFile, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN) {
    note[0] = 0;
    size_t n = 0; char err[200];
    unsigned char *d = SPRead(dumpFile, &n, err, sizeof err);
    if (!d) return NULL;
    SndFile f; if (!SndOpenData(&f, d, n)) { SndClose(&f); return NULL; }
    int sel = -1; for (int i = 0; i < f.n; i++) if (f.e[i].ok) { sel = i; break; }
    if (sel < 0) { SndClose(&f); return NULL; }
    SndEntry e = f.e[sel];
    int16_t *pcm = NULL; int ch = 1; size_t frames = 0; int rate = e.rate;
    if (e.prefetch) {                       // look for the full sound beside the BARS, as the player does
        wchar_t dir[2048]; swprintf(dir, 2048, L"%ls", dumpFile); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
        const wchar_t *sub[] = { L"%ls\\Stream\\%hs.bwav", L"%ls\\Stream\\%hs.bwav.zs", L"%ls\\..\\Stream\\%hs.bwav", L"%ls\\..\\Stream\\%hs.bwav.zs" };
        for (int k = 0; k < 4 && !pcm; k++) {
            wchar_t cand[2300]; swprintf(cand, 2300, sub[k], dir, e.name);
            if (GetFileAttributesW(cand) == INVALID_FILE_ATTRIBUTES) continue;
            size_t sn = 0; unsigned char *sd = SPRead(cand, &sn, err, sizeof err); if (!sd) continue;
            SndFile sf;
            if (SndOpenData(&sf, sd, sn) && sf.n >= 1 && sf.e[0].ok && !sf.e[0].prefetch) { pcm = SndDecode(&sf, &sf.e[0], &ch); frames = sf.e[0].samples; rate = sf.e[0].rate; }
            SndClose(&sf);
        }
        if (!pcm) { e.samples = e.stored; if (e.loopEnd > e.samples) e.loopEnd = 0; }
    }
    if (!pcm) { pcm = SndDecode(&f, &e, &ch); frames = e.samples; }
    int count = 0; for (int i = 0; i < f.n; i++) count += f.e[i].ok;
    SndClose(&f);
    if (!pcm || !frames || boxW < 4 || boxH < 4) { free(pcm); return NULL; }
    double secs = (double)frames / (rate > 0 ? rate : 48000);
    int mm = (int)(secs / 60); double ss = secs - mm * 60;
    if (count > 1) swprintf(note, noteN, L"%d:%04.1f - %d sounds", mm, ss, count); else swprintf(note, noteN, L"%d:%04.1f", mm, ss);
    // waveform: for each column the lowest and highest sample (channels mixed), drawn in coral around a faint centre line
    int W = boxW, H = boxH;
    unsigned char *px = (unsigned char *)calloc((size_t)W * boxH, 4);
    if (!px) { free(pcm); return NULL; }
    int mid = H / 2, amp = H / 2 - 2; if (amp < 2) amp = 2;
    int peak = 1; for (size_t i = 0; i < frames * ch; i += 7) { int a = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (a > peak) peak = a; }
    for (int x = 0; x < W; x++) {
        size_t a0 = frames * x / W, a1 = frames * (x + 1) / W; if (a1 <= a0) a1 = a0 + 1; if (a1 > frames) a1 = frames;
        int lo = 0, hi = 0; size_t step = (a1 - a0) / 256 + 1;
        for (size_t i = a0; i < a1; i += step) { int v = 0; for (int c = 0; c < ch; c++) v += pcm[i * ch + c]; v /= ch; if (v < lo) lo = v; if (v > hi) hi = v; }
        int y0 = mid - hi * amp / peak, y1 = mid - lo * amp / peak; if (y1 < y0 + 1) y1 = y0 + 1;
        for (int y = 0; y < H; y++) {
            unsigned char *p = px + ((size_t)y * W + x) * 4;
            if (y >= y0 && y <= y1) { p[0] = 0x4F; p[1] = 0x5D; p[2] = 0xFF; p[3] = 255; }          // coral (premultiplied, opaque)
            else if (y == mid) { p[0] = p[1] = p[2] = 0x3A; p[3] = 255; }
        }
    }
    free(pcm);
    *ow = W; *oh = boxH;
    return px;
}
