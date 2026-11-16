@echo off
rem Builds ns2dig.exe on this PC from the source in this folder (needs Python; installs the
rem Zig compiler with pip the first time). A program built on your own PC isn't a "downloaded" file,
rem so Windows Defender/SmartScreen treat it far less suspiciously.
setlocal
cd /d "%~dp0"
set PY=python
where python >nul 2>nul || set PY=py
echo Installing / updating the Zig compiler (pip install ziglang)...
%PY% -m pip install --upgrade ziglang || goto :err
set CC=%PY% -m ziglang cc -target x86_64-windows-gnu -O2
echo Compiling zstd (takes a minute)...
%CC% -DZSTD_DISABLE_ASM -w -c zstd.c -o zstd_win.o || goto :err
echo Compiling the VP9 video decoder (libvpx, takes a minute)...
pushd vpx
for %%f in (vpx_scale\vpx_scale_rtcd.c vpx_mem\vpx_mem.c vp9\vp9_dx_iface.c vp9\vp9_iface_common.c vp9\decoder\vp9_decodeframe.c vp9\decoder\vp9_decoder.c vp9\decoder\vp9_job_queue.c vp9\decoder\vp9_detokenize.c vp9\decoder\vp9_dsubexp.c vp9\decoder\vp9_decodemv.c vpx_scale\generic\yv12config.c vpx_scale\generic\yv12extend.c vpx_scale\generic\vpx_scale.c vpx_scale\generic\gen_scalers.c vpx\src\vpx_decoder.c vpx\src\vpx_encoder.c vpx\src\vpx_codec.c vpx\src\vpx_image.c vpx_util\vpx_thread.c vpx_util\vpx_write_yuv_frame.c vp9\common\vp9_scale.c vp9\common\vp9_common_data.c vp9\common\vp9_reconintra.c vp9\common\vp9_idct.c vp9\common\vp9_quant_common.c vp9\common\vp9_blockd.c vp9\common\vp9_seg_common.c vp9\common\vp9_tile_common.c vp9\common\vp9_scan.c vp9\common\vp9_entropymode.c vp9\common\vp9_mvref_common.c vp9\common\vp9_filter.c vp9\common\vp9_loopfilter.c vp9\common\vp9_alloccommon.c vp9\common\vp9_reconinter.c vp9\common\vp9_entropymv.c vp9\common\vp9_frame_buffers.c vp9\common\vp9_entropy.c vp9\common\vp9_thread_common.c vp9\common\vp9_pred_common.c vp9\common\vp9_rtcd.c vpx_dsp\inv_txfm.c vpx_dsp\bitreader.c vpx_dsp\skin_detection.c vpx_dsp\bitreader_buffer.c vpx_dsp\loopfilter.c vpx_dsp\intrapred.c vpx_dsp\vpx_convolve.c vpx_dsp\prob.c vpx_dsp\vpx_dsp_rtcd.c vpx_config.c) do %CC% -w -I. -c %%f -o %%~nf.o || (popd & goto :err)
%PY% -m ziglang ar rcs ..\libvpx_win.a *.o || (popd & goto :err)
del *.o >nul 2>nul
popd
echo Compiling the 3D view and sound player...
%CC% -municode -Wall -Wno-macro-redefined -c vox3d_win.c -o vox3d_win.o || goto :err
%CC% -municode -Wall -Wno-macro-redefined -c snd_win.c -o snd_win.o || goto :err
echo Compiling the texture viewer and decoders...
%CC% -municode -Wall -Wno-macro-redefined -c tex_win.c -o tex_win.o || goto :err
%CC% -municode -Wall -Wno-macro-redefined -c ui_skin.c -o ui_skin.o || goto :err
echo Compiling the layout, font, lighting and event viewers...
%CC% -municode -Wall -Wno-macro-redefined -c lyt_win.c -o lyt_win.o || goto :err
%CC% -municode -Wall -Wno-macro-redefined -c font_view.c -o font_view.o || goto :err
%CC% -municode -Wall -Wno-macro-redefined -c lgt_view.c -o lgt_view.o || goto :err
%CC% -municode -Wall -Wno-macro-redefined -c evt_view.c -o evt_view.o || goto :err
%CC% -municode -Wall -Wno-macro-redefined -Ivpx -c vid_view.c -o vid_view.o || goto :err
%PY% -m ziglang c++ -target x86_64-windows-gnu -O2 -w -It2d -c t2d\astc.cpp -o t2d_astc.o || goto :err
%PY% -m ziglang c++ -target x86_64-windows-gnu -O2 -w -It2d -c t2d\bcn.cpp -o t2d_bcn.o || goto :err
%PY% -m ziglang c++ -target x86_64-windows-gnu -O2 -w -It2d -c t2d\t2d_c.cpp -o t2d_c.o || goto :err
echo Linking...
%PY% -m ziglang c++ -target x86_64-windows-gnu -O2 -municode -Wl,--subsystem,windows -Wl,--gc-sections -Wall -Wno-macro-redefined -x c alsarc_gui.c -x none vox3d_win.o snd_win.o tex_win.o lyt_win.o font_view.o lgt_view.o evt_view.o vid_view.o ui_skin.o libvpx_win.a t2d_astc.o t2d_bcn.o t2d_c.o zstd_win.o app.rc -o ..\ns2dig.exe -lcomdlg32 -lshell32 -lshlwapi -lole32 -lcomctl32 -lgdi32 -luser32 -lkernel32 -lopengl32 -lwinmm -lmsimg32 || goto :err
del libvpx_win.a zstd_win.o vox3d_win.o snd_win.o tex_win.o lyt_win.o font_view.o lgt_view.o evt_view.o vid_view.o ui_skin.o t2d_astc.o t2d_bcn.o t2d_c.o >nul 2>nul
copy /y ns2dig.py ..\ns2dig.py >nul
echo.
echo Built: %~dp0..\ns2dig.exe
pause
exit /b 0
:err
echo.
echo Build failed - see the messages above.
pause
exit /b 1
