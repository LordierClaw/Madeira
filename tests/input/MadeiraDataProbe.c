/* Diagnostic only: reads MSVCP addresses and writes C:\madeira-data-probe.txt.
 * No CRT startup dependency; kernel32 plus optional static MSVCP DATA imports. */
#include <windows.h>

#define COUNTER "?_Init_cnt@Init@ios_base@std@@0HA"
#define ACCESSOR "?_Init_cnt_func@Init@ios_base@std@@CAAEAHXZ"
#define COUT "?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A"
#ifdef STATIC_PROBE
extern __declspec(dllimport) int imported_counter __asm__(COUNTER);
extern __declspec(dllimport) unsigned char imported_cout[] __asm__(COUT);
#endif
static HANDLE out;
static void text(const char *s)
{
    DWORD n=0,w;
    while(s[n]) n++;
    WriteFile(out,s,n,&w,NULL);
}
static void hex(const char *label, ULONG_PTR value)
{
    static const char digits[]="0123456789abcdef";
    char buf[19]; int i;
    buf[0]='0';buf[1]='x';buf[18]=0;
    for(i=17;i>=2;i--) { buf[i]=digits[value&15]; value>>=4; }
    text(label);text(buf);text("\r\n");
}
static int same(const char *a,const char *b)
{
    while(*a && *a==*b) { a++;b++; }
    return *a==*b;
}
static FARPROC by_ordinal(HMODULE m,const char *name)
{
    BYTE *base=(BYTE *)m;
    IMAGE_DOS_HEADER *dos=(void *)base;
    IMAGE_NT_HEADERS64 *nt=(void *)(base+dos->e_lfanew);
    IMAGE_EXPORT_DIRECTORY *exp=(void *)(base+nt->OptionalHeader.DataDirectory[0].VirtualAddress);
    DWORD *names=(void *)(base+exp->AddressOfNames);
    WORD *ordinals=(void *)(base+exp->AddressOfNameOrdinals);
    DWORD i;
    for(i=0;i<exp->NumberOfNames;i++)
        if(same((char *)base+names[i],name))
            return GetProcAddress(m,(const char *)(ULONG_PTR)(exp->Base+ordinals[i]));
    return NULL;
}
void mainCRTStartup(void)
{
    HMODULE m;
    int *exported,*accessed;
    int *(__cdecl *accessor)(void);
    void *cout,*ordinal;
    int ok;
    out=CreateFileA("C:\\madeira-data-probe.txt",GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);
    if(out==INVALID_HANDLE_VALUE) ExitProcess(10);
    text("Madeira MSVCP data-export probe v2\r\n");
    m=LoadLibraryA("msvcp140.dll");
    if(!m) { hex("LOAD_ERROR=",GetLastError());CloseHandle(out);ExitProcess(11); }
    exported=(void *)GetProcAddress(m,COUNTER);
    accessor=(void *)GetProcAddress(m,ACCESSOR);
    cout=(void *)GetProcAddress(m,COUT);
    ordinal=(void *)by_ordinal(m,COUNTER);
    if(!exported || !accessor || !cout || !ordinal)
    { text("MISSING_EXPORT\r\n");CloseHandle(out);ExitProcess(12); }
    accessed=accessor();
    hex("module=",(ULONG_PTR)m);
    hex("counter_export=",(ULONG_PTR)exported);
    hex("counter_accessor=",(ULONG_PTR)accessed);
    hex("counter_ordinal=",(ULONG_PTR)ordinal);
    hex("cout_export=",(ULONG_PTR)cout);
    hex("cout_first_pointer=",*(ULONG_PTR *)cout);
    text(exported==accessed ? "COUNTER_ADDRESS_IDENTITY=SAME\r\n" : "COUNTER_ADDRESS_IDENTITY=DIFFERENT\r\n");
    text(ordinal==(void *)exported ? "ORDINAL_ADDRESS_IDENTITY=SAME\r\n" : "ORDINAL_ADDRESS_IDENTITY=DIFFERENT\r\n");
    ok=exported==accessed && ordinal==(void *)exported && *(ULONG_PTR *)cout!=0;
#ifdef STATIC_PROBE
    hex("counter_static_import=",(ULONG_PTR)&imported_counter);
    hex("cout_static_import=",(ULONG_PTR)imported_cout);
    text(&imported_counter==accessed && imported_cout==cout ? "STATIC_IMPORT_IDENTITY=SAME\r\n" : "STATIC_IMPORT_IDENTITY=DIFFERENT\r\n");
    ok=ok && &imported_counter==accessed && imported_cout==cout;
#endif
    text(ok ? "RESULT=PASS\r\n" : "RESULT=FAIL\r\n");
    text("END\r\n");CloseHandle(out);ExitProcess(ok?0:1);
}
