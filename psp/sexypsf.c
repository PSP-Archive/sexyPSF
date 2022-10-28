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
//   - �ե������ȥ�꡼����
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
// �ޥ���������
//
// ===========================================================================

// �⥸�塼��̾   : sexypsf
// �⥸�塼��°�� : ̵��
// �С������     : 0.1
#ifndef BUILD20
PSP_MODULE_INFO("sexypsf", 0x1000, 1, 1);
#else
PSP_MODULE_INFO("sexypsf", 0x0, 1, 1);
#endif

// ����å�°�� : �桼�����⡼�� / VFPU ��������ͭ��
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define MAX_ENTRY 1024 // ����ե��������
#define MAX_PATH  1024 // ����ե�����̾��

#define SAMPLE_COUNT PSP_AUDIO_SAMPLE_ALIGN(1024) // �֥�å�������Υ���ץ����
#define SAMPLE_SIZE  (SAMPLE_COUNT * 4)           // �֥�å�������ΥХ��ȿ���
#define BUFFER_COUNT 8                            // �Хåե��θĿ���
#define BUFFER_SIZE  (SAMPLE_SIZE * 64)           // �Хåե��ΥХ��ȿ���

// ===========================================================================
//
// ����Τ������
//
// ===========================================================================

// ������򼨤�����Ρ�
enum {
	SCENE_DUMMY = 0,
	SCENE_FILER, // �ե���������
	SCENE_PLAY   // ���ա�
};

// ===========================================================================
//
// �ѿ��������
//
// ===========================================================================

static int audio_thread_id;       // �����ǥ�������åɤ� ID��
static u8 audio_thread_exit_flag; // �����ǥ�������åɤν�λ�ե饰��
static int sexy_thread_id;        // ���ߥ�졼����󥹥�åɤ� ID��
static u8 sexy_thread_exit_flag;  // ���ߥ�졼����󥹥�åɤν�λ�ե饰��

static u8 sound_buffer[BUFFER_COUNT][BUFFER_SIZE]; // ������ɥХåե���
static u8 sound_buffer_ready[BUFFER_COUNT];        // ������ɥХåե�����������Ƥ��뤫�����ե饰��
static u8 sound_read_index;                        // ������ɥХåե����ɤ߹��ߥ���ǥå�����
static u32 sound_read_offset;                      // ������ɥХåե����ɤ߹��ߥ��ե��åȡ�
static u8 sound_write_index;                       // ������ɥХåե��ν񤭹��ߥ���ǥå�����
static u32 sound_write_offset;                     // ������ɥХåե��ν񤭹��ߥ��ե��åȡ�
int audio_handle; // �����ǥ��������ͥ�Υϥ�ɥ롣

static int current_scene;      // ���ߤΥ�����
static int filer_scene_index;  // �ե�����������̤�ɽ������ǥå�����
static int filer_scene_select; // �ե�����������̤����򥤥�ǥå�����

static PSFINFO *psf_info;               // PSFINFO ��¤�Ρ�
static char current_psf_path[MAX_PATH]; // ���ߤ� PSF �ե�����Υѥ���

static char current_directory[MAX_PATH];       // ���ߤΥǥ��쥯�ȥꡣ
static SceIoDirent current_dirents[MAX_ENTRY]; // ���ߤΥǥ��쥯�ȥ�Υե����������
static int current_dirents_count;              // ���ߤΥǥ��쥯�ȥ�Υե��������

u8 enable_reverb; // ��С��֤�ͭ�� / ̵���ե饰��../spu/spu.c ��껲�Ȥ���롣
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
// �ؿ��������
//
// ===========================================================================

// ---------------------------------------------------------------------------
// audio_thread
//
// �����ǥ�������åɡ�
//
// ���� :
//   args : ̤���ѡ�
//   argp : ̤���ѡ�
//
// ����� :
//   ��� 0��
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
	
	// �����ǥ��������ͥ�μ�����
	audio_handle = sceAudioChReserve(
		PSP_AUDIO_NEXT_CHANNEL, SAMPLE_COUNT, PSP_AUDIO_FORMAT_STEREO);
	sceAudioChangeChannelVolume(audio_handle, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX);
	// ����åɤν�λ�ե饰��Ω�Ĥޤǥ롼�ס�
	while(!audio_thread_exit_flag)
	{
		// �������ȡ�(10 ms)
		sceKernelDelayThread(10000);
		
		// �оݥ�����ɥХåե������������ޤ��Ԥġ�
		if(!sound_buffer_ready[sound_read_index]) {
			continue;
		}
		// �оݥ�����ɥХåե����� 1 �֥�å�ʬ���Ϥ��롣
		sceAudioOutputPannedBlocking(
			audio_handle, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX,
			&sound_buffer[sound_read_index][sound_read_offset]);
		
		// �ɤ߹��ߥ��ե��åȤ�ʤ�롣
		sound_read_offset += SAMPLE_SIZE;
		
		// �оݥ�����ɥХåե��������ƽ��Ϥ��Ѥ������
		if(sound_read_offset >= BUFFER_SIZE) {
			// ���Υ�����ɥХåե��򻲾Ȥ���褦���ڤ��ؤ��롣
			sound_buffer_ready[sound_read_index] = 0;
			sound_read_index++;
			sound_read_offset = 0;
			
			// �Ǹ�Υ�����ɥХåե���ã��������
			if(sound_read_index >= BUFFER_COUNT) {
				// �ǽ�Υ�����ɥХåե��򻲾Ȥ���褦���ڤ��ؤ��롣
				sound_read_index = 0;
			}
		}
	}
	
	// ̵���Υ֥�å�����Ϥ��롣����������Υץ��Υ����ɻ��ѡ�
	sceAudioOutputPannedBlocking(audio_handle, 0, 0, sound_buffer[0]);
	
	// �����ǥ��������ͥ�β�����
	sceAudioChRelease(audio_handle);
	
	// ����åɤν�λ��
	sceKernelExitThread(0);
	
	// ���ｪλ��
	return 0;
}

// ---------------------------------------------------------------------------
// sexy_thread
//
// ���ߥ�졼����󥹥�åɡ�
//
// ���� :
//   args : ̤���ѡ�
//   argp : ̤���ѡ�
//
// ����� :
//   ��� 0��
// ---------------------------------------------------------------------------
static int sexy_thread(SceSize args, void *argp)
{
	// ư���å��� 333MHz ���ѹ���
	scePowerSetClockFrequency(333, 333, 166);


	
	// ���ߥ�졼����󳫻ϡ�sexy_stop() ���ƤФ��ޤ����ʤ���
	// ����ץ뤬�������줿������ sexyd_update() ���ƤФ�롣
	sexy_execute();
	
	// ư���å��� 222MHz ���ѹ���
	scePowerSetClockFrequency(222, 222, 111);

	
	// ����åɤν�λ��
	sceKernelExitThread(0);
	
	// ���ｪλ��
	return 0;
}

// ---------------------------------------------------------------------------
// sexyd_update
//
// ���ߥ�졼�����ǥ���ץ뤬�������줿��ƤФ��ؿ���
//
// ���� :
//   buffer : �������줿����ץ롣(44100 Hz / 16 bit / Stereo)
//   length : buffer �ΥХ��ȿ���
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
void sexyd_update(u8 *buffer, int length)
{
	int buffer_free_length; // �оݥХåե��λĤꥵ������
	
	// �оݥ�����ɥХåե������ˤʤ�ޤ��Ԥġ�
	do
	{
		// �������ȡ�(0.01 ms)
		sceKernelDelayThread(10);
		
		// ����åɤν�λ�ե饰��Ω�äƤ��뤫��
		if(sexy_thread_exit_flag) {
			// ���ߥ�졼��������ߤ����롣
			sexy_stop();
			// ���ｪλ��
			return;
		}
		
next:;
	} while(sound_buffer_ready[sound_write_index]);
	

	// �оݥХåե��λĤꥵ������׻����롣
	buffer_free_length = BUFFER_SIZE - sound_write_offset;
	
	// �оݥХåե������ƤΥ���ץ��񤭹���뤫��
	if(buffer_free_length >= length) {
		// �оݥХåե������ƤΥ���ץ��񤭹��ࡣ
		memcpy(&sound_buffer[sound_write_index][sound_write_offset], buffer, length);
		
		// �񤭹��ߥ��ե��åȤ�ʤ�롣
		sound_write_offset += length;
	} else {
		// �оݥХåե��λĤꥵ����ʬ��������ץ��񤭹��ࡣ
		memcpy(&sound_buffer[sound_write_index][sound_write_offset], buffer, buffer_free_length);

		
		// �񤭹����ʬ��������ץ�ΥХåե��ݥ��󥿤ȥ�������Ĵ�����롣
		buffer += buffer_free_length;
		length -= buffer_free_length;
		
		// ���Υ�����ɥХåե��򻲾Ȥ���褦���ڤ��ؤ��롣
		
		sound_buffer_ready[sound_write_index] = 1;
		sound_write_index++;
		sound_write_offset = 0;
		
		// �Ǹ�Υ�����ɥХåե���ã��������
		if(sound_write_index >= BUFFER_COUNT) {
			// �ǽ�Υ�����ɥХåե��򻲾Ȥ���褦���ڤ��ؤ��롣
			sound_write_index = 0;
		}
		
		// �Ĥ�Υ���ץ��񤭹��ि��ˡ����Υ�����ɥХåե������ˤʤ�Τ��Ԥġ�
		goto next;
	}
}

// ---------------------------------------------------------------------------
// psf_play
//
// PSF �ե�����α��ճ��ϡ�
//
// ���� :
//   ̵����
//
// ����� :
//   �������� 0������ʳ��� -1��
// ---------------------------------------------------------------------------
static int psf_play()
{
	int i; // �����ѿ���
	thread_paused = 0;
	// PSF ���ɤ߹��ߡ�
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

	
		



	// PSFINFO ��¤�Τ�ͭ������
	if(psf_info) {
		// ������ɥХåե��ν������
		for(i = 0; i < BUFFER_COUNT; i++)
		{
			memset(sound_buffer[i], 0, BUFFER_SIZE);
			sound_buffer_ready[i] = 0;
		}
		
		// ������ɥХåե����ɤ߽񤭥���ǥå��� / ���ե��åȤν������
		sound_read_index   = 0;
		sound_read_offset  = 0;
		sound_write_index  = 0;
		sound_write_offset = 0;
		
		// �����ǥ�������åɤ������ȳ��ϡ�
		audio_thread_id = sceKernelCreateThread("audio_thread", audio_thread, 18, 65536, 0, 0);
		audio_thread_exit_flag = 0;
		sceKernelStartThread(audio_thread_id, 0, 0);
		
		// ���ߥ�졼����󥹥�åɤ������ȳ��ϡ�
		sexy_thread_id = sceKernelCreateThread("sexy_thread", sexy_thread, 32, 65536, 0, 0);
		sexy_thread_exit_flag  = 0;
		sceKernelStartThread(sexy_thread_id, 0, 0);
		
		// ���ｪλ��
		return 0;
	} else {
		// ���顼��λ��
		return -1;
	}
}

// ---------------------------------------------------------------------------
// psf_stop
//
// PSF �ե�����α�����ߡ�
//
// ���� :
//   ̵����
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void psf_stop()
{
	// PSFINFO ��¤�Τ�ͭ������
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
		// ���ߥ�졼����󥹥�åɤ���ߤ��˴���
		sexy_thread_exit_flag = 1;
		sceKernelWaitThreadEnd(sexy_thread_id, 0);
		sceKernelDeleteThread(sexy_thread_id);
		
		// �����ǥ�������åɤ���ߤ��˴���
		audio_thread_exit_flag = 1;
		sceKernelWaitThreadEnd(audio_thread_id, 0);
		sceKernelDeleteThread(audio_thread_id);
		
		// PSFINFO ��¤�Τ��˴���
		sexy_freepsfinfo(psf_info);
		psf_info = 0;
		thread_paused = 0;

	}
}

// ---------------------------------------------------------------------------
// set_dirents
//
// �ǥ��쥯�ȥ�����μ�����
//
// ���� :
//   ̵����
//
// ����� :
//   �������� 0������ʳ��� -1��
// ---------------------------------------------------------------------------
static int set_dirents()
{
	SceUID uid;         // �ǥ��쥯�ȥꥨ��ȥ�� ID��
	SceIoDirent dirent; // ����Ѥ� SceIoDirent��
	char *extention;    // �ե�����γ�ĥ�ҤؤΥݥ��󥿡�
	int r;              // �����ѿ���

	// �ǥ��쥯�ȥꥨ��ȥ�μ�����
	uid = sceIoDopen(current_directory);
	
	// �ǥ��쥯�ȥꥨ��ȥ̵꤬������
	if(uid < 0) {
		// ���顼��λ��
		return -1;
	}
	
	// �ե���������μ�����
	for(current_dirents_count = 0; current_dirents_count < MAX_ENTRY;)
	{
		// �������������������ʤ���� sceIoDread ���ե꡼�����롣
		memset(&dirent, 0, sizeof(SceIoDirent));
		
		// ����ȥ����������ɤ߹��ߡ�
		r = sceIoDread(uid, &dirent);
		
		// �ǥ��쥯�ȥ꤫��
		if(FIO_SO_ISDIR(dirent.d_stat.st_attr)) {
			// ����ȥ����������鸽�ߤΥǥ��쥯�ȥ�����إ��ԡ���
			memcpy(&current_dirents[current_dirents_count], &dirent, sizeof(SceIoDirent));
			
			// ���ߤΥǥ��쥯�ȥ�Υե�������򥤥󥯥���ȡ�
			current_dirents_count++;
		} else {
			// �ե�����̾�����ĥ�Ҥ���Ф���
			extention = strrchr(dirent.d_name, '.');
			
			// ��ĥ�Ҥ�¸�ߤ��뤫��
			if(extention) {
				// ���ݡ��Ȥ����ĥ�Ҥ���
				if(!stricmp(extention, ".psf") || !stricmp(extention, ".psf1") || !stricmp(extention, ".minipsf")) {
					// ����ȥ����������鸽�ߤΥǥ��쥯�ȥ�����إ��ԡ���
					memcpy(&current_dirents[current_dirents_count], &dirent, sizeof(SceIoDirent));
					
					// ���ߤΥǥ��쥯�ȥ�Υե�������򥤥󥯥���ȡ�
					current_dirents_count++;
				}
			}
		}
		
		// �Ǹ�Υ���ȥ꤫��
		if(r <= 0) {
			break;
		}
	}
	
	// �ǥ��쥯�ȥꥨ��ȥ�β�����
	sceIoDclose(uid);
	
	// ���ｪλ��
	return 0;
}

// ---------------------------------------------------------------------------
// printf_line
//
// ����Ԥ��Ф���ե����ޥåȺѤ�ʸ�����ɽ����
//
// ���� :
//   y      : �Կ���
//   format : �ե����ޥå�ʸ����
//   ...    : �ե����ޥåȰ�����
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void printf_line(int y, char *format, ...)
{
	va_list options; // ���Ф�������Ĺ������
	char buffer[68]; // ʸ�����ѥХåե���68 �� 1 �Ԥ�ɽ����ǽ��ʸ������
	int r;           // �����ѿ���
	
	// ����Ĺ��������Ф���
	va_start(options, format);
	
	// �ե����ޥåȺѤ�ʸ��������롣
	r = vsnprintf(buffer, 68, format, options);
	
	// �ե����ޥåȺѤ�ʸ����� 68 ʸ����Ķ���Ƥ��ʤ�����
	if(r >= 0) {
		// �Ĥä���ʬ�򥹥ڡ��������롣
		memset(&buffer[r], ' ', 68 - r);
		
		// �����ߥ͡��Ȥ��롣
		buffer[67] = '\0';
	}
	
	// ����������֤����ꤹ�롣
	pspDebugScreenSetXY(0, y);
	
	// �ե����ޥåȺѤ�ʸ�����ɽ�����롣
	pspDebugScreenPrintf(buffer);
}

// ---------------------------------------------------------------------------
// filer_draw
//
// �ե�����������̤�ɽ������
//
// ���� :
//   ̵����
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void filer_draw()
{
	int i, j; // �����ѿ���
	
	// �ե�����������̤�ɽ����
	printf_line(0, current_directory);
	printf_line(1, "===== File selector ===============================================");
	
	for(i = 2; i < 32; i++)
	{
		// �ե�����������Ф��륤��ǥå�������롣
		j = i - 2 + filer_scene_index;
		
		// �ե����뤬¸�ߤ��뤫��
		if(j < current_dirents_count) {
			printf_line(i, "%s %s%s",
				// ���Υե����뤬���򤵤�Ƥ���ʤ�С�����������ղá�
				(j == filer_scene_select) ? ">" : " ",
				// �ե�����̾��
				current_dirents[j].d_name,
				// �ǥ��쥯�ȥ�Ǥ���С������˥��ѥ졼�����ղá�
				FIO_SO_ISDIR(current_dirents[j].d_stat.st_attr) ? "/" : ""
			);
		} else {
			// ����Ԥ�ɽ����
			printf_line(i, "");
		}
	}
	
	printf_line(32, "===================================================================");
	printf_line(33, "Cursor = Move / Circle = Play / Triangle = Toggle reverb        |%s|",
		// ��С��֤�ͭ�����ɤ����������󥸥�������
		enable_reverb ? "R" : " "
	);
}

// ---------------------------------------------------------------------------
// play_draw
//
// ���ղ��̤�ɽ������
//
// ���� :
//   ̵����
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void play_draw()
{
	int i; // �����ѿ���
	
	// ���ղ��̤�ɽ����
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
		// ����Ԥ�ɽ����
		printf_line(i, "");
	}
	//printf_line(31, "%3d:%2d:%2d - %dms -f: %dms",hourstot,minutestot,secondstot,psf_info->length, psf_info->fade);
	printf_line(30, "%3d:%2d:%2d",hours,minutes,seconds);
	printf_line(31,"%3d:%2d:%2d",hourstot,minutestot,secondstot);
	printf_line(32, "===================================================================");
	printf_line(33, "Cross = Stop / Triangle = Toggle reverb                     |%s|%s|%s|",
		// ��С��֤�ͭ�����ɤ����������󥸥�������
		enable_timelimit ? "T" : " ",enable_shuffle ? "S" : " ",enable_reverb ? "R" : " "
	);
}

// ---------------------------------------------------------------------------
// filer_scene
//
// �ե�����������̤Υ롼�׽�����
//
// ���� :
//   buttons : ������Ƥ���ܥ���
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void filer_scene(int buttons)
{
	int i; // �����ѿ���
	
	// ��������ܥ��󤬲����줿����
	if(buttons & (PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT)) {
		// ��������ܥ���������˱�������ư�̤���롣
		if(buttons & PSP_CTRL_UP) {
			// 1 ����ǥå������ذ�ư��
			i = -1;
		} else if(buttons & PSP_CTRL_RIGHT) {
			// 10 ����ǥå������ذ�ư��
			i = 10;
		} else if(buttons & PSP_CTRL_DOWN) {
			// 1 ����ǥå������ذ�ư��
			i = 1;
		} else {
			// 10 ����ǥå������ذ�ư��
			i = -10;
		}
		
		// ���򥤥�ǥå������ư�����롣
		filer_scene_select += i;
		
		// ���򥤥�ǥå������ե��������Ʊ�����礭���ʤä�����
		if(filer_scene_select >= current_dirents_count) {
			// ���򥤥�ǥå����򥯥�åפ��롣
			filer_scene_select = current_dirents_count - 1;
		// ���򥤥�ǥå����� 0 ��꾮�����ʤä�����
		} else if(filer_scene_select < 0) {
			// ���򥤥�ǥå����򥯥�åפ��롣
			filer_scene_select = 0;
		}
		
		// ɽ������ǥå��������򥤥�ǥå�������礭���ʤä�����
		if(filer_scene_index > filer_scene_select) {
			// ɽ������ǥå������ư�����롣
			filer_scene_index += i;
			
			// ɽ������ǥå����� 0 ��꾮�����ʤä�����
			if(filer_scene_index < 0) {
				// ɽ������ǥå����򥯥�åפ��롣
				filer_scene_index = 0;
			}
		// ɽ������ǥå��� + 30 �����򥤥�ǥå�����Ʊ�����������ʤä�����
		} else if(filer_scene_index + 30 <= filer_scene_select) {
			// ɽ������ǥå������ư�����롣
			filer_scene_index += i;
			
			// ɽ������ǥå��� + 30 ���ե��������Ʊ�����礭���ʤä�����
			if(filer_scene_index + 30 >= current_dirents_count) {
				// ɽ������ǥå����򥯥�åפ��롣
				filer_scene_index = current_dirents_count - 30;
			}
		}
		
		// ���̤򹹿����롣
		filer_draw();
	// ���ܥ��󤬲����줿����
	} else if(buttons & PSP_CTRL_CIRCLE) {
		// �ǥ��쥯�ȥ꤫��
		if(FIO_SO_ISDIR(current_dirents[filer_scene_select].d_stat.st_attr)) {
			// �ǥ��쥯�ȥ�̾�� 1 �Х����ܤ��ɥåȤ���
			if(current_dirents[filer_scene_select].d_name[0] == '.') {
				// �ǥ��쥯�ȥ�̾�� 2 �Х����ܤ��ɥåȤ���
				if(current_dirents[filer_scene_select].d_name[1] == '.') {
					// ���ߤΥǥ��쥯�ȥ���ѹ����롣(�ƥǥ��쥯�ȥ��)
					current_directory[strlen(current_directory) - 1] = '\0';
					*(strrchr(current_directory, '/') + 1) = '\0';
				}
			} else {
				// ���ߤΥǥ��쥯�ȥ���ѹ����롣(̾���Υǥ��쥯�ȥ��)
				strcat(current_directory, current_dirents[filer_scene_select].d_name);
				strcat(current_directory, "/");
			}
			
			// �ǥ��쥯�ȥ�����򹹿����롣
			set_dirents();
			// ɽ������ǥå�����������
			filer_scene_index  = 0;
			// ���򥤥�ǥå�����������
			filer_scene_select = 0;
			// ���̤򹹿����롣
			filer_draw();
		} else {
			// PSF �ե�����Υѥ����������롣
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			
			// ���դ򳫻Ϥ��롣����˳��Ϥ��줿����
			if(psf_play() >= 0) {
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
				play_draw();
			}
		}
	// ���ܥ��󤬲����줿����
	} else if(buttons & PSP_CTRL_TRIANGLE) {
		// ��С��֤�ͭ�� / ̵�����ڤ��ؤ��롣
		enable_reverb ^= 1;
		// ���̤򹹿����롣
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
// �ե�����������̤Υ롼�׽�����
//
// ���� :
//   buttons : ������Ƥ���ܥ���
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void filer_scene_remote(u32 buttons)
{
	#define PSP_HPRM_VOL_UP_DOWN 0x30
	int i = 0; // �����ѿ���
	
	// ��������ܥ��󤬲����줿����
	if(buttons & (PSP_HPRM_FORWARD | PSP_HPRM_BACK)) {
		// ��������ܥ���������˱�������ư�̤���롣
		if(buttons & PSP_HPRM_BACK) {
			// 1 ����ǥå������ذ�ư��
			i = -1;
		} else if(buttons & PSP_HPRM_FORWARD) {
			// 1 ����ǥå������ذ�ư��
			i = 1;
		}
		
		// ���򥤥�ǥå������ư�����롣
		filer_scene_select += i;
		
		// ���򥤥�ǥå������ե��������Ʊ�����礭���ʤä�����
		if(filer_scene_select >= current_dirents_count) {
			// ���򥤥�ǥå����򥯥�åפ��롣
			filer_scene_select = current_dirents_count - 1;
		// ���򥤥�ǥå����� 0 ��꾮�����ʤä�����
		} else if(filer_scene_select < 0) {
			// ���򥤥�ǥå����򥯥�åפ��롣
			filer_scene_select = 0;
		}
		
		// ɽ������ǥå��������򥤥�ǥå�������礭���ʤä�����
		if(filer_scene_index > filer_scene_select) {
			// ɽ������ǥå������ư�����롣
			filer_scene_index += i;
			
			// ɽ������ǥå����� 0 ��꾮�����ʤä�����
			if(filer_scene_index < 0) {
				// ɽ������ǥå����򥯥�åפ��롣
				filer_scene_index = 0;
			}
		// ɽ������ǥå��� + 30 �����򥤥�ǥå�����Ʊ�����������ʤä�����
		} else if(filer_scene_index + 30 <= filer_scene_select) {
			// ɽ������ǥå������ư�����롣
			filer_scene_index += i;
			
			// ɽ������ǥå��� + 30 ���ե��������Ʊ�����礭���ʤä�����
			if(filer_scene_index + 30 >= current_dirents_count) {
				// ɽ������ǥå����򥯥�åפ��롣
				filer_scene_index = current_dirents_count - 30;
			}
		}
		
		// ���̤򹹿����롣
		filer_draw();
	// ���ܥ��󤬲����줿����
	} else if(buttons & PSP_HPRM_PLAYPAUSE) {
		// �ǥ��쥯�ȥ꤫��
		if(FIO_SO_ISDIR(current_dirents[filer_scene_select].d_stat.st_attr)) {
			// �ǥ��쥯�ȥ�̾�� 1 �Х����ܤ��ɥåȤ���
			if(current_dirents[filer_scene_select].d_name[0] == '.') {
				// �ǥ��쥯�ȥ�̾�� 2 �Х����ܤ��ɥåȤ���
				if(current_dirents[filer_scene_select].d_name[1] == '.') {
					// ���ߤΥǥ��쥯�ȥ���ѹ����롣(�ƥǥ��쥯�ȥ��)
					current_directory[strlen(current_directory) - 1] = '\0';
					*(strrchr(current_directory, '/') + 1) = '\0';
				}
			} else {
				// ���ߤΥǥ��쥯�ȥ���ѹ����롣(̾���Υǥ��쥯�ȥ��)
				strcat(current_directory, current_dirents[filer_scene_select].d_name);
				strcat(current_directory, "/");
			}
			
			// �ǥ��쥯�ȥ�����򹹿����롣
			set_dirents();
			// ɽ������ǥå�����������
			filer_scene_index  = 0;
			// ���򥤥�ǥå�����������
			filer_scene_select = 0;
			// ���̤򹹿����롣
			filer_draw();
		} else {
			// PSF �ե�����Υѥ����������롣
			strcpy(current_psf_path, current_directory);
			strcat(current_psf_path, current_dirents[filer_scene_select].d_name);
			
			// ���դ򳫻Ϥ��롣����˳��Ϥ��줿����
			if(psf_play() >= 0) {
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
				play_draw();
			}
		}
	// ���ܥ��󤬲����줿����
	} else if(buttons & PSP_HPRM_VOL_UP_DOWN) {
		// ��С��֤�ͭ�� / ̵�����ڤ��ؤ��롣
		enable_reverb ^= 1;
		// ���̤򹹿����롣
		filer_draw();
	}
}

// ---------------------------------------------------------------------------
// play_scene
//
// ���ղ��̤Υ롼�׽�����
//
// ���� :
//   buttons : ������Ƥ���ܥ���
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void play_scene(int buttons)
{
	// ���ܥ��󤬲����줿����
	if(buttons & PSP_CTRL_CROSS) {
		// ���դ���ߤ��롣
		psf_stop();
		// ���ߤΥ������֥ե���������פ����ꡣ
		current_scene = SCENE_FILER;
		// ���̤򹹿����롣
		filer_draw();
	// ���ܥ��󤬲����줿����
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
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
		// ��С��֤�ͭ�� / ̵�����ڤ��ؤ��롣
		enable_reverb ^= 1;
		// ���̤򹹿����롣
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
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
			// ���դ򳫻Ϥ��롣����˳��Ϥ��줿����
			if(psf_play() >= 0) {
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
			// ���դ򳫻Ϥ��롣����˳��Ϥ��줿����
			if(psf_play() >= 0) {
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
// ���ղ��̤Υ롼�׽�����
//
// ���� :
//   buttons : ������Ƥ���ܥ���
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void play_scene_remote(u32 buttons)
{
#define PSP_HPRM_VOL_UP_DOWN 0x30
	// ���ܥ��󤬲����줿����
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
		/*// ���դ���ߤ��롣
		psf_stop();
		// ���ߤΥ������֥ե���������פ����ꡣ
		current_scene = SCENE_FILER;
		// ���̤򹹿����롣
		filer_draw();
	// ���ܥ��󤬲����줿����*/
	} else if(buttons == 48) {//& PSP_HPRM_VOL_UP_DOWN) {
				// ��С��֤�ͭ�� / ̵�����ڤ��ؤ��롣
		enable_reverb ^= 1;
		// ���̤򹹿����롣
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
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
			// ���դ򳫻Ϥ��롣����˳��Ϥ��줿����
			if(psf_play() >= 0) {
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
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
			// ���դ򳫻Ϥ��롣����˳��Ϥ��줿����
			if(psf_play() >= 0) {
				// ���ߤΥ������ֱ��աפ����ꡣ
				current_scene = SCENE_PLAY;
				// ���̤򹹿����롣
				play_draw();
			}
		}
		}
	}
}

// ---------------------------------------------------------------------------
// exit_callback
//
// ��λ������Хå���
//
// ���� :
//   ̵����
//
// ����� :
//   ��� 0��
// ---------------------------------------------------------------------------
static int exit_callback()
{
	// ���դ���ߡ�
	psf_stop();
	
	// ���եȥ������ν�λ��
	sceKernelExitGame();
	
	// ���ｪλ��
	return 0;
}

// ---------------------------------------------------------------------------
// power_callback
//
// �Ÿ�������Хå���
//
// ���� :
//   unknown   : ̤���ѡ�
//   powerInfo : ̤���ѡ�
//
// ����� :
//   ̵����
// ---------------------------------------------------------------------------
static void power_callback(int unknown, int powerInfo)
{
}

// ---------------------------------------------------------------------------
// callback_thread
//
// ���Υ���åɡ�
//
// ���� :
//   args : ̤���ѡ�
//   argp : ̤���ѡ�
//
// ����� :
//   ��� 0��
// ---------------------------------------------------------------------------
static int callback_thread(SceSize args, void *argp)
{
	int exit_callback_id;  // ��λ������Хå��� ID��
	int power_callback_id; // �Ÿ�������Хå��� ID��
	
	// ��λ������Хå�����������Ͽ��
	exit_callback_id = sceKernelCreateCallback("Exit Callback", exit_callback, 0);
	sceKernelRegisterExitCallback(exit_callback_id);
	
	// �Ÿ�������Хå�����������Ͽ��
	power_callback_id = sceKernelCreateCallback("Power Callback", power_callback, 0);
	scePowerRegisterCallback(0, power_callback_id);
	
	// ����åɤ򥹥꡼�פ��롣
	sceKernelSleepThreadCB();
	
	// ���ｪλ��
	return 0;
}

// ---------------------------------------------------------------------------
// main
//
// �ᥤ��
//
// ���� :
//   argc : ̤���ѡ�
//   argv : ̤���ѡ�
//
// ����� :
//   ��� 0��
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	int callback_thread_id; // ���Υ���åɤ� ID��
	SceCtrlData ctrl_data;  // ����ȥ��顼�ξ��֡�
	int buttons_prev;       // ����Υܥ���ξ��֡�
	u32 remoteButtons, remoteButtonsOld = 0;
	
	// ���Υ���åɤ������ȳ��ϡ�
	
	callback_thread_id = sceKernelCreateThread("update_thread", callback_thread, 17, 4000, 0, 0);
	sceKernelStartThread(callback_thread_id, 0, 0);
	

	
	// �ǥХå������꡼��ν������
	pspDebugScreenInit();
	//pspDebugInstallErrorHandler(NULL);
	// ����ȥ��顼�Υ���ץ�󥰥�������ȥ⡼�ɤ����ꡣ
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingCycle(PSP_CTRL_MODE_DIGITAL);
	
	// PSFINFO ��¤�Τν������
	psf_info      = 0;
	// ��С��֤�̵���ˤ��롣
	enable_reverb = 0;
	// ����Υܥ���ξ��֤�������
	buttons_prev  = 0;
	
	// ���ߤΥ������֥ե���������פ����ꡣ
	current_scene = SCENE_FILER;
	// ���ߤΥǥ��쥯�ȥ���ms0:/�פ����ꡣ
	strcpy(current_directory, "ms0:/");
	// �ǥ��쥯�ȥ�����򹹿����롣
	set_dirents();
	// ɽ������ǥå�����������
	filer_scene_index  = 0;
	// ���򥤥�ǥå�����������
	filer_scene_select = 0;
	// ���̤򹹿����롣
	filer_draw();
	
	// �ᥤ��롼�ס�
	while(1)
	{

		// �������ȡ�(100 ms)
		sceKernelDelayThread(100000);
		
		// ����ȥ��顼�ξ��֤������
		sceCtrlReadBufferPositive(&ctrl_data, 1);
		
		// ����Ȳ�����Ƥ���ܥ���ξ��֤�Ʊ������
		if(ctrl_data.Buttons == buttons_prev) {
			// ��������ܥ���ʳ��ϲ�����Ƥ��ʤ���ΤȤ��롣
			ctrl_data.Buttons &= (PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT);
		} else {
			// ���ߤΥܥ���ξ��֤���¸���롣
			buttons_prev = ctrl_data.Buttons;
		}
		
		// ���ߤΥ�����˱������ؿ���ƤӽФ���
		switch(current_scene)
		{
		case SCENE_FILER:
			{	// �ե���������
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
			// ���ա�
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
	
	// ���ｪλ��
	return 0;
}
