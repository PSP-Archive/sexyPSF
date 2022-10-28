// ===========================================================================
//
// sexypsf for PSP
// Copyright (C) 2005 Weltall (weltall@consoleworld.org)(www.consoleworld.org)
// Copyright (C) 2005 Sumire Kinoshita
//
// 0.4.5-r1 (13/7/2005)
// 0.4.5-1.1 (27/9/2005)
// 0.4.5-1.1b (2/10/2005)
// 0.4.5-1.1c (4/11/2005)
//   - ファーストリリース。
//
// ===========================================================================

#include <pspkernel.h>
#include <pspthreadman.h>
#include <psppower.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspdebug.h>
#include <psphprm.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define _SPSF_TYPES_H__
#include <driver.h>
#include <psprtc.h>
#include <stdlib.h>
#include <time.h>
#include <pspsdk.h>

// ===========================================================================
//
// マクロの定義。
//
// ===========================================================================

// モジュール名   : sexypsf
// モジュール属性 : 無し
// バージョン     : 0.1
#ifndef BUILD20
PSP_MODULE_INFO("sexypsf", 0x1000, 1, 1);
#else
PSP_MODULE_INFO("sexypsf", 0x0, 1, 1);
#endif

// スレッド属性 : ユーザーモード / VFPU アクセス有効
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define MAX_ENTRY 1024 // 最大ファイル数。
#define MAX_PATH  1024 // 最大ファイル名。

#define SAMPLE_COUNT PSP_AUDIO_SAMPLE_ALIGN(1024) // ブロックあたりのサンプル数。
#define SAMPLE_SIZE  (SAMPLE_COUNT * 4)           // ブロックあたりのバイト数。
#define BUFFER_COUNT 8                            // バッファの個数。
#define BUFFER_SIZE  (SAMPLE_SIZE * 64)           // バッファのバイト数。

// ===========================================================================
//
// 列挙体の定義。
//
// ===========================================================================

// シーンを示す列挙体。
enum {
	SCENE_DUMMY = 0,
	SCENE_FILER, // ファイル選択。
	SCENE_PLAY   // 演奏。
};

// ===========================================================================
//
// 変数の定義。
//
// ===========================================================================

static int audio_thread_id;       // オーディオスレッドの ID。
static u8 audio_thread_exit_flag; // オーディオスレッドの終了フラグ。
static int sexy_thread_id;        // エミュレーションスレッドの ID。
static u8 sexy_thread_exit_flag;  // エミュレーションスレッドの終了フラグ。

static u8 sound_buffer[BUFFER_COUNT][BUFFER_SIZE]; // サウンドバッファ。
static u8 sound_buffer_ready[BUFFER_COUNT];        // サウンドバッファが満たされているか示すフラグ。
static u8 sound_read_index;                        // サウンドバッファの読み込みインデックス。
static u32 sound_read_offset;                      // サウンドバッファの読み込みオフセット。
static u8 sound_write_index;                       // サウンドバッファの書き込みインデックス。
static u32 sound_write_offset;                     // サウンドバッファの書き込みオフセット。
int audio_handle; // オーディオチャンネルのハンドル。

static int current_scene;      // 現在のシーン。
static int filer_scene_index;  // ファイル選択画面の表示インデックス。
static int filer_scene_select; // ファイル選択画面の選択インデックス。

static PSFINFO *psf_info;               // PSFINFO 構造体。
static char current_psf_path[MAX_PATH]; // 現在の PSF ファイルのパス。

static char current_directory[MAX_PATH];       // 現在のディレクトリ。
static SceIoDirent current_dirents[MAX_ENTRY]; // 現在のディレクトリのファイル一覧。
static int current_dirents_count;              // 現在のディレクトリのファイル数。

u8 enable_reverb; // リバーブの有効 / 無効フラグ。../spu/spu.c より参照される。
int thread_paused = 0; //tells if the play and sexypsf thread are suspended
int enable_shuffle = 0; //suffle is enabled if 1
int enable_timelimit = 1;
int enable_fade = 0;
/*used to count play time*/
u64 start_cputicks = 0;
u64 current_tick = 0;
//current time
int seconds = 0;
int minutes = 0;
int hours = 0;
//song defined time
int secondstot = 0;
int minutestot = 0;
int hourstot = 0;
//fade info
u64 fade_start_ticks = 0;
u64 fade_ticks = 0;
int into_fade = 0;
int fade_res = 0;

#ifndef BUILD20
__attribute__ ((constructor))
void loaderInit()
{
    pspKernelSetKernelPC();
    pspSdkInstallNoDeviceCheckPatch();
	pspDebugInstallErrorHandler(NULL);
    //pspDebugInstallKprintfHandler(NULL);
}
#endif
// ===========================================================================
//
// 関数の定義。
//
// ===========================================================================

// ---------------------------------------------------------------------------
// audio_thread
//
// オーディオスレッド。
//
// 引数 :
//   args : 未使用。
//   argp : 未使用。
//
// 戻り値 :
//   常に 0。
// ---------------------------------------------------------------------------
/*void	volume_boost_2( short* audio_data, int length )
{
	register int	l = length / 2;
	register short*	d = audio_data;

	while (l--)
	{
		register int	val = *d * 2;
		if (val > 32767)
			val = 32767;
		else if (val < 32768)
			val = -32768;
		*d++ = (short)val;
	}
}*/
static int audio_thread(SceSize args, void *argp)
{
	
	// オーディオチャンネルの取得。
	audio_handle = sceAudioChReserve(
		PSP_AUDIO_NEXT_CHANNEL, SAMPLE_COUNT, PSP_AUDIO_FORMAT_STEREO);
	sceAudioChangeChannelVolume(audio_handle, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX);
	// スレッドの終了フラグが立つまでループ。
	while(!audio_thread_exit_flag)
	{
		// ウェイト。(10 ms)
		sceKernelDelayThread(10000);
		
		// 対象サウンドバッファが満たされるまで待つ。
		if(!sound_buffer_ready[sound_read_index]) {
			continue;
		}
		// 対象サウンドバッファから 1 ブロック分出力する。
		sceAudioOutputPannedBlocking(
			audio_handle, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX,
			&sound_buffer[sound_read_index][sound_read_offset]);
		
		// 読み込みオフセットを進める。
		sound_read_offset += SAMPLE_SIZE;
		
		// 対象サウンドバッファから全て出力が済んだか？
		if(sound_read_offset >= BUFFER_SIZE) {
			// 次のサウンドバッファを参照するように切り替える。
			sound_buffer_ready[sound_read_index] = 0;
			sound_read_index++;
			sound_read_offset = 0;
			
			// 最後のサウンドバッファに達したか？
			if(sound_read_index >= BUFFER_COUNT) {
				// 最初のサウンドバッファを参照するように切り替える。
				sound_read_index = 0;
			}
		}
	}
	
	// 無音のブロックを出力する。次回再生時のプチノイズ防止用。
	sceAudioOutputPannedBlocking(audio_handle, 0, 0, sound_buffer[0]);
	
	// オーディオチャンネルの解放。
	sceAudioChRelease(audio_handle);
	
	// スレッドの終了。
	sceKernelExitThread(0);
	
	// 正常終了。
	return 0;
}

// ---------------------------------------------------------------------------
// sexy_thread
//
// エミュレーションスレッド。
//
// 引数 :
//   args : 未使用。
//   argp : 未使用。
//
// 戻り値 :
//   常に 0。
// ---------------------------------------------------------------------------
static int sexy_thread(SceSize args, void *argp)
{
	// 動作クロックを 333MHz に変更。
	scePowerSetClockFrequency(333, 333, 166);


	
	// エミュレーション開始。sexy_stop() が呼ばれるまで戻らない。
	// サンプルが生成された時点で sexyd_update() が呼ばれる。
	sexy_execute();
	
	// 動作クロックを 222MHz に変更。
	scePowerSetClockFrequency(222, 222, 111);

	
	// スレッドの終了。
	sceKernelExitThread(0);
	
	// 正常終了。
	return 0;
}

// ---------------------------------------------------------------------------
// sexyd_update
//
// エミュレーションでサンプルが生成されたら呼ばれる関数。
//
// 引数 :
//   buffer : 生成されたサンプル。(44100 Hz / 16 bit / Stereo)
//   length : buffer のバイト数。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
void sexyd_update(u8 *buffer, int length)
{
	int buffer_free_length; // 対象バッファの残りサイズ。
	
	// 対象サウンドバッファが空になるまで待つ。
	do
	{
		// ウェイト。(0.01 ms)
		sceKernelDelayThread(10);
		
		// スレッドの終了フラグが立っているか？
		if(sexy_thread_exit_flag) {
			// エミュレーションを停止させる。
			sexy_stop();
			// 正常終了。
			return;
		}
		
next:;
	} while(sound_buffer_ready[sound_write_index]);
	

	// 対象バッファの残りサイズを計算する。
	buffer_free_length = BUFFER_SIZE - sound_write_offset;
	
	// 対象バッファに全てのサンプルを書き込めるか？
	if(buffer_free_length >= length) {
		// 対象バッファに全てのサンプルを書き込む。
		memcpy(&sound_buffer[sound_write_index][sound_write_offset], buffer, length);
		
		// 書き込みオフセットを進める。
		sound_write_offset += length;
	} else {
		// 対象バッファの残りサイズ分だけサンプルを書き込む。
		memcpy(&sound_buffer[sound_write_index][sound_write_offset], buffer, buffer_free_length);

		
		// 書き込んだ分だけサンプルのバッファポインタとサイズを調整する。
		buffer += buffer_free_length;
		length -= buffer_free_length;
		
		// 次のサウンドバッファを参照するように切り替える。
		
		sound_buffer_ready[sound_write_index] = 1;
		sound_write_index++;
		sound_write_offset = 0;
		
		// 最後のサウンドバッファに達したか？
		if(sound_write_index >= BUFFER_COUNT) {
			// 最初のサウンドバッファを参照するように切り替える。
			sound_write_index = 0;
		}
		
		// 残りのサンプルを書き込むために、次のサウンドバッファが空になるのを待つ。
		goto next;
	}
}

// ---------------------------------------------------------------------------
// psf_play
//
// PSF ファイルの演奏開始。
//
// 引数 :
//   無し。
//
// 戻り値 :
//   成功時は 0、それ以外は -1。
// ---------------------------------------------------------------------------
static int psf_play()
{
	int i; // 汎用変数。
	thread_paused = 0;
	// PSF の読み込み。
	sceRtcGetCurrentTick(&start_cputicks);
	seconds = 0;
	minutes = 0;
	hours = 0;
	secondstot = 0;
	minutestot = 0;
	hourstot = 0;
	psf_info = sexy_load(current_psf_path);
	u32 temp = psf_info->stop/10;
	while(temp>=100)
	{
		secondstot += 1;
		temp -= 100;
		while(secondstot >= 60)
		{
			minutestot +=1;
			secondstot -= 60;
			while(minutestot >= 60)
			{
				hourstot += 1;
				minutestot -= 60;
			}
		}
	}

	
		



	// PSFINFO 構造体は有効か？
	if(psf_info) {
		// サウンドバッファの初期化。
		for(i = 0; i < BUFFER_COUNT; i++)
		{
			memset(sound_buffer[i], 0, BUFFER_SIZE);
			sound_buffer_ready[i] = 0;
		}
		
		// サウンドバッファの読み書きインデックス / オフセットの初期化。
		sound_read_index   = 0;
		sound_read_offset  = 0;
		sound_write_index  = 0;
		sound_write_offset = 0;
		
		// オーディオスレッドの生成と開始。
		audio_thread_id = sceKernelCreateThread("audio_thread", audio_thread, 18, 65536, 0, 0);
		audio_thread_exit_flag = 0;
		sceKernelStartThread(audio_thread_id, 0, 0);
		
		// エミュレーションスレッドの生成と開始。
		sexy_thread_id = sceKernelCreateThread("sexy_thread", sexy_thread, 32, 65536, 0, 0);
		sexy_thread_exit_flag  = 0;
		sceKernelStartThread(sexy_thread_id, 0, 0);
		
		// 正常終了。
		return 0;
	} else {
		// エラー終了。
		return -1;
	}
}

// ---------------------------------------------------------------------------
// psf_stop
//
// PSF ファイルの演奏停止。
//
// 引数 :
//   無し。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void psf_stop()
{
	// PSFINFO 構造体は有効か？
	if(thread_paused == 1)
		{
			scePowerSetCpuClockFrequency(333);
			scePowerSetBusClockFrequency(166);
			sceKernelResumeThread(sexy_thread_id);
			sceKernelResumeThread(audio_thread_id);
			//	printf_line(1,"unpaused");
			thread_paused = 0;
		}
	if(psf_info) {
		// エミュレーションスレッドの停止と破棄。
		sexy_thread_exit_flag = 1;
		sceKernelWaitThreadEnd(sexy_thread_id, 0);
		sceKernelDeleteThread(sexy_thread_id);
		
		// オーディオスレッドの停止と破棄。
		audio_thread_exit_flag = 1;
		sceKernelWaitThreadEnd(audio_thread_id, 0);
		sceKernelDeleteThread(audio_thread_id);
		
		// PSFINFO 構造体の破棄。
		sexy_freepsfinfo(psf_info);
		psf_info = 0;
		thread_paused = 0;

	}
}

// ---------------------------------------------------------------------------
// set_dirents
//
// ディレクトリ一覧の取得。
//
// 引数 :
//   無し。
//
// 戻り値 :
//   成功時は 0、それ以外は -1。
// ---------------------------------------------------------------------------
static int set_dirents()
{
	SceUID uid;         // ディレクトリエントリの ID。
	SceIoDirent dirent; // ワーク用の SceIoDirent。
	char *extention;    // ファイルの拡張子へのポインタ。
	int r;              // 汎用変数。

	// ディレクトリエントリの取得。
	uid = sceIoDopen(current_directory);
	
	// ディレクトリエントリが無効か？
	if(uid < 0) {
		// エラー終了。
		return -1;
	}
	
	// ファイル一覧の取得。
	for(current_dirents_count = 0; current_dirents_count < MAX_ENTRY;)
	{
		// ワークを初期化。初期化しなければ sceIoDread がフリーズする。
		memset(&dirent, 0, sizeof(SceIoDirent));
		
		// エントリ情報をワークへ読み込み。
		r = sceIoDread(uid, &dirent);
		
		// ディレクトリか？
		if(FIO_SO_ISDIR(dirent.d_stat.st_attr)) {
			// エントリ情報をワークから現在のディレクトリ一覧へコピー。
			memcpy(&current_dirents[current_dirents_count], &dirent, sizeof(SceIoDirent));
			
			// 現在のディレクトリのファイル数をインクリメント。
			current_dirents_count++;
		} else {
			// ファイル名から拡張子を取り出す。
			extention = strrchr(dirent.d_name, '.');
			
			// 拡張子が存在するか？
			if(extention) {
				// サポートする拡張子か？
				if(!stricmp(extention, ".psf") || !stricmp(extention, ".psf1") || !stricmp(extention, ".minipsf")) {
					// エントリ情報をワークから現在のディレクトリ一覧へコピー。
					memcpy(&current_dirents[current_dirents_count], &dirent, sizeof(SceIoDirent));
					
					// 現在のディレクトリのファイル数をインクリメント。
					current_dirents_count++;
				}
			}
		}
		
		// 最後のエントリか？
		if(r <= 0) {
			break;
		}
	}
	
	// ディレクトリエントリの解放。
	sceIoDclose(uid);
	
	// 正常終了。
	return 0;
}

// ---------------------------------------------------------------------------
// printf_line
//
// 指定行に対するフォーマット済み文字列の表示。
//
// 引数 :
//   y      : 行数。
//   format : フォーマット文字列。
//   ...    : フォーマット引数。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void printf_line(int y, char *format, ...)
{
	va_list options; // 取り出した可変長引数。
	char buffer[68]; // 文字列用バッファ。68 は 1 行に表示可能な文字数。
	int r;           // 汎用変数。
	
	// 可変長引数を取り出す。
	va_start(options, format);
	
	// フォーマット済み文字列を得る。
	r = vsnprintf(buffer, 68, format, options);
	
	// フォーマット済み文字列は 68 文字を超えていないか？
	if(r >= 0) {
		// 残った部分をスペースで埋める。
		memset(&buffer[r], ' ', 68 - r);
		
		// ターミネートする。
		buffer[67] = '\0';
	}
	
	// カーソル位置を設定する。
	pspDebugScreenSetXY(0, y);
	
	// フォーマット済み文字列を表示する。
	pspDebugScreenPrintf(buffer);
}

// ---------------------------------------------------------------------------
// filer_draw
//
// ファイル選択画面の表示。。
//
// 引数 :
//   無し。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void filer_draw()
{
	int i, j; // 汎用変数。
	
	// ファイル選択画面の表示。
	printf_line(0, current_directory);
	printf_line(1, "===== File selector ===============================================");
	
	for(i = 2; i < 32; i++)
	{
		// ファイル一覧に対するインデックスを求める。
		j = i - 2 + filer_scene_index;
		
		// ファイルが存在するか？
		if(j < current_dirents_count) {
			printf_line(i, "%s %s%s",
				// このファイルが選択されているならば、カーソルを付加。
				(j == filer_scene_select) ? ">" : " ",
				// ファイル名。
				current_dirents[j].d_name,
				// ディレクトリであれば、末尾にセパレータを付加。
				FIO_SO_ISDIR(current_dirents[j].d_stat.st_attr) ? "/" : ""
			);
		} else {
			// 空白行を表示。
			printf_line(i, "");
		}
	}
	
	printf_line(32, "===================================================================");
	printf_line(33, "Cursor = Move / Circle = Play / Triangle = Toggle reverb        |%s|",
		// リバーブが有効かどうか示すインジケータ。
		enable_reverb ? "R" : " "
	);
}

// ---------------------------------------------------------------------------
// play_draw
//
// 演奏画面の表示。。
//
// 引数 :
//   無し。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void play_draw()
{
	int i; // 汎用変数。
	
	// 演奏画面の表示。
	printf_line( 0, current_dirents[filer_scene_select].d_name);
	printf_line( 1, "===== PSF information ===============================%s========", thread_paused ? "PAUSED" : "======");
//	printf_line( 1, "===== PSF information =============================================");
	printf_line( 2, "Game      : %s", psf_info->game      ? psf_info->game      : "");
	printf_line( 3, "Title     : %s", psf_info->title     ? psf_info->title     : "");
	printf_line( 4, "Artist    : %s", psf_info->artist    ? psf_info->artist    : "");
	printf_line( 5, "Year      : %s", psf_info->year      ? psf_info->year      : "");
	printf_line( 6, "Genre     : %s", psf_info->genre     ? psf_info->genre     : "");
	printf_line( 7, "PSF by    : %s", psf_info->psfby     ? psf_info->psfby     : "");
	printf_line( 8, "Copyright : %s", psf_info->copyright ? psf_info->copyright : "");
	
	for(i = 9; i < 30; i++)
	{
		// 空白行を表示。
		printf_line(i, "");
	}
	//printf_line(31, "%3d:%2d:%2d - %dms -f: %dms",hourstot,minutestot,secondstot,psf_info->length, psf_info->fade);
	printf_line(30, "%3d:%2d:%2d",hours,minutes,seconds);
	printf_line(31,"%3d:%2d:%2d",hourstot,minutestot,secondstot);
	printf_line(32, "===================================================================");
	printf_line(33, "Cross = Stop / Triangle = Toggle reverb                     |%s|%s|%s|",
		// リバーブが有効かどうか示すインジケータ。
		enable_timelimit ? "T" : " ",enable_shuffle ? "S" : " ",enable_reverb ? "R" : " "
	);
}

// ---------------------------------------------------------------------------
// filer_scene
//
// ファイル選択画面のループ処理。
//
// 引数 :
//   buttons : 押されているボタン。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void filer_scene(int buttons)
{
	int i; // 汎用変数。
	
	// カーソルボタンが押されたか？
	if(buttons & (PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT)) {
		// カーソルボタンの方向に応じた移動量を求める。
		if(buttons & PSP_CTRL_UP) {
			// 1 インデックス前へ移動。
			i = -1;
		} else if(buttons & PSP_CTRL_RIGHT) {
			// 10 インデックス次へ移動。
			i = 10;
		} else if(buttons & PSP_CTRL_DOWN) {
			// 1 インデックス次へ移動。
			i = 1;
		} else {
			// 10 インデックス前へ移動。
			i = -10;
		}
		
		// 選択インデックスを移動させる。
		filer_scene_select += i;
		
		// 選択インデックスがファイル数と同じか大きくなったか？
		if(filer_scene_select >= current_dirents_count) {
			// 選択インデックスをクリップする。
			filer_scene_select = current_dirents_count - 1;
		// 選択インデックスが 0 より小さくなったか？
		} else if(filer_scene_select < 0) {
			// 選択インデックスをクリップする。
			filer_scene_select = 0;
		}
		
		// 表示インデックスが選択インデックスより大きくなったか？
		if(filer_scene_index > filer_scene_select) {
			// 表示インデックスを移動させる。
			filer_scene_index += i;
			
			// 表示インデックスが 0 より小さくなったか？
			if(filer_scene_index < 0) {
				// 表示インデックスをクリップする。
				filer_scene_index = 0;
			}
		// 表示インデックス + 30 が選択インデックスと同じか小さくなったか？
		} else if(filer_scene_index + 30 <= filer_scene_select) {
			// 表示インデックスを移動させる。
			filer_scene_index += i;
			
			// 表示インデックス + 30 がファイル数と同じか大きくなったか？
			if(filer_scene_index + 30 >= current_dirents_count) {
				// 表示インデックスをクリップする。
				filer_scene_index = current_dirents_count - 30;
			}
		}
		
		// 画面を更新する。
		filer_draw();
	// ○ボタンが押されたか？
	} else if(buttons & PSP_CTRL_CIRCLE) {
		// ディレクトリか？
		if(FIO_SO_ISDIR(current_dirents[filer_scene_select].d_stat.st_attr)) {
			// ディレクトリ名の 1 バイト目がドットか？
			if(current_dirents[filer_scene_select].d_name[0] == '.') {
				// ディレクトリ名の 2 バイト目がドットか？
				if(current_dirents[filer_scene_select].d_name[1] == '.') {
					// 現在のディレクトリを変更する。(親ディレクトリへ)
					current_directory[strlen(current_directory) - 1] = '\0';
					*(strrchr(current_directory, '/') + 1) = '\0';
				}
			} else {
				// 現在のディレクトリを変更する。(名前のディレクトリへ)
				strcat(current_directory, current_dirents[filer_scene_select].d_name);
				strcat(current_directory, "/");
			}
			
			// ディレクトリ一覧を更新する。
			set_dirents();
			// 表示インデックスを初期化。
			filer_scene_index  = 0;
			// 選択インデックスを初期化。
			filer_scene_select = 0;
			// 画面を更新する。
			filer_draw();
		} else {
			// PSF ファイルのパスを生成する。
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			
			// 演奏を開始する。正常に開始されたか？
			if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
			}
		}
	// △ボタンが押されたか？
	} else if(buttons & PSP_CTRL_TRIANGLE) {
		// リバーブの有効 / 無効を切り替える。
		enable_reverb ^= 1;
		// 画面を更新する。
		filer_draw();
	} else if(buttons & PSP_CTRL_SQUARE) {
		int i;
		printf_line(0,"sexypsf 0.4.5-r1 PSP 1.1d\n");
		printf_line(1, "===================================================================");
		for(i = 2; i<11; i++)
		{
			printf_line(i,"");
		}
		printf_line(11,"                Coded by weltall  (weltall@consoleworld.org)\n");
        printf_line(12,"     Coded and ported by yaneurao\n");
		printf_line(13,"                  gfx by c0d3x\n");
		printf_line(14,"");
		printf_line(15,"               thanks to pspdev community!\n");
		for(i = 16; i < 25; i++)
		{
			printf_line(i, "");
		}
		printf_line(25,"                         www.consoleworld.org\n");
		for(i = 26; i < 32; i++)
		{
			printf_line(i, "");
		}
		printf_line(32, "===================================================================");
		}
	}

// ---------------------------------------------------------------------------
// filer_scene_remote
//
// ファイル選択画面のループ処理。
//
// 引数 :
//   buttons : 押されているボタン。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void filer_scene_remote(u32 buttons)
{
	#define PSP_HPRM_VOL_UP_DOWN 0x30
	int i = 0; // 汎用変数。
	
	// カーソルボタンが押されたか？
	if(buttons & (PSP_HPRM_FORWARD | PSP_HPRM_BACK)) {
		// カーソルボタンの方向に応じた移動量を求める。
		if(buttons & PSP_HPRM_BACK) {
			// 1 インデックス前へ移動。
			i = -1;
		} else if(buttons & PSP_HPRM_FORWARD) {
			// 1 インデックス次へ移動。
			i = 1;
		}
		
		// 選択インデックスを移動させる。
		filer_scene_select += i;
		
		// 選択インデックスがファイル数と同じか大きくなったか？
		if(filer_scene_select >= current_dirents_count) {
			// 選択インデックスをクリップする。
			filer_scene_select = current_dirents_count - 1;
		// 選択インデックスが 0 より小さくなったか？
		} else if(filer_scene_select < 0) {
			// 選択インデックスをクリップする。
			filer_scene_select = 0;
		}
		
		// 表示インデックスが選択インデックスより大きくなったか？
		if(filer_scene_index > filer_scene_select) {
			// 表示インデックスを移動させる。
			filer_scene_index += i;
			
			// 表示インデックスが 0 より小さくなったか？
			if(filer_scene_index < 0) {
				// 表示インデックスをクリップする。
				filer_scene_index = 0;
			}
		// 表示インデックス + 30 が選択インデックスと同じか小さくなったか？
		} else if(filer_scene_index + 30 <= filer_scene_select) {
			// 表示インデックスを移動させる。
			filer_scene_index += i;
			
			// 表示インデックス + 30 がファイル数と同じか大きくなったか？
			if(filer_scene_index + 30 >= current_dirents_count) {
				// 表示インデックスをクリップする。
				filer_scene_index = current_dirents_count - 30;
			}
		}
		
		// 画面を更新する。
		filer_draw();
	// ○ボタンが押されたか？
	} else if(buttons & PSP_HPRM_PLAYPAUSE) {
		// ディレクトリか？
		if(FIO_SO_ISDIR(current_dirents[filer_scene_select].d_stat.st_attr)) {
			// ディレクトリ名の 1 バイト目がドットか？
			if(current_dirents[filer_scene_select].d_name[0] == '.') {
				// ディレクトリ名の 2 バイト目がドットか？
				if(current_dirents[filer_scene_select].d_name[1] == '.') {
					// 現在のディレクトリを変更する。(親ディレクトリへ)
					current_directory[strlen(current_directory) - 1] = '\0';
					*(strrchr(current_directory, '/') + 1) = '\0';
				}
			} else {
				// 現在のディレクトリを変更する。(名前のディレクトリへ)
				strcat(current_directory, current_dirents[filer_scene_select].d_name);
				strcat(current_directory, "/");
			}
			
			// ディレクトリ一覧を更新する。
			set_dirents();
			// 表示インデックスを初期化。
			filer_scene_index  = 0;
			// 選択インデックスを初期化。
			filer_scene_select = 0;
			// 画面を更新する。
			filer_draw();
		} else {
			// PSF ファイルのパスを生成する。
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			
			// 演奏を開始する。正常に開始されたか？
			if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
			}
		}
	// △ボタンが押されたか？
	} else if(buttons & PSP_HPRM_VOL_UP_DOWN) {
		// リバーブの有効 / 無効を切り替える。
		enable_reverb ^= 1;
		// 画面を更新する。
		filer_draw();
	}
}

// ---------------------------------------------------------------------------
// play_scene
//
// 演奏画面のループ処理。
//
// 引数 :
//   buttons : 押されているボタン。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void play_scene(int buttons)
{
	// ○ボタンが押されたか？
	if(buttons & PSP_CTRL_CROSS) {
		// 演奏を停止する。
		psf_stop();
		// 現在のシーンを「ファイル選択」に設定。
		current_scene = SCENE_FILER;
		// 画面を更新する。
		filer_draw();
	// △ボタンが押されたか？
	} else if(buttons & PSP_CTRL_SELECT) {
		if( enable_shuffle == 0)
			enable_shuffle = 1;
		else
			enable_shuffle = 0;

		play_draw();
	} else if(buttons & PSP_CTRL_START) {
	
	/*	//discarded, sorry but buttons are over :P
		srand(time(NULL));
		filer_scene_select = rand() % current_dirents_count-1;
			psf_stop();
			filer_scene_index = filer_scene_select;
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
				if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
				}*/
		if(enable_timelimit == 0)
			enable_timelimit = 1;
		else
			enable_timelimit = 0;
		play_draw();

			

	} else if(buttons & PSP_CTRL_SQUARE){
	
		printf_line( 9, "cpu       : %d", scePowerGetCpuClockFrequencyInt());
		printf_line(10, "bus       : %d", scePowerGetBusClockFrequencyInt());

	} else if(buttons & PSP_CTRL_TRIANGLE) {
		// リバーブの有効 / 無効を切り替える。
		enable_reverb ^= 1;
		// 画面を更新する。
		play_draw();
	} else if(buttons & PSP_CTRL_LTRIGGER) {
		if(enable_shuffle == 1)
		{
					srand(time(NULL));
		filer_scene_select = rand() % current_dirents_count-1;
			psf_stop();
			filer_scene_index = filer_scene_select;

			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
				if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
				}
		}
		else
		{
		filer_scene_select--;
		if(filer_scene_select <0 || FIO_S_ISREG(current_dirents[filer_scene_select].d_stat.st_mode) != 1)//filer_scene_select < 2)
		{
			filer_scene_select++;
		}
		else
		{
			psf_stop();
			
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			if(filer_scene_index > filer_scene_select) {
			filer_scene_index -= 1;

				if(filer_scene_index < 0) 
				{
					filer_scene_index = 0;
				}
			}
			// 演奏を開始する。正常に開始されたか？
			if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
			}
		}
		}
	} else if(buttons & PSP_CTRL_RTRIGGER) {
		if(enable_shuffle == 1)
		{
					srand(time(NULL));
		filer_scene_select = rand() % current_dirents_count-1;
			psf_stop();
			filer_scene_index = filer_scene_select;

			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
				if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
				}
		}
		else
		{
			filer_scene_select++;
			if(filer_scene_select >= current_dirents_count)
			{
				filer_scene_select = current_dirents_count - 1;
			}
			else
			{
			psf_stop();
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			if(filer_scene_index < filer_scene_select) {
			filer_scene_index += 1;	
			}
			// 演奏を開始する。正常に開始されたか？
			if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
			
				}
			}
		}
	} else if(buttons & PSP_CTRL_CIRCLE) {
		if(thread_paused == 0)
		{
			sceKernelSuspendThread(sexy_thread_id);
			sceKernelSuspendThread(audio_thread_id);
		//	printf_line(1,"paused");
			scePowerSetCpuClockFrequency(33);
			scePowerSetBusClockFrequency(33);
			thread_paused = 1;

		}
		else if(thread_paused == 1)
		{
			scePowerSetCpuClockFrequency(333);
			scePowerSetBusClockFrequency(166);
			sceKernelResumeThread(sexy_thread_id);
			sceKernelResumeThread(audio_thread_id);
		//	printf_line(1,"unpaused");
			thread_paused = 0;
		}
		play_draw();
	}

}

// ---------------------------------------------------------------------------
// play_scene_remote
//
// 演奏画面のループ処理。
//
// 引数 :
//   buttons : 押されているボタン。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void play_scene_remote(u32 buttons)
{
#define PSP_HPRM_VOL_UP_DOWN 0x30
	// ○ボタンが押されたか？
	if(buttons & PSP_HPRM_PLAYPAUSE) {
		//printf_line(2,"thread:%d", thread_paused);
		if(thread_paused == 0)
		{
			sceKernelSuspendThread(sexy_thread_id);
			sceKernelSuspendThread(audio_thread_id);
			
			scePowerSetCpuClockFrequency(33);
			scePowerSetBusClockFrequency(33);
		//	printf_line(1,"paused");
			thread_paused = 1;

		}
		else if(thread_paused == 1)
		{
			scePowerSetCpuClockFrequency(333);
			scePowerSetBusClockFrequency(166);
			sceKernelResumeThread(sexy_thread_id);
			sceKernelResumeThread(audio_thread_id);
		//	printf_line(1,"unpaused");
			thread_paused = 0;
		}
	//		printf_line(0," ");

		play_draw();
		/*// 演奏を停止する。
		psf_stop();
		// 現在のシーンを「ファイル選択」に設定。
		current_scene = SCENE_FILER;
		// 画面を更新する。
		filer_draw();
	// △ボタンが押されたか？*/
	} else if(buttons == 48) {//& PSP_HPRM_VOL_UP_DOWN) {
				// リバーブの有効 / 無効を切り替える。
		enable_reverb ^= 1;
		// 画面を更新する。
		play_draw();
		
	}	else if(buttons & PSP_HPRM_FORWARD) {
		if(enable_shuffle == 1)
		{
					srand(time(NULL));
		filer_scene_select = rand() % current_dirents_count-1;
					filer_scene_index = filer_scene_select;

			psf_stop();
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
				if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
				}
		}
		else
		{

			filer_scene_select++;
			if(filer_scene_select >= current_dirents_count)
			{
				filer_scene_select = current_dirents_count - 1;
			}
			else
			{
			psf_stop();
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			if(filer_scene_index < filer_scene_select) {
			filer_scene_index += 1;		
			}
			// 演奏を開始する。正常に開始されたか？
			if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
			
			}
			}
		}
	} else if(buttons & PSP_HPRM_BACK) {
		if(enable_shuffle == 1)
		{
					srand(time(NULL));
		filer_scene_select = rand() % current_dirents_count-1;
			psf_stop();
						filer_scene_index = filer_scene_select;

			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
				if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
				}
		}
		else
		{
			filer_scene_select--;
		if(filer_scene_select <0 || FIO_S_ISREG(current_dirents[filer_scene_select].d_stat.st_mode) != 1)//filer_scene_select < 2)
		{
			filer_scene_select++;
		}
		else
		{
			psf_stop();
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			if(filer_scene_index > filer_scene_select) {

				filer_scene_index -= 1;
			
				if(filer_scene_index < 0) 
				{

					filer_scene_index = 0;
				}
			}
			// 演奏を開始する。正常に開始されたか？
			if(psf_play() >= 0) {
				// 現在のシーンを「演奏」に設定。
				current_scene = SCENE_PLAY;
				// 画面を更新する。
				play_draw();
			}
		}
		}
	}
}

// ---------------------------------------------------------------------------
// exit_callback
//
// 終了コールバック。
//
// 引数 :
//   無し。
//
// 戻り値 :
//   常に 0。
// ---------------------------------------------------------------------------
static int exit_callback()
{
	// 演奏の停止。
	psf_stop();
	
	// ソフトウェアの終了。
	sceKernelExitGame();
	
	// 正常終了。
	return 0;
}

// ---------------------------------------------------------------------------
// power_callback
//
// 電源コールバック。
//
// 引数 :
//   unknown   : 未使用。
//   powerInfo : 未使用。
//
// 戻り値 :
//   無し。
// ---------------------------------------------------------------------------
static void power_callback(int unknown, int powerInfo)
{
}

// ---------------------------------------------------------------------------
// callback_thread
//
// 通知スレッド。
//
// 引数 :
//   args : 未使用。
//   argp : 未使用。
//
// 戻り値 :
//   常に 0。
// ---------------------------------------------------------------------------
static int callback_thread(SceSize args, void *argp)
{
	int exit_callback_id;  // 終了コールバックの ID。
	int power_callback_id; // 電源コールバックの ID。
	
	// 終了コールバックの生成と登録。
	exit_callback_id = sceKernelCreateCallback("Exit Callback", exit_callback, 0);
	sceKernelRegisterExitCallback(exit_callback_id);
	
	// 電源コールバックの生成と登録。
	power_callback_id = sceKernelCreateCallback("Power Callback", power_callback, 0);
	scePowerRegisterCallback(0, power_callback_id);
	
	// スレッドをスリープする。
	sceKernelSleepThreadCB();
	
	// 正常終了。
	return 0;
}

// ---------------------------------------------------------------------------
// main
//
// メイン。
//
// 引数 :
//   argc : 未使用。
//   argv : 未使用。
//
// 戻り値 :
//   常に 0。
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	int callback_thread_id; // 通知スレッドの ID。
	SceCtrlData ctrl_data;  // コントローラーの状態。
	int buttons_prev;       // 前回のボタンの状態。
	u32 remoteButtons, remoteButtonsOld = 0;
	
	// 通知スレッドの生成と開始。
	
	callback_thread_id = sceKernelCreateThread("update_thread", callback_thread, 17, 4000, 0, 0);
	sceKernelStartThread(callback_thread_id, 0, 0);
	

	
	// デバッグスクリーンの初期化。
	pspDebugScreenInit();
	//pspDebugInstallErrorHandler(NULL);
	// コントローラーのサンプリングサイクルとモードの設定。
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingCycle(PSP_CTRL_MODE_DIGITAL);
	
	// PSFINFO 構造体の初期化。
	psf_info      = 0;
	// リバーブを無効にする。
	enable_reverb = 0;
	// 前回のボタンの状態を初期化。
	buttons_prev  = 0;
	
	// 現在のシーンを「ファイル選択」に設定。
	current_scene = SCENE_FILER;
	// 現在のディレクトリを「ms0:/」に設定。
	strcpy(current_directory, "ms0:/");
	// ディレクトリ一覧を更新する。
	set_dirents();
	// 表示インデックスを初期化。
	filer_scene_index  = 0;
	// 選択インデックスを初期化。
	filer_scene_select = 0;
	// 画面を更新する。
	filer_draw();
	
	// メインループ。
	while(1)
	{

		// ウェイト。(100 ms)
		sceKernelDelayThread(100000);
		
		// コントローラーの状態を取得。
		sceCtrlReadBufferPositive(&ctrl_data, 1);
		
		// 前回と押されているボタンの状態が同じか？
		if(ctrl_data.Buttons == buttons_prev) {
			// カーソルボタン以外は押されていないものとする。
			ctrl_data.Buttons &= (PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT);
		} else {
			// 現在のボタンの状態を保存する。
			buttons_prev = ctrl_data.Buttons;
		}
		
		// 現在のシーンに応じた関数を呼び出す。
		switch(current_scene)
		{
		case SCENE_FILER:
			{	// ファイル選択。
			filer_scene(ctrl_data.Buttons);
				remoteButtonsOld = remoteButtons;
				sceHprmPeekCurrentKey(&remoteButtons);
			//	printf_line(0,"r:%d", remoteButtons);
				if (remoteButtons != remoteButtonsOld && !(remoteButtons & PSP_HPRM_HOLD))
				{
					filer_scene_remote(remoteButtons);
				}
			break;
			}
		case SCENE_PLAY:
			{
			// 演奏。
				//used to count time and show
			sceRtcGetCurrentTick(&current_tick);
			if(current_tick-start_cputicks >= sceRtcGetTickResolution())
			{
				sceRtcGetCurrentTick(&start_cputicks);
				if(thread_paused == 0)
				{
					seconds += 1;
					if(seconds >= 60)
					{
						minutes += 1;
						seconds = 0;
						if(minutes >= 60)
						{					
							hours +=1;//ok, i know, this *can* overflow but i think the psp burn before this happens XD
							minutes = 0;
						}
					}
				}
			}
			printf_line(30, "%3d:%2d:%2d",hours,minutes,seconds);
			if(enable_fade == 0 && enable_timelimit == 1 && hours >= hourstot && minutes >= minutestot && seconds >= secondstot)
			{
				play_scene(PSP_CTRL_RTRIGGER);
			}
			//fade still dont' work :(
			/*else if(enable_fade == 1 && enable_timelimit == 1 && hours >= hourstot && minutes >= minutestot && seconds >= secondstot)
			{

				if(into_fade == 1)
				{
							sceKernelDelayThread(300000);

					sceRtcGetCurrentTick(&fade_ticks);
					if(fade_ticks-fade_start_ticks >= fade_res)
					{
									play_scene(PSP_CTRL_RTRIGGER);
					}
					//sceAudioChangeChannelVolume(audio_handle, (PSP_AUDIO_VOLUME_MAX/fade_res)*(fade_ticks-fade_start_ticks), (PSP_AUDIO_VOLUME_MAX/fade_res)*(fade_ticks-fade_start_ticks));
					sceAudioChangeChannelVolume(audio_handle, PSP_AUDIO_VOLUME_MAX/20, PSP_AUDIO_VOLUME_MAX/20);
				}
				else if(into_fade == 0) //first time here eh? :D
				{
					sceRtcGetCurrentTick(&fade_start_ticks);
					fade_res = (psf_info->fade/10)*(sceRtcGetTickResolution()/100);//fade total ticks duration
					into_fade = 1;
				}
			}*/
		//	else
			play_scene(ctrl_data.Buttons);
			//if (sceHprmIsRemoteExist > 0)
			//{
				remoteButtonsOld = remoteButtons;
				sceHprmPeekCurrentKey(&remoteButtons);
			//	printf_line(0,"r:%d", remoteButtons);
				if (remoteButtons != remoteButtonsOld && !(remoteButtons & PSP_HPRM_HOLD))
				{
					play_scene_remote(remoteButtons);
			//		printf_line(0,"r:%d", remoteButtons);
			
				}
			//}
			}
		}
	}
	
	// 正常終了。
	return 0;
}
