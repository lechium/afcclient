//
//  afcclient.h
//  TestApp
//
//  Created by Kevin Bradley on 10/21/14.
//
//


#ifndef _afcclient_h
#define _afcclient_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "libimobiledevice/afc.h"
#include "plist/plist.h"

#ifdef __cplusplus
#	define EXT_C extern "C"
#else
#	define EXT_C extern
#endif
    
#ifdef LIBGMMD_DYNAMIC
#	define LIBAFC_DYNAMIC 1
#endif
    
#ifdef _WIN32
#define PATH_MAX 256
#	ifdef LIBAFC_DYNAMIC
#		ifdef LIBGMMD_EXPORTS
#			define LIBGMMD_EXPORT EXT_C __declspec(dllexport)
#		else
#			define LIBGMMD_EXPORT EXT_C __declspec(dllimport)
#		endif
#	else
#		define LIBGMMD_EXPORT EXT_C
#	endif
#else
#	define LIBGMMD_EXPORT EXT_C __attribute__((visibility("default")))
#endif
    
LIBGMMD_EXPORT int list_devices(FILE *outf);
    
LIBGMMD_EXPORT int rm_file(afc_client_t afc, char *filePath);
LIBGMMD_EXPORT plist_t * afc_file_info_for_path(afc_client_t afc, const char *path);
LIBGMMD_EXPORT int dump_afc_file_info(afc_client_t afc, const char *path);
LIBGMMD_EXPORT plist_t * afc_list_path(afc_client_t afc, const char *path, int8_t recursive);
LIBGMMD_EXPORT int get_afc_path(afc_client_t afc, const char *src, const char *dst);
LIBGMMD_EXPORT int put_afc_path(afc_client_t afc, const char *src, const char *dst);
LIBGMMD_EXPORT int clone_afc_path(afc_client_t afc, const char *src, const char *dst);

    
#ifdef __cplusplus
}
#endif
#endif
