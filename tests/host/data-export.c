/* Compile the actual loader helper with simulated PE/JIT mappings. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define WARN(...) ((void)0)
#define wcsnicmp _wcsnicmp
typedef struct { struct { UNICODE_STRING BaseDllName; } ldr; } WINE_MODREF;
static WINE_MODREF mock_module;
static unsigned char original_image[0x4000], jit_image[0x4000];
static int has_module = 1, has_metadata = 1, mapping = 1, roundtrip = 1;
static int disabled, translate_calls;
static WINE_MODREF *get_modref(HMODULE m) { return has_module ? &mock_module : NULL; }
static void *get_rva(HMODULE m, DWORD rva) { return (char *)m + rva; }
static void *arm64ec_get_module_metadata(HMODULE m) { return has_metadata ? original_image : NULL; }
static IMAGE_NT_HEADERS *mock_nt(HMODULE m) { return (IMAGE_NT_HEADERS *)original_image; }
static void mock_init(UNICODE_STRING *u, const WCHAR *p) { u->Buffer=(WCHAR *)p; u->Length=wcslen(p)*2; u->MaximumLength=u->Length+2; }
static NTSTATUS mock_env(void *env, UNICODE_STRING *name, UNICODE_STRING *value)
{
    if (!disabled) return (NTSTATUS)0xc0000100;
    value->Buffer[0]='0'; value->Length=2; return 0;
}
#define RtlImageNtHeader mock_nt
#define RtlInitUnicodeString mock_init
#define RtlQueryEnvironmentVariable_U mock_env
void *xlate_ios_jit(void *p)
{
    translate_calls++;
    if (!mapping) return p;
    if (mapping == 2) return NULL;
    return jit_image + ((char *)p - (char *)original_image);
}
void *xlate_ios_jit_rev(void *p)
{
    if (!roundtrip) return p;
    return original_image + ((char *)p - (char *)jit_image);
}
#include "data-export-under-test.inc"

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "valid";
    IMAGE_NT_HEADERS *nt = (void *)original_image;
    IMAGE_SECTION_HEADER *sec;
    DWORD rva=0x1010;
    void *expected=jit_image+rva;
    nt->OptionalHeader.SizeOfImage=sizeof(original_image);
    nt->FileHeader.SizeOfOptionalHeader=sizeof(nt->OptionalHeader);
    nt->FileHeader.NumberOfSections=1;
    sec=IMAGE_FIRST_SECTION(nt);
    sec->VirtualAddress=0x1000; sec->Misc.VirtualSize=0x100;
    sec->Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;
    mock_init(&mock_module.ldr.BaseDllName,L"MSVCP140.DLL");
    if (!strcmp(which,"other-dll")) { mock_init(&mock_module.ldr.BaseDllName,L"ucrtbase.dll"); expected=NULL; }
    else if (!strcmp(which,"wrong-name-length")) { mock_module.ldr.BaseDllName.Length-=2; expected=NULL; }
    else if (!strcmp(which,"no-module")) { has_module=0; expected=NULL; }
    else if (!strcmp(which,"native-x64")) { has_metadata=0; expected=NULL; }
    else if (!strcmp(which,"code")) { sec->Characteristics|=IMAGE_SCN_MEM_EXECUTE; expected=NULL; }
    else if (!strcmp(which,"readonly")) { sec->Characteristics=IMAGE_SCN_MEM_READ; expected=NULL; }
    else if (!strcmp(which,"unreadable")) { sec->Characteristics=IMAGE_SCN_MEM_WRITE; expected=NULL; }
    else if (!strcmp(which,"outside-image")) { rva=sizeof(original_image); expected=NULL; }
    else if (!strcmp(which,"headers")) { rva=0x100; expected=NULL; }
    else if (!strcmp(which,"section-end")) { rva=0x1100; expected=NULL; }
    else if (!strcmp(which,"last-byte")) { rva=0x10ff; expected=jit_image+rva; }
    else if (!strcmp(which,"zero-virtual-size")) { sec->Misc.VirtualSize=0; sec->SizeOfRawData=0x100; }
    else if (!strcmp(which,"identity")) { mapping=0; expected=original_image+rva; }
    else if (!strcmp(which,"null-map")) { mapping=2; expected=original_image+rva; }
    else if (!strcmp(which,"wrong-reverse-map")) { roundtrip=0; expected=original_image+rva; }
    else if (!strcmp(which,"disabled")) { disabled=1; expected=NULL; }
    void *got=(void *)madeira_msvcp_data_export((HMODULE)original_image,rva);
    if (got!=expected || (!expected && translate_calls))
    { fprintf(stderr,"FAIL %s got=%p expected=%p calls=%d\n",which,got,expected,translate_calls); return 1; }
    printf("PASS %s\n",which); return 0;
}
