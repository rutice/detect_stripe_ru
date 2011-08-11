//---------------------------------------------------------------------------------------------
//		縞判定フィルタ	by Bean
//---------------------------------------------------------------------------------------------

#include "stdafx.h"
#include "filter.h"
#include <Windows.h>
#include <vector>
#include <sys/timeb.h>
#include <tchar.h>

/* 定数 */
const int TRK_THRESHOLD	= 0;
const int TRK_CRITERION	= 1;
const int TRACK_N		= 2;
const int CHK_LUMA		= 0;
const int CHK_INVERT	= 1;
const int CHECK_N		= 2;
const int ID_STOP		= 100;
const int ID_SCAN_NEXT	= 101;
const int ID_SCAN_ALL	= 102;
const int WM_FILTER_REDRAW		= WM_USER + 150;
const int WM_FILTER_SCAN_STOP	= WM_USER + 151;

/* グローバル変数 */
int		state;
int		block_h, block_v, offset_h, offset_v, max_w, pitch;
std::vector<char> flag;
bool	sse2, redraw;
HWND	hbutton_scan_next, hbutton_scan_all;
HWND	hstatic_detect;
HWND	hstatic_result;

// 定数(ru)
const int IDS_DETECT		= 7010;
const int IDS_RESULT		= 7011;
const int IDT_DETECT		= 0;

/* 関数プロトタイプ */
bool	DetectStripe( FILTER *fp, PIXEL_YC *ycp_edit );
void	PartialJudge_MT( int thread_id, int thread_num, void *param1, void *param2 );
void	Invert_MT( int thread_id, int thread_num, void *param1, void *param2 );
HWND	CreateButton( const TCHAR *str, int x, int y, int w, int h, int id, const FILTER *fp );
bool	SSE2Check();


//---------------------------------------------------------------------
//		フィルタ構造体定義
//---------------------------------------------------------------------
TCHAR	*track_name[] =		{	(TCHAR*)"検出閾値",	(TCHAR*)"判定基準"	};
int		track_default[] =	{	8,		3	};
int		track_s[] =			{	0,		1	};
int		track_e[] =			{	15,		9	};

TCHAR	*check_name[] = 	{	(TCHAR*)"輝度のみで判定",	(TCHAR*)"縞検出部分を反転する"	};
int		check_default[] = 	{	1,		0	};

FILTER_DLL filter = {
	FILTER_FLAG_EX_INFORMATION | FILTER_FLAG_WINDOW_SIZE,
	360, 180,
	//320, 165,
	(TCHAR*)"縞判定フィルタ",
	TRACK_N,
	track_name,
	track_default,
	track_s, track_e,
	CHECK_N,
	check_name,
	check_default,
	func_proc,
	NULL, NULL, NULL,
	func_WndProc,
	NULL, NULL, NULL, 0,
	(TCHAR*)"縞判定フィルタ ver 0.20",
};


//---------------------------------------------------------------------
//		フィルタ構造体のポインタを渡す関数
//---------------------------------------------------------------------
extern "C" FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTable( void )
{
	return &filter;
}


//---------------------------------------------------------------------
//		フィルタ処理関数
//---------------------------------------------------------------------
BOOL func_proc( FILTER *fp, FILTER_PROC_INFO *fpip )
{
	if( !sse2 || fp->exfunc->is_saving( fpip->editp ) )
		return TRUE;

	// 走査中の再描画時は処理せず
	if( redraw )
	{
		redraw = false;
		return TRUE;
	}

	block_v		= (fpip->h - 10) / 32;		// 上下4行は処理しない
	block_h		= fpip->w / 32;
	offset_v	= (fpip->h - 32 * block_v) / 2;
	offset_h	= (fpip->w - 32 * block_h) / 2 + 4 &~ 7;	// 8の倍数

	if( state == ID_STOP )
	{
		if( DetectStripe( fp, fpip->ycp_edit ) )
			SetWindowText( fp->hwnd, _T("縞判定フィルタ [縞検出]") );
		else
			SetWindowText( fp->hwnd, _T("縞判定フィルタ") );

		// 反転
		if( fp->check[CHK_INVERT] )
			fp->exfunc->exec_multi_thread_func( Invert_MT, fpip->ycp_edit, NULL );
	}
	else // state != ID_STOP
	{
		int frame	= fp->exfunc->get_frame( fpip->editp );
		int frame_s	= frame;
		int frame_e	= fp->exfunc->get_frame_n( fpip->editp );
		int detect	= 0;

		PIXEL_YC		*ycp_edit;
		FRAME_STATUS	fsp;
		TCHAR			buf[64], buf2[64];
		MSG				msg;
		
		fp->exfunc->set_ycp_filtering_cache_size( fp, fpip->max_w, fpip->h, 1, NULL );
		fp->exfunc->get_frame_status( fpip->editp, frame, &fsp );
		int profile = fsp.config;

		// 選択範囲をチェック
		int select_s, select_e;
		fp->exfunc->get_select_frame( fpip->editp, &select_s, &select_e );
		if( frame < select_s || select_e <= frame )
		{
			SendMessage( fp->hwnd, WM_COMMAND, state, 0 );
			return TRUE;
		}

		if( state == ID_SCAN_NEXT )
		{
			++frame;
			_stprintf_s( buf, _T("走査中 %d/%d"), frame + 1, frame_e );
		}
		else // state == ID_SCAN_ALL
			_stprintf_s( buf, _T("全走査中 %d/%d"), frame + 1, frame_e );
		SetWindowText( fp->hwnd, buf );

		_timeb start, now;
		_ftime_s( &start );

		// メインループ
		while( true )
		{
			fp->exfunc->set_frame( fpip->editp, frame );

			// メッセージ処理
			while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
				DispatchMessage( &msg );
			if( state == ID_STOP )
				return TRUE;
			if( !fp->exfunc->is_filter_active( fp ) )
			{
				SendMessage( fp->hwnd, WM_FILTER_SCAN_STOP, 0, 0 );
				return TRUE;
			}

			// プロファイルの確認
			fp->exfunc->get_frame_status( fpip->editp, frame, &fsp );
			if( profile != fsp.config )
			{
				profile = fsp.config;
				SendMessage( fp->hwnd, WM_FILTER_REDRAW, 0, 0 );
				if( !fp->exfunc->is_filter_active( fp ) )
				{
					redraw = false;
					SendMessage( fp->hwnd, WM_FILTER_SCAN_STOP, 0, 0 );
					MessageBeep( MB_ICONASTERISK );
					return TRUE;
				}
			}
			
			ycp_edit = fp->exfunc->get_ycp_filtering_cache_ex( fp, fpip->editp, frame, NULL, NULL );
			bool judge = DetectStripe( fp, ycp_edit );

			if( state == ID_SCAN_NEXT )
			{
				if( judge )
				{
					MessageBeep( MB_ICONASTERISK );
					break;
				}
				if( !(frame & 15) )	// 情報表示の更新
				{
					if( frame > frame_s + 300 )
					{
						_ftime_s( &now );
						int remain = int( (now.time * 1000 + now.millitm - start.time * 1000 - now.millitm) * (select_e - frame) / ((frame - frame_s) * 1000) );
						_stprintf_s( buf, _T("走査中 %d/%d 残り時間:"), frame + 1, frame_e );
						if( remain >= 3600 ) { _stprintf_s( buf2, _T("%d時間"), remain / 3600 ); _tcscat_s( buf, buf2 ); }
						if( remain >= 60 ) { _stprintf_s( buf2, _T("%d分"), (remain / 60) % 60 ); _tcscat_s( buf, buf2 ); }
						if( remain < 600 ) { _stprintf_s( buf2, _T("%d秒"), remain % 60 ); _tcscat_s( buf, buf2 ); }
					}
					else // frame <= frame_s + 300
						_stprintf_s( buf, _T("走査中 %d/%d"), frame + 1, frame_e );
					SetWindowText( fp->hwnd, buf );
				}
			}
			else // state == ID_SCAN_ALL
			{
				if( judge )
				{
					fsp.edit_flag |= EDIT_FRAME_EDIT_FLAG_MARKFRAME;
					fp->exfunc->set_frame_status( fpip->editp, frame, &fsp );
					++detect;
				}
				if( !(frame & 15) )	// 情報表示の更新
				{
					if( frame > frame_s + 300 )
					{
						_ftime_s( &now );
						int remain = int( (now.time * 1000 + now.millitm - start.time * 1000 - now.millitm) * (select_e - frame) / ((frame - frame_s) * 1000) );
						_stprintf_s( buf, _T("全走査中 %d/%d 検出:%d 残り:"), frame + 1, frame_e, detect );
						if( remain >= 3600 ) { _stprintf_s( buf2, _T("%d時間"), remain / 3600 ); _tcscat_s( buf, buf2 ); }
						if( remain >= 60 ) { _stprintf_s( buf2, _T("%d分"), (remain / 60) % 60 ); _tcscat_s( buf, buf2 ); }
						if( remain < 600 ) { _stprintf_s( buf2, _T("%d秒"), remain % 60 ); _tcscat_s( buf, buf2 ); }
					}
					else // frame <= frame_s + 300
						_stprintf_s( buf, _T("全走査中 %d/%d 検出:%d"), frame + 1, frame_e, detect );
					SetWindowText( fp->hwnd, buf );
				}
			}

			if( frame + 1 == frame_e || frame == select_e )
			{
				MessageBeep( MB_ICONASTERISK );
				break;
			}

			++frame;
		}

		SendMessage( fp->hwnd, WM_COMMAND, state, 0 );
	}

	return TRUE;
}

bool DetectStripe( FILTER *fp, PIXEL_YC *ycp_edit )
{
	flag.assign( block_v * block_h, 0 );

	fp->exfunc->exec_multi_thread_func( PartialJudge_MT, fp, ycp_edit );

	// 任意の3x3ブロック内の縞検出ブロック数をチェック
	std::vector<char> temp( block_h * block_v );
	for( int i = 0; i < block_v; ++i )
		for( int j = 0; j < block_h - 2; ++j )
			temp[i*block_h+j] = flag[i*block_h+j] + flag[i*block_h+j+1] + flag[i*block_h+j+2];
	for( int i = 0; i < block_v - 2; ++i )
		for( int j = 0; j < block_h - 2; ++j )
			if( fp->track[TRK_CRITERION] <= temp[i*block_h+j] + temp[(i+1)*block_h+j] + temp[(i+2)*block_h+j] )
				return true;

	return false;
}

void PartialJudge_MT( int thread_id, int thread_num, void *param1, void *param2 )
{
	FILTER *fp = (FILTER*)param1;
	PIXEL_YC *ycp_edit = (PIXEL_YC*)param2;

	int block_v_s	= block_v *  thread_id      / thread_num;
	int block_v_e	= block_v * (thread_id + 1) / thread_num;
	int threshold	= ( (1<<(fp->track[TRK_THRESHOLD]+1)/2) + (1<<fp->track[TRK_THRESHOLD]/2) ) << 4;
	int denoise		= 16 * fp->track[TRK_THRESHOLD] + 32;

	_declspec(align(16)) const int DENOISE[4] = { denoise, 0, denoise, 0 };
	_declspec(align(16)) int dst[4];
	_declspec(align(16)) unsigned char temp[3*32*34];
	_declspec(align(16)) short chroma_offset[96];
	unsigned char *ptemp = temp;
	short *pchroma_offset = chroma_offset;
	for( int i = 0; i < 96; i+=3 )
		chroma_offset[i] = 0, chroma_offset[i+1] = chroma_offset[i+2] = 2048;

	PIXEL_YC *ycp;
	PIXEL_YC *ycp2 = ycp_edit + (32 * block_v_s + offset_v - 1) * max_w + offset_h;

	for( int i = block_v_s; i < block_v_e; ++i )
	{
		for( int j = 0; j < block_h; ++j )
		{
			ycp = ycp2 + 32 * j;

			// 基本方針:
			// まず、輝度・色差を short から unsigned char に圧縮変換し、temp に格納する
			// 次に、1行隣と2行隣とのそれぞれの絶対差の和を psadbw で一気に求める
			// ノイズを弾くために、psadbw で求められた絶対差の和をある定数を符号無し飽和演算で減算してから総和を取る
			// 上下端で誤判定しないように、2行隣との絶対差の和を求める範囲は、1行隣とのそれよりも広く取ることに注意する

			if( fp->check[CHK_LUMA] )
			{
				__asm
				{
					mov			esi, ycp
					mov			edi, ptemp
					mov			eax, pitch
					mov			ecx, 34

compress_nextline_luma:
					pinsrw		xmm0, [esi], 0
					pinsrw		xmm0, [esi+6], 1
					pinsrw		xmm0, [esi+12], 2
					pinsrw		xmm0, [esi+18], 3
					pinsrw		xmm0, [esi+24], 4
					pinsrw		xmm0, [esi+30], 5
					pinsrw		xmm0, [esi+36], 6
					pinsrw		xmm0, [esi+42], 7
					pinsrw		xmm1, [esi+48], 0
					pinsrw		xmm1, [esi+54], 1
					pinsrw		xmm1, [esi+60], 2
					pinsrw		xmm1, [esi+66], 3
					pinsrw		xmm1, [esi+72], 4
					pinsrw		xmm1, [esi+78], 5
					pinsrw		xmm1, [esi+84], 6
					pinsrw		xmm1, [esi+90], 7
					pinsrw		xmm2, [esi+96], 0
					pinsrw		xmm2, [esi+102], 1
					pinsrw		xmm2, [esi+108], 2
					pinsrw		xmm2, [esi+114], 3
					pinsrw		xmm2, [esi+120], 4
					pinsrw		xmm2, [esi+126], 5
					pinsrw		xmm2, [esi+132], 6
					pinsrw		xmm2, [esi+138], 7
					pinsrw		xmm3, [esi+144], 0
					pinsrw		xmm3, [esi+150], 1
					pinsrw		xmm3, [esi+156], 2
					pinsrw		xmm3, [esi+162], 3
					pinsrw		xmm3, [esi+168], 4
					pinsrw		xmm3, [esi+174], 5
					pinsrw		xmm3, [esi+180], 6
					pinsrw		xmm3, [esi+186], 7
					psraw		xmm0, 4
					psraw		xmm1, 4
					psraw		xmm2, 4
					psraw		xmm3, 4
					packuswb	xmm0, xmm1
					packuswb	xmm2, xmm3
					movdqa		[edi], xmm0
					movdqa		[edi+16], xmm2
					add			esi, eax
					add			edi, 32
					dec			ecx
					jnz			compress_nextline_luma

					mov			esi, ptemp
					mov			ecx, 31
					pxor		xmm6, xmm6
					pxor		xmm7, xmm7

					movdqa		xmm0, [esi]
					movdqa		xmm1, [esi+16]
					movdqa		xmm2, [esi+64]
					movdqa		xmm3, [esi+64+16]
					psadbw		xmm0, xmm2
					psadbw		xmm1, xmm3
					psubusw		xmm0, DENOISE
					psubusw		xmm1, DENOISE
					paddq		xmm7, xmm0
					paddq		xmm7, xmm1
					add			esi, 32

judge_nextline_luma:
					movdqa		xmm0, [esi]
					movdqa		xmm1, [esi+16]
					movdqa		xmm2, [esi+32]
					movdqa		xmm3, [esi+32+16]
					movdqa		xmm4, xmm0
					movdqa		xmm5, xmm1
					psadbw		xmm0, xmm2
					psadbw		xmm1, xmm3
					movdqa		xmm2, [esi+64]
					movdqa		xmm3, [esi+64+16]
					psadbw		xmm2, xmm4
					psadbw		xmm3, xmm5
					psubusw		xmm0, DENOISE
					psubusw		xmm1, DENOISE
					psubusw		xmm2, DENOISE
					psubusw		xmm3, DENOISE
					paddq		xmm6, xmm0
					paddq		xmm6, xmm1
					paddq		xmm7, xmm2
					paddq		xmm7, xmm3
					add			esi, 32
					dec			ecx
					jnz			judge_nextline_luma

					psllq		xmm7, 32
					por			xmm6, xmm7
					movdqa		dst, xmm6
				}
				
				/*	C++ 参考コード (SSE2での処理とは多少異なるが互換性有り)
				int maxw2 = 2 * max_w, denoise2 = 2 * denoise;
				dst[0] = dst[1] = dst[2] = dst[3] = 0;
				threshold *= 16;
				for( int j = 0; j < 32; ++j )
					dst[1] += std::max( abs( ycp[j].y - ycp[maxw2+j].y ) - denoise2, 0 );
				ycp += max_w;
				for( int i = 0; i < 31; ++i )
				{
					for( int j = 0; j < 32; ++j )
					{
						dst[0] += std::max( abs( ycp[j].y - ycp[max_w+j].y ) - denoise2, 0 );
						dst[1] += std::max( abs( ycp[j].y - ycp[maxw2+j].y ) - denoise2, 0 );
					}
					ycp += max_w;
				}
				*/
			}
			else // fp->check[CHK_LUMA] == 0
			{
				int yloopcount = 34;

				__asm
				{
					mov			esi, ycp
					mov			edi, ptemp

compress_nextline:
					xor			eax, eax
					mov			ecx, 3
					mov			edx, pchroma_offset

compress_next32data:
					movdqa		xmm0, [esi+2*eax]
					movdqa		xmm1, [esi+2*eax+16]
					movdqa		xmm2, [esi+2*eax+32]
					movdqa		xmm3, [esi+2*eax+48]
					paddw		xmm0, [edx+2*eax]
					paddw		xmm1, [edx+2*eax+16]
					paddw		xmm2, [edx+2*eax+32]
					paddw		xmm3, [edx+2*eax+48]
					psraw		xmm0, 4
					psraw		xmm1, 4
					psraw		xmm2, 4
					psraw		xmm3, 4
					packuswb	xmm0, xmm1
					packuswb	xmm2, xmm3
					movdqa		[edi+eax], xmm0
					movdqa		[edi+eax+16], xmm2
					add			eax, 32
					dec			ecx
					jnz			compress_next32data

					add			esi, pitch
					add			edi, 96
					dec			yloopcount
					jnz			compress_nextline

					mov			esi, ptemp
					mov			ecx, 3
					pxor		xmm6, xmm6
					pxor		xmm7, xmm7

judge_next32data_firstline:
					movdqa		xmm0, [esi]
					movdqa		xmm1, [esi+16]
					movdqa		xmm2, [esi+192]
					movdqa		xmm3, [esi+192+16]
					psadbw		xmm0, xmm2
					psadbw		xmm1, xmm3
					psubusw		xmm0, DENOISE
					psubusw		xmm1, DENOISE
					paddq		xmm7, xmm0
					paddq		xmm7, xmm1
					add			esi, 32
					dec			ecx
					jnz			judge_next32data_firstline

					mov			ecx, 3*31

judge_next32data:
					movdqa		xmm0, [esi]
					movdqa		xmm1, [esi+16]
					movdqa		xmm2, [esi+96]
					movdqa		xmm3, [esi+96+16]
					movdqa		xmm4, xmm0
					movdqa		xmm5, xmm1
					psadbw		xmm0, xmm2
					psadbw		xmm1, xmm3
					movdqa		xmm2, [esi+192]
					movdqa		xmm3, [esi+192+16]
					psadbw		xmm2, xmm4
					psadbw		xmm3, xmm5
					psubusw		xmm0, DENOISE
					psubusw		xmm1, DENOISE
					psubusw		xmm2, DENOISE
					psubusw		xmm3, DENOISE
					paddq		xmm6, xmm0
					paddq		xmm6, xmm1
					paddq		xmm7, xmm2
					paddq		xmm7, xmm3
					add			esi, 32
					dec			ecx
					jnz			judge_next32data

					psllq		xmm7, 32
					por			xmm6, xmm7
					movdqa		dst, xmm6
				}
			}

			int diff_one_line  = dst[0] + dst[2];
			int diff_two_lines = dst[1] + dst[3];

			if( diff_one_line > 1.05 * diff_two_lines && diff_one_line > threshold )
				flag[i*block_h+j] = 1;
		}

		ycp2 += 32 * max_w;
	}
}

void Invert_MT( int thread_id, int thread_num, void *param1, void *param2 )
{
	int block_v_s = block_v *  thread_id      / thread_num;
	int block_v_e = block_v * (thread_id + 1) / thread_num;
	PIXEL_YC *ycp_edit = (PIXEL_YC*)param1 + offset_v * max_w + offset_h, *ycp;

	for( int i = block_v_s; i < block_v_e; ++i )
	{
		for( int j = 0; j < block_h; ++j )
		{
			if( flag[i*block_h+j] )
			{
				ycp = ycp_edit + i * 32 * max_w + j * 32;
				for( int k = 0; k < 32; ++k )
				{
					for( int l = 0; l < 32; ++l )
					{
						ycp[l].y  = 4096 - ycp[l].y;
						ycp[l].cb = -ycp[l].cb;
						ycp[l].cr = -ycp[l].cr;
					}
					ycp += max_w;
				}
			}
		}
	}
}

bool check_cycle(int *r) {
	if (abs(r[0]) < 1500 && r[1] < 0 && r[3] > 0 && abs(r[4]) < 1500) {
		return true;
	}
	return false;
}

template <typename T>
T _max(T a, T b) {
	return a > b ? a : b;
}

inline
void get_y_max(PIXEL_YC *yc0, PIXEL_YC *yc1, int hstart, int hend, int w, int max_w, int m[2]) {
	for (int i=hstart; i<(hend&0xFFFE); i+=2) {
		for (int j=16; j<(w&0xFFF0)-16; j++) {
			m[0] = max(m[0], abs((yc0 + i * max_w + j)->y - (yc1 + i * max_w + j)->y));
			m[1] = max(m[1], abs((yc0 + (i+1) * max_w + j)->y - (yc1 + (i+1) * max_w + j)->y));
		}
	}
}

bool detect_pattern(FILTER *fp, void *editp, int iFrame, int nCheck) {
	int h, w, max_h, max_w;
	SYS_INFO si;
	FRAME_STATUS fs;
	int vid0, vid1;
	fp->exfunc->get_frame_size(editp, &w, &h);
	fp->exfunc->get_sys_info(editp, &si);
	max_h = h;//si.max_h;
	max_w = si.vram_w;

	SetWindowText(hstatic_detect, "");
	SetWindowText(hstatic_result, "チェック中...");

	if (fp->exfunc->get_frame_status(editp, iFrame, &fs) == FALSE) {
		return false;
	}
	vid0 = fs.video;
	PIXEL_YC *yc0, *yc1;
	yc0 = (PIXEL_YC*)fp->exfunc->get_ycp_source_cache(editp, iFrame, 0);
	if (yc0 == NULL) {
		return false;
	}
	int hantei[10] = {0};
	for (int x=1; x<36; x++) {
		if (fp->exfunc->get_frame_status(editp, iFrame+x, &fs) == FALSE) {
			return false;
		}
		vid1 = fs.video;
		int diff = vid1 - vid0;
		if (diff != 1) {
			break;
		}
		yc1 = (PIXEL_YC*)fp->exfunc->get_ycp_source_cache(editp, iFrame+x, 0);
		if (yc1 == NULL) {
			return false;
		}

		int m[2][2] = {{0, 0}, {2, 2}};
		get_y_max(yc0, yc1, 8  , h/2, w, max_w, m[0]); // 上半分
		get_y_max(yc0, yc1, h/2, h-8, w, max_w, m[1]); // 下半分
		yc0 = yc1;
		vid0 = vid1;

		// TOP・BOTTOMフィールドの変化の変化を計算する
		// ＋ならTOPのが変化が大きい、－ならBOTTOMのが変化が大きい
		// DVD等では変化の無いフィールドは分母がほぼ0になるのでゼロ除算回避
		for (int i=0; i<2; i++) {
			double sa = (double)(m[i][0] - m[i][1]) / max(2, min(m[i][0], m[i][1]));
			if (sa > 1.5) {
				hantei[(iFrame+x-1) % 5] += (int)(sa * max(m[i][0], m[i][1]));
			}
			if (sa < -1.5) {
				hantei[(iFrame+x-1) % 5] += (int)(sa * max(m[i][0], m[i][1]));
			}
		}
	}

	char sz[256];
	wsprintf(sz, "判定 0:%d, 1:%d, 2:%d, 3:%d, 4:%d", hantei[0], hantei[1], hantei[2], hantei[3], hantei[4]);
	SetWindowText(hstatic_result, sz);
		
	// 周期チェック
	for (int i=0; i<5; i++) {
		hantei[i+5] = hantei[i];
		if (check_cycle(hantei + i)) {
			int cycle = i;
			char s[6] = {'o', 'o', 'o', 'o', 'o', '\0'};
			s[(cycle + 1) % 5] = 'x';
			wsprintf(sz, "推定:%s", s);
			SetWindowText(hstatic_detect, sz);
			return true;
		}
	}

	SetWindowText(hstatic_detect, "動きが無いか30fps");
	return true;
}


//---------------------------------------------------------------------
//		ウィンドウメッセージ処理関数
//---------------------------------------------------------------------
BOOL func_WndProc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, void *editp, FILTER *fp )
{
	SYS_INFO si;

	switch( msg )
	{
	case WM_FILTER_INIT:
		fp->exfunc->get_sys_info( editp, &si );
		max_w = si.vram_w;
		pitch = si.vram_line_size;
		
		#define CreateStatic( str, x, y, w, h, id ) \
			CreateWindowEx( 0, _T("STATIC"), _T(str), WS_CHILD | WS_VISIBLE, x, y, w, h, hwnd, (HMENU)id, fp->dll_hinst, NULL )
		hstatic_detect  = CreateStatic( "", 170, 108, 120, 22, IDS_DETECT );
		hstatic_result  = CreateStatic( "35フレームから適当に自動判定", 10, 135, 350, 22, IDS_RESULT );
		SetWindowFont( hstatic_detect,  si.hfont, 0 );
		SetWindowFont( hstatic_result,  si.hfont, 0 );

		hbutton_scan_next = CreateButton( _T("走査"),    20, 108, 60, 22, ID_SCAN_NEXT, fp );
		hbutton_scan_all  = CreateButton( _T("全走査"), 100, 108, 60, 22, ID_SCAN_ALL,  fp );
		SendMessage( hbutton_scan_next, WM_SETFONT, (WPARAM)si.hfont, FALSE );
		SendMessage( hbutton_scan_all,  WM_SETFONT, (WPARAM)si.hfont, FALSE );
		EnableWindow( hbutton_scan_next, false );
		EnableWindow( hbutton_scan_all,  false );
		state = ID_STOP;
		sse2 = SSE2Check();
		if( !sse2 )
			MessageBox( hwnd, _T("お使いのCPUはSSE2非対応なため、本フィルタは動作しません。"), _T("縞判定フィルタ - エラー"), MB_OK );
		break;

	case WM_FILTER_FILE_OPEN:
		EnableWindow( hbutton_scan_next, true );
		EnableWindow( hbutton_scan_all,  true );
		break;

	case WM_FILTER_FILE_CLOSE:
		EnableWindow( hbutton_scan_next, false );
		EnableWindow( hbutton_scan_all,  false );
		return FALSE;
	
	case WM_FILTER_UPDATE:
		if (fp->exfunc->is_filter_active(fp) && 
			fp->exfunc->get_frame_n(editp) > 0) {
			SetTimer(hwnd, IDT_DETECT, 750, NULL);
		} else {
			SetWindowText(hstatic_detect, "");
			SetWindowText(hstatic_result, "");
		}
		break;
	
	case WM_TIMER:
		KillTimer(hwnd, IDT_DETECT);
		if (editp == NULL) {
			break;
		}
		do {
			bool ret = detect_pattern(fp, editp, fp->exfunc->get_frame(editp), 5);
			if (ret == false) {
				SetWindowText(hstatic_result, "周期推測：あいまいです。");
			}
		} while(0);
		break;

	case WM_FILTER_REDRAW:
		redraw = true;
		return TRUE;

	case WM_FILTER_SCAN_STOP:
		state = ID_STOP;
		SetWindowText( hwnd, _T("縞判定フィルタ") );
		SetWindowText( hbutton_scan_next, _T("走査") );
		SetWindowText( hbutton_scan_all, _T("全走査") );
		EnableWindow( hbutton_scan_all, true );
		EnableWindow( hbutton_scan_next, true );
		return TRUE;

	case WM_COMMAND:
		if( fp->exfunc->is_filter_active( fp ) )
		{
			switch( LOWORD( wparam ) )
			{
			case ID_SCAN_NEXT:
				if( state == ID_STOP )
				{
					state = ID_SCAN_NEXT;
					SetWindowText( hbutton_scan_next, _T("中止") );
					EnableWindow( hbutton_scan_all, false );
					return TRUE;
				}
				else // state == ID_SCAN_NEXT
				{
					state = ID_STOP;
					SetWindowText( hbutton_scan_next, _T("走査") );
					EnableWindow( hbutton_scan_all, true );
					return TRUE;
				}
				break;

			case ID_SCAN_ALL:
				if( state == ID_STOP )
				{
					state = ID_SCAN_ALL;
					SetWindowText( hbutton_scan_all, _T("中止") );
					EnableWindow( hbutton_scan_next, false );
					return TRUE;
				}
				else // state == ID_SCAN_ALL
				{
					state = ID_STOP;
					SetWindowText( hbutton_scan_all, _T("全走査") );
					EnableWindow( hbutton_scan_next, true );
					return TRUE;
				}
				break;
			}
		}
		break;
	}

	return FALSE;
}

inline HWND CreateButton( const TCHAR *str, int x, int y, int w, int h, int id, const FILTER *fp )
{
	return CreateWindowEx( 0, _T("BUTTON"), str, WS_CHILD | WS_VISIBLE, x, y, w, h, fp->hwnd, (HMENU)id, fp->dll_hinst, NULL );
}

bool SSE2Check()
{
	int cpu_check;

	__asm
	{
		xor		eax, eax
		cpuid
		cmp		eax, 0
		jg		check
		xor		edx, edx
		jmp		last
check:
		mov		eax, 1
		cpuid
last:
		mov		cpu_check, edx
	}
	
	return cpu_check & 1 << 26 ? true : false;
}
