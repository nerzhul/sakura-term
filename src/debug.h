#pragma once

#define SAY(format,...) do {\
	if (strcmp("Debug", BUILDTYPE)==0) {\
	    fprintf(stderr, "[%d] ", getpid());\
	    fprintf(stderr, "[%s] ", __FUNCTION__);\
	    if (format) fprintf(stderr, format, ##__VA_ARGS__);\
	    fputc('\n', stderr);\
		fflush(stderr);\
	}\
} while (0)