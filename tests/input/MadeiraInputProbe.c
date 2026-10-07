#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <xinput.h>
#include <mmsystem.h>
#include <dinput.h>

static HANDLE report=INVALID_HANDLE_VALUE;
static HWND window;
static unsigned messages, rawkeys, asyncchanges, padchanges, dipads, pollcount;
static DWORD started, last_heartbeat;
static unsigned char keydown[256];
static char display[2048], line[320];
static int selftest;
typedef DWORD (WINAPI *GetPad)(DWORD,XINPUT_STATE*);
typedef HRESULT (WINAPI *CreateDI)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
static struct {const char *name; GetPad get; DWORD status[4]; XINPUT_STATE old[4];} api[3];
static const GUID di8a={0xbf798030,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};

void *memset(void *p,int v,SIZE_T n){unsigned char *b=p;while(n--)*b++=(unsigned char)v;return p;}
void *memcpy(void *d,const void *s,SIZE_T n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
int memcmp(const void *a,const void *b,SIZE_T n){const unsigned char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
static void logline(const char *s){DWORD wrote;char t[40];if(report!=INVALID_HANDLE_VALUE){wsprintfA(t,"[%lu ms] ",GetTickCount()-started);WriteFile(report,t,lstrlenA(t),&wrote,0);WriteFile(report,s,lstrlenA(s),&wrote,0);WriteFile(report,"\r\n",2,&wrote,0);FlushFileBuffers(report);}}
static BOOL CALLBACK enum_pad(const DIDEVICEINSTANCEA *d,void *ctx){(void)ctx;dipads++;wsprintfA(line,"DINPUT_DEVICE type=0x%lx name=%s",d->dwDevType,d->tszProductName);logline(line);return DIENUM_CONTINUE;}
static void init_apis(void){
    const char *names[3]={"xinput1_4.dll","xinput1_3.dll","xinput9_1_0.dll"};
    for(unsigned a=0;a<3;a++){
        HMODULE m=LoadLibraryA(names[a]);api[a].name=names[a];api[a].get=m?(GetPad)GetProcAddress(m,"XInputGetState"):0;
        wsprintfA(line,"API %s loaded=%u GetState=%u",names[a],m!=0,api[a].get!=0);logline(line);
        if(m){char path[MAX_PATH];if(GetModuleFileNameA(m,path,MAX_PATH)){logline("API_MODULE_PATH:");logline(path);}}
        if(api[a].get){XINPUT_STATE initial={0};DWORD r=api[a].get(0,&initial);wsprintfA(line,"STARTUP_PAD api=%s slot=0 status=%lu buttons=0x%x",names[a],r,initial.Gamepad.wButtons);logline(line);}
        for(unsigned i=0;i<4;i++)api[a].status[i]=~0u;
    }
    HMODULE dm=LoadLibraryA("dinput8.dll");
    CreateDI create=dm?(CreateDI)GetProcAddress(dm,"DirectInput8Create"):0;
    IDirectInput8A *di=0;
    HRESULT hr=create?create(GetModuleHandleA(0),0x0800,&di8a,(void**)&di,0):E_FAIL;
    if(SUCCEEDED(hr)&&di){hr=IDirectInput8_EnumDevices(di,DI8DEVCLASS_GAMECTRL,enum_pad,0,DIEDFL_ATTACHEDONLY);IDirectInput8_Release(di);}
    wsprintfA(line,"DINPUT_ENUM result=0x%lx count=%u",hr,dipads);logline(line);
    wsprintfA(line,"WINMM_SLOTS=%u",joyGetNumDevs());logline(line);
    for(unsigned i=0;i<4;i++){JOYINFOEX j={0};j.dwSize=sizeof(j);j.dwFlags=JOY_RETURNALL;MMRESULT r=joyGetPosEx(i,&j);wsprintfA(line,"WINMM index=%u result=%u buttons=0x%lx",i,r,j.dwButtons);logline(line);}
}
static void paint_status(void){
    wsprintfA(display,"Madeira input probe\r\n\r\nTap this window first. Try keyboard Esc / A / arrows.\r\nShow the Xbox controller layout, then press A / B / D-pad.\r\nHold one button for a second as well as tapping it.\r\n\r\nKeyboard messages: %u    Raw keyboard: %u\r\nKey-state changes: %u    Controller changes: %u\r\nDirectInput devices at startup: %u\r\n\r\nXInput 1.4: %lu / %lu / %lu / %lu\r\nXInput 1.3: %lu / %lu / %lu / %lu\r\nXInput 9.1: %lu / %lu / %lu / %lu\r\n(0 = connected; 1167 = no controller)\r\n\r\nReport: C:\\madeira-input-probe.txt\r\nClose this window after testing.",messages,rawkeys,asyncchanges,padchanges,dipads,
        api[0].status[0],api[0].status[1],api[0].status[2],api[0].status[3],
        api[1].status[0],api[1].status[1],api[1].status[2],api[1].status[3],
        api[2].status[0],api[2].status[1],api[2].status[2],api[2].status[3]);
    InvalidateRect(window,0,TRUE);
}
static void poll(void){
    BOOL foreground=GetForegroundWindow()==window;
    DWORD now=GetTickCount();
    if(now-last_heartbeat>=5000){last_heartbeat=now;wsprintfA(line,"HEARTBEAT foreground=%u key_messages=%u raw_keys=%u async_changes=%u",foreground,messages,rawkeys,asyncchanges);logline(line);}
    if(!foreground)return;
    for(unsigned k=1;k<256;k++){
        unsigned char down=(GetAsyncKeyState(k)&0x8000)!=0;
        if(down!=keydown[k]){keydown[k]=down;asyncchanges++;wsprintfA(line,"ASYNC_KEY vk=0x%x down=%u",k,down);logline(line);}
    }
    for(unsigned a=0;a<3;a++)for(unsigned i=0;i<4;i++){
        XINPUT_STATE s={0};DWORD r=api[a].get?api[a].get(i,&s):ERROR_MOD_NOT_FOUND;
        if(r!=api[a].status[i]||(!r&&memcmp(&s.Gamepad,&api[a].old[i].Gamepad,sizeof(s.Gamepad)))){
            padchanges++;api[a].status[i]=r;api[a].old[i]=s;
            wsprintfA(line,"PAD api=%s slot=%u status=%lu packet=%lu buttons=0x%x LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d",api[a].name,i,r,s.dwPacketNumber,s.Gamepad.wButtons,s.Gamepad.bLeftTrigger,s.Gamepad.bRightTrigger,s.Gamepad.sThumbLX,s.Gamepad.sThumbLY,s.Gamepad.sThumbRX,s.Gamepad.sThumbRY);logline(line);
        }
    }
    if((++pollcount%15)==0)paint_status();
}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_ACTIVATE:case WM_SETFOCUS:case WM_KILLFOCUS:
        wsprintfA(line,"FOCUS msg=0x%x active=%u",m,m==WM_ACTIVATE?LOWORD(wp):m==WM_SETFOCUS);logline(line);break;
    case WM_KEYDOWN:case WM_KEYUP:case WM_SYSKEYDOWN:case WM_SYSKEYUP:
        messages++;wsprintfA(line,"KEY_MESSAGE msg=0x%x vk=0x%lx scan=0x%lx",m,(DWORD)wp,(DWORD)((lp>>16)&255));logline(line);return 0;
    case WM_INPUT:{
        RAWINPUT r;UINT n=sizeof(r);
        if(GetRawInputData((HRAWINPUT)lp,RID_INPUT,&r,&n,sizeof(RAWINPUTHEADER))!=(UINT)-1&&r.header.dwType==RIM_TYPEKEYBOARD){rawkeys++;wsprintfA(line,"RAW_KEY vk=0x%x scan=0x%x flags=0x%x",r.data.keyboard.VKey,r.data.keyboard.MakeCode,r.data.keyboard.Flags);logline(line);}
        break;}
    case WM_TIMER:poll();return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);r.left+=18;r.top+=15;DrawTextA(dc,display,-1,&r,DT_LEFT|DT_TOP|DT_NOPREFIX);EndPaint(w,&p);return 0;}
    case WM_DESTROY:logline("END");PostQuitMessage(0);return 0;
    }
    return DefWindowProcA(w,m,wp,lp);
}
void mainCRTStartup(void){
    started=GetTickCount();
    const char *cmd=GetCommandLineA();
    for(const char *p=cmd;*p;p++)if(*p=='-'&&lstrcmpA(p,"--self-test")==0)selftest=1;
    report=CreateFileA(selftest?"madeira-input-selftest.txt":"C:\\madeira-input-probe.txt",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
    if(report==INVALID_HANDLE_VALUE){MessageBoxA(0,"Cannot write report file","Madeira input probe",MB_OK);ExitProcess(2);}
    logline("MADEIRA INPUT PROBE v1");
    if(!selftest){const char *keys[]={"SDL_JOYSTICK_RAWINPUT","MADEIRA_TOUCH_XINPUT","MADEIRA_XINPUT","MADEIRA_DINPUT_PAD","MADEIRA_PAD_EARLY_SLOT","MADEIRA_KEY_HOLD_MS","MADEIRA_CLICK_HOLD_MS"};for(unsigned i=0;i<7;i++){char value[80];DWORD n=GetEnvironmentVariableA(keys[i],value,sizeof(value));wsprintfA(line,"ENV %s=%s",keys[i],n&&n<sizeof(value)?value:"(unset)");logline(line);}}
    WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleA(0);wc.lpszClassName="MadeiraInputProbe";wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.hCursor=LoadCursorA(0,IDC_ARROW);
    RegisterClassA(&wc);window=CreateWindowExA(0,wc.lpszClassName,"Madeira input probe",WS_OVERLAPPEDWINDOW,60,50,850,590,0,0,wc.hInstance,0);
    if(!window){logline("CREATE_WINDOW_FAILED");CloseHandle(report);ExitProcess(3);}
    if(selftest){proc(window,WM_KEYDOWN,65,0x001e0001);proc(window,WM_KEYUP,65,0xc01e0001);logline(messages==2?"SELFTEST=PASS":"SELFTEST=FAIL");DestroyWindow(window);CloseHandle(report);ExitProcess(messages==2?0:4);}
    init_apis();RAWINPUTDEVICE rd={1,6,0,window};BOOL raw=RegisterRawInputDevices(&rd,1,sizeof(rd));wsprintfA(line,"RAW_KEYBOARD_REGISTER=%u error=%lu",raw,raw?0:GetLastError());logline(line);
    paint_status();ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(window);SetTimer(window,1,16,0);
    MSG msg;while(GetMessageA(&msg,0,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}
    CloseHandle(report);ExitProcess(0);
}
