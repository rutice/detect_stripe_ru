//---------------------------------------------------------------------------------------------
//		縞判定フィルタ		by Bean
//---------------------------------------------------------------------------------------------

#include "filter.h"
#include <WindowsX.h>
#include <vector>
#include <cmath>
#include <ctime>
#include <tchar.h>

// グローバル変数
int		state;
int		frame;
int		block_v, block_h, offset_v, offset_h, max_w, pitch, thrs, adjust;
double	rate;
std::vector<int> flag;
HWND	hbutton_scan_next, hbutton_scan_all;

// 定数
const int STATE_STOP		= 0;
const int STATE_SCAN_NEXT	= 1;
const int STATE_SCAN_ALL	= 2;
const int ID_SCAN_NEXT		= 7000;
const int ID_SCAN_ALL		= 7001;

// 関数プロトタイプ
void	multi_thread_func( int thread_id, int thread_num, void *param1, void *param2 );


//---------------------------------------------------------------------
//		フィルタ構造体定義
//---------------------------------------------------------------------
const int TRACK_N = 2;
TCHAR	*track_name[] =		{	(TCHAR*)"検出強度",		(TCHAR*)"判定基準"	};
int		track_default[] =	{	10,						3					};
int		track_s[] =			{	0,						1					};
int		track_e[] =			{	15,						9					};

const int CHECK_N = 1;
TCHAR	*check_name[] = 	{	(TCHAR*)"縞検出部分を反転する"	};
int		check_default[] = 	{	0,								};

FILTER_DLL filter = {
	FILTER_FLAG_EX_INFORMATION | FILTER_FLAG_WINDOW_SIZE,
	320, 145,
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
	NULL, NULL, NULL, NULL,
	(TCHAR*)"縞判定フィルタ ver 0.10"
};


//---------------------------------------------------------------------
//		フィルタ構造体のポインタを渡す関数
//---------------------------------------------------------------------
EXTERN_C FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTable( void )
{
	return &filter;
}

//---------------------------------------------------------------------
//		フィルタ処理関数
//---------------------------------------------------------------------
BOOL func_proc( FILTER *fp, FILTER_PROC_INFO *fpip )
{
	if( fp->exfunc->is_saving( fpip->editp ) )
		return TRUE;

	frame = fp->exfunc->get_frame( fpip->editp );
	int frame_s = frame, frame_e = fp->exfunc->get_frame_n( fpip->editp );

	block_v		= (fpip->h - 14) / 32;
	block_h		= fpip->w / 32;
	offset_v	= (fpip->h - 32 * block_v) / 2;
	offset_h	= (fpip->w - 32 * block_h) / 2 + 4 &~ 7;
	thrs		= ( (1<<(fp->track[0]+1)/2) + (1<<fp->track[0]/2) ) << 4;
	adjust		= 16 * fp->track[0] + 32;
	rate		= 1.05 + 0.005 * fp->track[0];

	if( state == STATE_STOP )
	{
		bool judge = false;
		flag.assign( block_v * block_h, 0 );

		fp->exfunc->exec_multi_thread_func( multi_thread_func, fpip->ycp_edit, fp );

		int index = 0, count;
		for( int i = 0; i < block_v - 2; ++i )
		{
			for( int j = 0; j < block_h - 2; ++j )
			{
				count = 0;
				count += flag[index+j] + flag[index+j+1] + flag[index+j+2];
				count += flag[index+block_h+j] + flag[index+block_h+j+1] + flag[index+block_h+j+2];
				count += flag[index+2*block_h+j] + flag[index+2*block_h+j+1] + flag[index+2*block_h+j+2];
				if( count >= fp->track[1] )
					judge = true;
			}
			index += block_h;
		}

		if( judge )
			SetWindowText( fp->hwnd, _T("縞判定フィルタ [縞検出]") );
		else
			SetWindowText( fp->hwnd, _T("縞判定フィルタ") );
	}
	else // state != STATE_STOP
	{
		if( !fp->exfunc->set_ycp_filtering_cache_size( fp, fpip->max_w, fpip->h, 1, NULL ) )
			return FALSE;

		MSG msg;
		PIXEL_YC *ycp_edit;
		FRAME_STATUS fsp;
		TCHAR buf[64];
		clock_t start = clock(), now;
		int remain, hour, min, sec, detect = 0;

		// 選択範囲をチェック
		int select_s, select_e;
		fp->exfunc->get_select_frame( fpip->editp, &select_s, &select_e );
		if( frame < select_s || select_e <= frame )
		{
			if( state == STATE_SCAN_NEXT )
				SendMessage( fp->hwnd, WM_COMMAND, ID_SCAN_NEXT, 0 );
			else // state == STATE_SCAN_ALL
				SendMessage( fp->hwnd, WM_COMMAND, ID_SCAN_ALL, 0 );
			return TRUE;
		}

		if( state == STATE_SCAN_NEXT )
		{
			++frame;
			_stprintf_s( buf, _T("縞判定フィルタ 走査中 %d/%d"), frame + 1, frame_e );
			SetWindowText( fp->hwnd, buf );
		}
		else // state == STATE_SCAN_ALL
		{
			_stprintf_s( buf, _T( "全走査中 %d/%d"), frame + 1, frame_e );
			SetWindowText( fp->hwnd, buf );
		}

		// メインループ
		while( true )
		{
			fp->exfunc->set_frame( fpip->editp, frame );

			// メッセージ処理
			while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
				DispatchMessage( &msg );
			if( state == STATE_STOP )
				break;

			bool judge = false;
			flag.assign( block_v * block_h, 0 );

			ycp_edit = fp->exfunc->get_ycp_filtering_cache_ex( fp, fpip->editp, frame, NULL, NULL );
			fp->exfunc->exec_multi_thread_func( multi_thread_func, ycp_edit, fp );

			int index = 0, count;
			for( int i = 0; i < block_v - 2; ++i )
			{
				for( int j = 0; j < block_h - 2; ++j )
				{
					count = 0;
					count += flag[index+j] + flag[index+j+1] + flag[index+j+2];
					count += flag[index+block_h+j] + flag[index+block_h+j+1] + flag[index+block_h+j+2];
					count += flag[index+2*block_h+j] + flag[index+2*block_h+j+1] + flag[index+2*block_h+j+2];
					if( count >= fp->track[1] )
						judge = true;
				}
				index += block_h;
			}

			if( state == STATE_SCAN_NEXT )
			{
				if( judge )
					SetWindowText( fp->hwnd, _T("縞判定フィルタ [縞検出]") );
				else if( (frame & 15) == 0 )
				{
					_stprintf_s( buf, _T("縞判定フィルタ 走査中 %d/%d"), frame + 1, frame_e );
					SetWindowText( fp->hwnd, buf );
				}
			}
			else // state == STATE_SCAN_ALL
			{
				if( judge )
				{
					fp->exfunc->get_frame_status( fpip->editp, frame, &fsp );
					fsp.edit_flag |= EDIT_FRAME_EDIT_FLAG_MARKFRAME;
					fp->exfunc->set_frame_status( fpip->editp, frame, &fsp );
					++detect;
				}
				if( (frame & 15) == 0 )
				{
					if( frame != frame_s )
					{
						now = clock();
						remain = int( double( now - start ) * (frame_e - frame) / ((frame - frame_s) * CLOCKS_PER_SEC) );
						hour = remain / 3600, min = remain / 60 - hour * 60, sec = remain - hour * 3600 - min * 60;
						if( hour )
							_stprintf_s( buf, _T("全走査中 %d/%d 検出:%d 残り:%d時間%d分%d秒"), frame + 1, frame_e, detect, hour, min, sec );
						else if( min )
							_stprintf_s( buf, _T("全走査中 %d/%d 検出:%d 残り:%d分%d秒"), frame + 1, frame_e, detect, min, sec );
						else
							_stprintf_s( buf, _T("全走査中 %d/%d 検出:%d 残り:%d秒"), frame + 1, frame_e, detect, sec );
					}
					else // frame == frame_s
						_stprintf_s( buf, _T("全走査中 %d/%d"), frame, frame_e );
					SetWindowText( fp->hwnd, buf );
				}
			}
			
			if( (state == STATE_SCAN_NEXT && judge) || frame + 1 == frame_e || frame == select_e || !fp->exfunc->is_filter_active( fp ) )
				break;

			++frame;
		}

		if( state == STATE_SCAN_NEXT )
			SendMessage( fp->hwnd, WM_COMMAND, ID_SCAN_NEXT, 0 );
		if( state == STATE_SCAN_ALL )
			SendMessage( fp->hwnd, WM_COMMAND, ID_SCAN_ALL, 0 );
	}

	return TRUE;
}

// マルチスレッド処理関数
void multi_thread_func( int thread_id, int thread_num, void *param1, void *param2 )
{
	PIXEL_YC *ycp_edit	= (PIXEL_YC*)param1;
	FILTER *fp			= (FILTER*)param2;

	int block_v_s = block_v *  thread_id      / thread_num;
	int block_v_e = block_v * (thread_id + 1) / thread_num;

	_declspec(align(16)) const short ADD1[8] = {    0, 2048, 2048,    0, 2048, 2048,    0, 2048 };
	_declspec(align(16)) const short ADD2[8] = { 2048,    0, 2048, 2048,    0, 2048, 2048,    0 };
	_declspec(align(16)) const short ADD3[8] = { 2048, 2048,    0, 2048, 2048,    0, 2048, 2048 };
	_declspec(align(16)) const int   SUB1[4] = { adjust, 0, adjust, 0 };
	_declspec(align(16)) int dst[4];

	int sum1, sum2;
	PIXEL_YC *ycp = ycp_edit + (32 * block_v_s + offset_v) * max_w + offset_h, *ycp2 = ycp;
	
	for( int i = block_v_s; i < block_v_e; ++i )
	{
		for( int j = 0; j < block_h; ++j )
		{
			ycp = ycp2 + 32 * j;

			__asm
			{
				// esi: ソースポインタ
				// eax: 画像領域の幅のバイト数
				// ecx: ループカウンタ (横)
				// edx: ループカウンタ (縦)
				// xmm0 - xmm5: データ読み込み
				// xmm6, xmm7: 絶対差の和
				mov			esi, ycp
				mov			eax, pitch
				mov			edx, 32
				pxor		xmm6, xmm6
				pxor		xmm7, xmm7

nextline:
				mov			ecx, 2

next16pixels:
				movdqa		xmm0, [esi]
				movdqa		xmm1, [esi+16]
				movdqa		xmm2, [esi+eax]
				movdqa		xmm3, [esi+eax+16]
				movdqa		xmm4, [esi+2*eax]
				movdqa		xmm5, [esi+2*eax+16]
				paddw		xmm0, ADD1
				paddw		xmm1, ADD2
				paddw		xmm2, ADD1
				paddw		xmm3, ADD2
				paddw		xmm4, ADD1
				paddw		xmm5, ADD2
				psraw		xmm0, 4
				psraw		xmm1, 4
				psraw		xmm2, 4
				psraw		xmm3, 4
				psraw		xmm4, 4
				psraw		xmm5, 4
				packuswb	xmm0, xmm1
				packuswb	xmm2, xmm3
				packuswb	xmm4, xmm5
				movdqa		xmm1, xmm0
				psadbw		xmm0, xmm2
				psadbw		xmm1, xmm4
				psubusw		xmm0, SUB1
				psubusw		xmm1, SUB1
				paddw		xmm6, xmm0
				paddw		xmm7, xmm1

				movdqa		xmm0, [esi+32]
				movdqa		xmm1, [esi+48]
				movdqa		xmm2, [esi+eax+32]
				movdqa		xmm3, [esi+eax+48]
				movdqa		xmm4, [esi+2*eax+32]
				movdqa		xmm5, [esi+2*eax+48]
				paddw		xmm0, ADD3
				paddw		xmm1, ADD1
				paddw		xmm2, ADD3
				paddw		xmm3, ADD1
				paddw		xmm4, ADD3
				paddw		xmm5, ADD1
				psraw		xmm0, 4
				psraw		xmm1, 4
				psraw		xmm2, 4
				psraw		xmm3, 4
				psraw		xmm4, 4
				psraw		xmm5, 4
				packuswb	xmm0, xmm1
				packuswb	xmm2, xmm3
				packuswb	xmm4, xmm5
				movdqa		xmm1, xmm0
				psadbw		xmm0, xmm2
				psadbw		xmm1, xmm4
				psubusw		xmm0, SUB1
				psubusw		xmm1, SUB1
				paddw		xmm6, xmm0
				paddw		xmm7, xmm1

				movdqa		xmm0, [esi+64]
				movdqa		xmm1, [esi+80]
				movdqa		xmm2, [esi+eax+64]
				movdqa		xmm3, [esi+eax+80]
				movdqa		xmm4, [esi+2*eax+64]
				movdqa		xmm5, [esi+2*eax+80]
				paddw		xmm0, ADD2
				paddw		xmm1, ADD3
				paddw		xmm2, ADD2
				paddw		xmm3, ADD3
				paddw		xmm4, ADD2
				paddw		xmm5, ADD3
				psraw		xmm0, 4
				psraw		xmm1, 4
				psraw		xmm2, 4
				psraw		xmm3, 4
				psraw		xmm4, 4
				psraw		xmm5, 4
				packuswb	xmm0, xmm1
				packuswb	xmm2, xmm3
				packuswb	xmm4, xmm5
				movdqa		xmm1, xmm0
				psadbw		xmm0, xmm2
				psadbw		xmm1, xmm4
				psubusw		xmm0, SUB1
				psubusw		xmm1, SUB1
				paddw		xmm6, xmm0
				paddw		xmm7, xmm1

				add			esi, 96
				dec			ecx
				jnz			next16pixels

				mov			esi, ycp
				add			esi, pitch
				mov			ycp, esi
				dec			edx
				jnz			nextline

				psllq		xmm6, 32
				por			xmm6, xmm7
				movdqa		dst, xmm6
			}

			sum1 = dst[1] + dst[3];
			sum2 = dst[0] + dst[2];

			if( sum1 > rate * sum2 && sum1 > thrs )
			{
				flag[i*block_h+j] = 1;

				// 反転
				if( state == STATE_STOP && fp->check[0] )
				{
					ycp = ycp2 + max_w + 32 * j;
					for( int k = 0; k < 32; ++k )
					{
						for( int l = 0; l < 32; ++l )
						{
							ycp[l].y  = -ycp[l].y + 4096;
							ycp[l].cb = -ycp[l].cb;
							ycp[l].cr = -ycp[l].cr;
						}
						ycp += max_w;
					}
				}
			}
		}

		ycp2 += 32 * max_w;
		ycp = ycp2;
	}
}


//---------------------------------------------------------------------
//		ウィンドウメッセージ処理関数
//---------------------------------------------------------------------
BOOL func_WndProc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, void *editp, FILTER *fp )
{
	switch( msg )
	{
	case WM_FILTER_INIT:
		SYS_INFO si;
		fp->exfunc->get_sys_info( editp, &si );
		max_w = si.vram_w;
		pitch = si.vram_line_size;
		#define CreateButton( str, x, y, w, h, id ) \
			CreateWindowEx( 0, _T("BUTTON"), _T(str), WS_CHILD | WS_VISIBLE, x, y, w, h, hwnd, (HMENU)id, fp->dll_hinst, NULL )
		hbutton_scan_next = CreateButton( "走査",    20, 88, 60, 22, ID_SCAN_NEXT );
		hbutton_scan_all  = CreateButton( "全走査", 100, 88, 60, 22, ID_SCAN_ALL );
		SetWindowFont( hbutton_scan_next, si.hfont, 0 );
		SetWindowFont( hbutton_scan_all,  si.hfont, 0 );
		EnableWindow( hbutton_scan_next, false );
		EnableWindow( hbutton_scan_all,  false );
		state = STATE_STOP;
		return FALSE;

	case WM_FILTER_FILE_OPEN:
		EnableWindow( hbutton_scan_next, true );
		EnableWindow( hbutton_scan_all,  true );
		return FALSE;

	case WM_FILTER_FILE_CLOSE:
		EnableWindow( hbutton_scan_next, false );
		EnableWindow( hbutton_scan_all,  false );
		return FALSE;

	case WM_COMMAND:
		if( fp->exfunc->is_filter_active( fp ) )
		{
			switch( LOWORD( wparam ) )
			{
			case ID_SCAN_NEXT:
				if( state == STATE_STOP )
				{
					state = STATE_SCAN_NEXT;
					SetWindowText( hbutton_scan_next, _T("中止") );
					EnableWindow( hbutton_scan_all, false );
					return TRUE;
				}
				else // state != STATE_STOP
				{
					state = STATE_STOP;
					SetWindowText( hbutton_scan_next, _T("走査") );
					EnableWindow( hbutton_scan_all, true );
					return TRUE;
				}
				break;
			case ID_SCAN_ALL:
				if( state == STATE_STOP )
				{
					state = STATE_SCAN_ALL;
					SetWindowText( hbutton_scan_all, _T("中止") );
					EnableWindow( hbutton_scan_next, false );
					return TRUE;
				}
				else // state != STATE_STOP
				{
					state = STATE_STOP;
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
