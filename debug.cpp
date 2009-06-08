/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"

#define DBGHELP_TRANSLATE_TCHAR
#ifdef _WIN32
# ifndef __out_xcount
#  define __out_xcount(x)
# endif
# ifndef __out_ecount_opt
#  define __out_ecount_opt(x)
# endif
#endif

/* the debug helper APIs all *require* 8 byte structure packing,
 * but don't guarantee it by doing this for themselves. */
#include <pshpack8.h>
#include <dbghelp.h>
#include <poppack.h>

static BOOL CALLBACK minidump_callback(void *unused,
  PMINIDUMP_CALLBACK_INPUT input,
  PMINIDUMP_CALLBACK_OUTPUT output)
{
  if (!input || !output) return FALSE;

  switch (input->CallbackType) {
    case IncludeModuleCallback:
    case IncludeThreadCallback:
    case ThreadCallback:
    case ThreadExCallback:
      return TRUE;

    case ModuleCallback:
      if (!(output->ModuleWriteFlags & ModuleReferencedByMemory)) {
        /* not referenced, so exclude it from the dump */
        output->ModuleWriteFlags &= ~ModuleWriteModule;
      }
      return TRUE;

    default:
      return FALSE;
  }
}

LONG WINAPI crash_dumper(PEXCEPTION_POINTERS info)
{
  TCHAR trace_filename[MAX_PATH];
  HANDLE f;
  SECURITY_ATTRIBUTES sa;
  MINIDUMP_EXCEPTION_INFORMATION mdei;
  MINIDUMP_CALLBACK_INFORMATION mci;

  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

#define TRC_SUFFIX TEXT(".dmp")

  StringCbPrintf(trace_filename, sizeof(trace_filename),
    TEXT("%s\\wezdesk-%d-trace") TRC_SUFFIX,
    base_dir, GetCurrentProcessId());

  f = CreateFile(trace_filename, GENERIC_READ|GENERIC_WRITE,
        0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  if (f == INVALID_HANDLE_VALUE) {
    /* try dumping it to a temporary dir */
    TCHAR tmp[MAX_PATH];
		GetTempPath(sizeof(tmp)/sizeof(tmp[0]), tmp);
    StringCbPrintf(trace_filename, sizeof(trace_filename),
        TEXT("%s\\wezdesk-%d-trace") TRC_SUFFIX,
        tmp, GetCurrentProcessId());
    f = CreateFile(trace_filename, GENERIC_READ|GENERIC_WRITE,
          0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (f == INVALID_HANDLE_VALUE) {
      /* dump to stderr instead... it might work */
      f = GetStdHandle(STD_OUTPUT_HANDLE);
      if (f == NULL || f == INVALID_HANDLE_VALUE) {
        /* punt */
        return EXCEPTION_EXECUTE_HANDLER;
      }
    }
  }
  
  memset(&mdei, 0, sizeof(mdei));
  memset(&mci, 0, sizeof(mci));

  mdei.ThreadId = GetCurrentThreadId();
  mdei.ExceptionPointers = info;
  mdei.ClientPointers = FALSE;

  mci.CallbackRoutine = minidump_callback;

  MiniDumpWriteDump(GetCurrentProcess(),
    GetCurrentProcessId(), f,
    (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
    info ? &mdei : 0, 0, &mci);

  CloseHandle(f);
  return EXCEPTION_EXECUTE_HANDLER;
}

/* vim:sw=2:ts=2:et:
 * */
