/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"

int wezdesk_re::compile(LPCTSTR pattern)
{
	LPCTSTR end;
	TCHAR delim;
	int options = PCRE_UTF8;
	char *utf8pat = NULL;
	int buflen;
	const char *errptr = NULL;
	int erroffset = 0;
	int len;

	re = NULL;

	delim = *pattern;
	end = pattern + lstrlen(pattern) - 1;
	pattern++;

	while (*end != delim)
		--end;
	
	/* convert the pattern to utf-8 */
	buflen = (end - pattern + 1) * 6;
	utf8pat = (char*)malloc(buflen);
	
	len = WideCharToMultiByte(CP_UTF8, 0, pattern, end - pattern, utf8pat, buflen, NULL, NULL);
	utf8pat[len] = '\0';
	
	debug_printf(TEXT("%p: regex compile [%d] %s\r\n"), this, len, pattern);

	/* now look for flags */
	++end;
	while (*end) {
		switch (*end) {
			case 'i':	options |= PCRE_CASELESS; break;
			case 'm':	options |= PCRE_MULTILINE; break;
			case 's':	options |= PCRE_DOTALL; break;
			case 'x':	options |= PCRE_EXTENDED; break;
			case 'A':	options |= PCRE_ANCHORED; break;
			case 'D':	options |= PCRE_DOLLAR_ENDONLY; break;
			case 'U':	options |= PCRE_UNGREEDY; break;
			case 'X':	options |= PCRE_EXTRA; break;
		}
		++end;
	}

	re = pcre_compile(utf8pat, options, &errptr, &erroffset, NULL);

	free(utf8pat);

	if (re) {
		int num_subpats = 0;
		
		extra = pcre_study(re, 0, &errptr);

		if (pcre_fullinfo(re, extra, PCRE_INFO_CAPTURECOUNT, &num_subpats) < 0) {
			return 0;
		}

		num_subpats++;
		size_offsets = num_subpats * 3;
		offsets = (int*)malloc(size_offsets * sizeof(int));

		if (offsets) {
			return 1;
		}
	}
	
	core_funcs.ShowMessageBox(TEXT("config error"), TEXT("failed to compile regex"), MB_ICONERROR|MB_OK);

	return 0;
}

int wezdesk_re::match(LPCTSTR subject)
{
	char *utf8;
	int len, buflen;
	int count;
	
	buflen = (lstrlen(subject) + 1) * 6;
	utf8 = (char*)malloc(buflen);
	
	len = WideCharToMultiByte(CP_UTF8, 0, subject, lstrlen(subject), utf8, buflen, NULL, NULL);
	utf8[len] = '\0';
	count = pcre_exec(re, extra, utf8, len, 0, 0, offsets, size_offsets);
	free(utf8);
	
	debug_printf(TEXT("regex %p: match against %s ---> %d\r\n"), this, subject, count);

	return count;
}

wezdesk_re::~wezdesk_re()
{
	if (offsets) {
		free(offsets);
	}
	pcre_free(re);
	pcre_free(extra);
}
