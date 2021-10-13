/*
 * afcclient
 * Date: Nov 2013
 * Eric Monti - esmonti at gmail dot com
 *
 * A simple CLI interface to AFC via libimobiledevice.
 *
 * Build with 'make'
 *
 * run "afcclient -h" for usage.
 *
 * Updated by Kevin Bradley with massive updates for improved functionality
 * and easier windows support.
 *
 * Major additions include:
 *
 * 1. the ability to make carbon copy clones of documents folder
 * 2. use libimobiledevice to list all attached iOS devices
 * 3. print plist formatted recursive documents list
 * 4. Added recursive listing support
 * 5. Made the standard listing output a lot nicer looking (attempted to make it look like ls -al)
 */

#include "afcclient.h"

#ifdef __linux
#include <limits.h>
#endif

#ifdef __APPLE__
#include <sys/syslimits.h>
#endif

#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "libidev.h"

#include <sys/stat.h>
#include <sys/types.h>

#define CHUNKSZ 8192

#pragma mark - AFC Implementation Utility Functions

char *progname;
bool hasAppID;
bool clean;
bool recursiveList;
bool root;
bool xml; //output as xml
bool fs; //output only apps that have file sharing capabilities
char *udid;
bool appMode;
bool quiet;
int _relativeYear;
char * AFVersionNumber = "1.0.1";

void usage(FILE *outf);

//loadBar(totbytes, fsize, 50,basename((char*)src));

// A nice loading bar. Credits: classdump-dyld
static inline void loadBar(off_t currentValue, off_t totalValue, int width,const char *fileName) {
    if (quiet) return;
    // Calculuate the ratio of complete-to-incomplete.
    float ratio = currentValue/(float)totalValue;
    int   elapsed     = ratio * width;
    // Show the percentage complete.
    printf("%3d%% [", (int)(ratio*100) );
    
    // Show the load bar.
    for (int x=0; x<elapsed; x++)
        printf("=");
    
    for (int x=elapsed; x<width; x++)
        printf(" ");
    
    // ANSI Control codes to go back to the
    // previous line and clear it.
    printf("] %.0lld/%.0lld MB <%s>\n\033[F\033[J",currentValue/1024/1024,totalValue/1024/1024,fileName);
}

bool fileExists(const char* file) {
    struct stat buf;
    return (stat(file, &buf) == 0);
}

bool is_dir(char *path) {
    struct stat s;
    return (stat(path, &s) == 0 && s.st_mode & S_IFDIR);
}

int dump_afc_device_info(afc_client_t afc) {
    int ret=EXIT_FAILURE;
    char **infos=NULL;
    afc_error_t err=afc_get_device_info(afc, &infos);
    if (err == AFC_E_SUCCESS && infos) {
        int i;
        printf("AFC Device Info: -");
        for (i=0; infos[i]; i++)
        printf("%c%s", ((i%2)? ':' : ' '), infos[i]);
        printf("\n");
        ret = EXIT_SUCCESS;
    } else {
        fprintf(stderr, "Error: afc get device info failed: %s\n", idev_afc_strerror(err));
    }
    if (infos)
        idevice_device_list_free(infos);
    return ret;
}

int currentRelativeYear() {
    time_t rawtime;
    struct tm *info;
    time( &rawtime );
    info = localtime( &rawtime );
    return info->tm_year;
}

void epochToTime(long epoch, char* s) {
    const time_t rawtime = epoch/1000000000;
    struct tm * dt;
    dt = localtime(&rawtime);
    char slocal[100];
    if (dt->tm_year != _relativeYear){
        strftime(slocal,sizeof(slocal),"%b %d  %G", dt);
    } else {
        strftime(slocal,sizeof(slocal),"%b %d %R", dt);
    }
    strncpy(s, slocal, 99);
    s[99] = '\0';
}

/*
 
 get plist formatted information for a afc file path
 
 Path = "Documents/TestFolder/Science/test1.txt";
 "st_birthtime" = 1413989837;
 "st_blocks" = 0;
 "st_ifmt" = "S_IFREG";
 "st_mtime" = 1413989837;
 "st_nlink" = 1;
 "st_size" = 0;
 
 */

plist_t * afc_file_info_for_path(afc_client_t afc, const char *path) {
    
    int i, ret=EXIT_FAILURE;
    char **infolist=NULL;
    plist_t currentDevicePlist = plist_new_dict();
    afc_error_t err = afc_get_file_info(afc, path, &infolist);
    
    if (err == AFC_E_SUCCESS && infolist) {
        plist_dict_set_item(currentDevicePlist, "path", plist_new_string(path));
        int arraySize = 0;
        for(i=0; infolist[i]; i++) {
            arraySize++;
        }
        for(i=0; infolist[i]; i++) {
            if (i+1 < arraySize && (i % 2 == 0)) {
                if (strcmp("st_birthtime", infolist[i])== 0 || strcmp("st_mtime", infolist[i]) == 0) {
                    char s[100];
                    epochToTime(atol(infolist[i+1]),s);
                    plist_dict_set_item(currentDevicePlist, infolist[i], plist_new_string(s));
                } else {
                    plist_dict_set_item(currentDevicePlist, infolist[i], plist_new_string(infolist[i+1]));
                }
            }
        }
        // printf("\t%s\n %i", path, i);
        ret=EXIT_SUCCESS;
        
    } else {
        fprintf(stderr, "Error: info error for path: %s - %s\n", path, idev_afc_strerror(err));
    }
    if (infolist)
        idevice_device_list_free(infolist);
    
    return currentDevicePlist;
    
}

/*
 
 plist recursive array of the root documents folder
 
 */

plist_t * afc_list_path(afc_client_t afc, const char *path, int8_t recursive) {
    int ret=EXIT_FAILURE;
    char **list=NULL;
    plist_t fileList = plist_new_array();
    if (idev_verbose)
        fprintf(stderr, "[debug] reading afc directory contents at \"%s\"\n", path);
    
    afc_error_t err = afc_read_directory(afc, path, &list);
    
    if (err == AFC_E_SUCCESS && list) {
        int i;
        if (idev_verbose){
            printf("AFC Device Listing path=\"%s\":\n", path);
        }
        for (i=0; list[i]; i++) {
            char tpath[PATH_MAX], *lpath;
            if (!strcmp(path, "")) {
                lpath=list[i];
            } else {
                if (path[strlen(path)-1]=='/') {
                    snprintf(tpath, PATH_MAX-1, "%s%s", path, list[i]);
                } else {
                    snprintf(tpath, PATH_MAX-1, "%s/%s", path, list[i]);
                }
                lpath = tpath;
            }
            if (strcmp(list[i], ".") != 0 && strcmp(list[i], "..") != 0) {
                plist_t *fileInfo = afc_file_info_for_path(afc, lpath);
                
                //if is a directory recurse into it
                plist_t fmt_node = plist_dict_get_item(fileInfo, "st_ifmt");
                char *fmt = NULL;
                plist_get_string_val(fmt_node, &fmt);
                
                if ((strcmp(fmt, "S_IFDIR") == 0) && (recursive == true)){
                    plist_t infoCopy = plist_copy(fileInfo);
                    plist_array_append_item(fileList, infoCopy);
                    if (idev_verbose){
                        printf("%s folder, recurse!!\n", lpath);
                    }
                    plist_t *dirInfo = afc_list_path(afc, lpath, true);
                    //          printf("lpath: %s\n", lpath);
                    uint32_t arrayCount = plist_array_get_size(dirInfo);
                    int j;
                    for (j = 0; j < arrayCount; j++){
                        //  printf("j: %i\n", j);
                        plist_t arrayItem = NULL;
                        arrayItem = plist_array_get_item(dirInfo, j);
                        if (arrayItem != NULL) {
                            //need to make a copy of the item for some reason... things just disappear otherwise
                            plist_t itemCopy = plist_copy(arrayItem);
                            plist_array_append_item(fileList, itemCopy);
                        }
                    }
                    
                } else if (strcmp(fmt, "S_IFREG") == 0) { //not a directory, just a file
                    if (idev_verbose){
                        printf("%s file, treat normally!!\n", lpath);
                    }
                    plist_array_append_item(fileList, fileInfo);
                } else if (strcmp(fmt, "S_IFLNK") == 0) {
                    if (idev_verbose){
                        printf("%s symbolic link, treat normally!!\n", lpath);
                    }
                    plist_array_append_item(fileList, fileInfo);
                }
            }
        }
        
        ret=EXIT_SUCCESS;
    } else if (err == AFC_E_READ_ERROR) { // fall-back to doing a file info request, incase its a file
        if (idev_verbose)
            fprintf(stderr, "[debug] directory read error -- falling back to file info at %s\n", path);
        
        ret = dump_afc_file_info(afc, path);
    } else {
        fprintf(stderr, "Error: afc list \"%s\" failed: %s\n", path, idev_afc_strerror(err));
    }
    
    if (list)
        free(list);
    
    
    return fileList;
}

int dump_afc_file_info_old(afc_client_t afc, const char *path) {
    int i, ret=EXIT_FAILURE;
    
    char **infolist=NULL;
    afc_error_t err = afc_get_file_info(afc, path, &infolist);
    
    if (err == AFC_E_SUCCESS && infolist) {
        for(i=0; infolist[i]; i++)
        printf("%c%s", ((i%2)? '=' : ' '), infolist[i]);
        
        printf("\t%s\n", path);
        ret=EXIT_SUCCESS;
        
    } else {
        fprintf(stderr, "Error: info error for path: %s - %s\n", path, idev_afc_strerror(err));
    }
    
    if (infolist)
        idevice_device_list_free(infolist);
    
    return ret;
}

/*
 
 much prettier now!
 
 d    61          1952    Feb 28 12:22    bin
 d     2            64    Oct 28 07:07    boot
 d     2            64    Oct 02 19:38    cores
 d     4          1707    Sep 24 13:00    dev
 l     1            11    Mar 14 05:24    etc -> private/etc
 
 */
int dump_afc_file_info(afc_client_t afc, const char *path) {
    //return dump_afc_file_info_old(afc, path);
    int ret=EXIT_FAILURE;
    if (strcmp(path, "..") == 0 || strcmp(path, "." ) == 0 || strstr(path, "/..") || strstr(path, "/.")){
        return ret;
    }
    
    if (xml) {
        plist_t *documentList = afc_list_path(afc, path, recursiveList);
        char *xmlData = NULL;
        uint32_t length = 0;
        plist_to_xml(documentList, &xmlData, &length);
        printf("%s", xmlData);
        return 0;
        //fprintf(outf, "%s\n", xmlData);
    }
    
    plist_t *node = afc_file_info_for_path(afc, path);
    if (node){
        char displayString[PATH_MAX];
        char type = '\0';
        char *fmt = NULL;
        char *link = NULL;
        char *size = NULL;
        char *time = NULL;
        char *lt = NULL;
        bool isLink = false;
        bool isDirectory = false;
        plist_get_string_val(plist_dict_get_item(node, "st_ifmt"), &fmt);
        if (fmt){
            if (strcmp(fmt, "S_IFREG") == 0){
                type = 'f';
            } else if (strcmp(fmt,"S_IFDIR") == 0) {
                type = 'd';
                isDirectory = true;
            } else if (strcmp(fmt,"S_IFLNK") == 0) {
                isLink = true;
                type = 'l';
            }
        } else {
            return ret;
        }
        plist_get_string_val(plist_dict_get_item(node, "st_nlink"), &link);
        plist_get_string_val(plist_dict_get_item(node, "st_size"), &size);
        long longsize = atol(size);
        long longlink = atol(link);
        plist_get_string_val(plist_dict_get_item(node, "st_mtime"), &time);
        
        if (isLink){
            plist_get_string_val(plist_dict_get_item(node, "LinkTarget"), &lt);
            snprintf(displayString, PATH_MAX-1, "%c %5ld\t%10ld\t%s\t%s -> %s", type, longlink, longsize, time, path, lt);
        } else {
            snprintf(displayString, PATH_MAX-1, "%c %5ld\t%10ld\t%s\t%s", type, longlink, longsize, time, path);
        }
        printf("%s\n", displayString);
        if (isDirectory && recursiveList){
            dump_afc_list_path(afc, path);
        }
        ret=EXIT_SUCCESS;
    } else {
        //fprintf(stderr, "Error: info error for path: %s - %s\n", path, idev_afc_strerror(err));
    }
    return ret;
}

int dump_afc_list_path(afc_client_t afc, const char *path) {
    int ret=EXIT_FAILURE;
    char **list=NULL;
    if (idev_verbose)
        fprintf(stderr, "[debug] reading afc directory contents at \"%s\"\n", path);
    
    afc_error_t err = afc_read_directory(afc, path, &list);
    if (err == AFC_E_SUCCESS && list) {
        int i;
        if (idev_verbose){
            printf("AFC Device Listing path=\"%s\":\n", path);
        }
        for (i=0; list[i]; i++) {
            char tpath[PATH_MAX], *lpath;
            if (!strcmp(path, "")) {
                lpath=list[i];
            } else {
                if (path[strlen(path)-1]=='/') {
                    snprintf(tpath, PATH_MAX-1, "%s%s", path, list[i]);
                } else {
                    snprintf(tpath, PATH_MAX-1, "%s/%s", path, list[i]);
                }
                lpath = tpath;
            }
            
            dump_afc_file_info(afc, lpath);
        }
        
        ret=EXIT_SUCCESS;
    } else if (err == AFC_E_READ_ERROR) { // fall-back to doing a file info request, incase its a file
        if (idev_verbose)
            fprintf(stderr, "[debug] directory read error -- falling back to file info at %s\n", path);
        
        ret = dump_afc_file_info(afc, path);
    } else {
        fprintf(stderr, "Error: afc list \"%s\" failed: %s\n", path, idev_afc_strerror(err));
    }
    
    if (list)
        free(list);
    
    return ret;
}


int dump_afc_path(afc_client_t afc, const char *path, FILE *outf) {
    int ret=EXIT_FAILURE;
    uint64_t handle=0;
    if (idev_verbose)
        fprintf(stderr, "[debug] creating afc file connection to %s\n", path);
    
    afc_error_t err = afc_file_open(afc, path, AFC_FOPEN_RDONLY, &handle);
    
    if (err == AFC_E_SUCCESS) {
        char buf[CHUNKSZ];
        uint32_t bytes_read=0;
        
        while((err=afc_file_read(afc, handle, buf, CHUNKSZ, &bytes_read)) == AFC_E_SUCCESS && bytes_read > 0) {
            fwrite(buf, 1, bytes_read, outf);
        }
        
        if (err)
            fprintf(stderr, "Error: Encountered error while reading %s: %s\n", path, idev_afc_strerror(err));
        else
            ret=EXIT_SUCCESS;
        
        afc_file_close(afc, handle);
    } else {
        fprintf(stderr, "Error: afc open file %s failed: %s\n", path, idev_afc_strerror(err));
    }
    return ret;
}

/*
 i think text files writing in binary will not be okay,
 but windows fopen defaults to text, so files get corrupt
 so this gets called every time we use fopen, make
 sure our plist files can get read back and forth without
 trouble.
 
 right now only focusing on plist, doubt it should matter with anything else.
 
 */

char * write_mode_for_file(char *filename) {
    char* ret = "w";
#if defined(_WIN32)
    char* ext;
    ext = strrchr(filename,'.');
    printf("%s extension: %s\n", filename, ext);
    if (strcmp(".plist", ext) == 0 || strcmp(".PLIST", ext) == 0)
    {
        ret = "w";
    } else {
        ret = "wb";
    }
#endif
    return ret;
}


/*
 (
 {
 Path = "Documents/.";
 "st_birthtime" = 1409934234000000000;
 "st_blocks" = 0;
 "st_ifmt" = "S_IFDIR";
 "st_mtime" = 1409934239000000000;
 "st_nlink" = 2;
 "st_size" = 102;
 },
 {
 Path = "Documents/..";
 "st_birthtime" = 1409934234000000000;
 "st_blocks" = 0;
 "st_ifmt" = "S_IFDIR";
 "st_mtime" = 1409934234000000000;
 "st_nlink" = 6;
 "st_size" = 204;
 },
 {
 Path = "Documents/config.txt";
 "st_birthtime" = 1409865203000000000;
 "st_blocks" = 8;
 "st_ifmt" = "S_IFREG";
 "st_mtime" = 1409865203000000000;
 "st_nlink" = 1;
 "st_size" = 29;
 }
 )
 
 
 this method will loop recursively through the entire src path and output it to the destination
 TODO: check for permissions on file folder creation on the selected directory.
 
 */

int clone_afc_path(afc_client_t afc, const char *src, const char *dst) {
    int ret=EXIT_FAILURE;
    
    if (idev_verbose)
        fprintf(stderr, "[debug] Cloning %s to %s - creating afc file connection\n", src, dst);
    
    plist_t fileList = afc_list_path(afc, src, true);
    int fileCount = plist_array_get_size(fileList);
    if (idev_verbose)
        printf("fileCount: %i\n", fileCount);
    
    int i;
    //windows mkdir is different!
    
#if defined(_WIN32)
    
    _mkdir(dst);
    
#else
    mkdir(dst, 0777); // notice that 777 is different than 0777
    
#endif
    
    for (i = 0; i < fileCount; i++) {
        char *path = NULL, *fmt = NULL, *size = NULL;
        plist_t item = plist_array_get_item(fileList, i);
        plist_get_string_val(plist_dict_get_item(item, "path"), &path);
        plist_get_string_val(plist_dict_get_item(item, "st_ifmt"), &fmt);
        plist_get_string_val(plist_dict_get_item(item, "st_size"), &size);
        long fsize = atol(size);
        char newPath[PATH_MAX], sys[PATH_MAX];
        
        if (strcmp(fmt, "S_IFDIR") == 0) {
            sprintf(newPath,"%s/%s/",dst, path);
            sprintf(sys, "/bin/mkdir -p %s", newPath); //using mkdir -p since theres no easy way to use mkdir() to create intermediate paths
#if defined(_WIN32)
            _mkdir(newPath);
            
#else
            printf("%s\n", sys);
            system(sys);
            mkdir(newPath, 0777); // notice that 777 is different than 0777
#endif
            printf("mkdir at new path: %s\n", newPath);
        } else {
           
            sprintf(newPath,"%s/%s",dst, path);
            char *dir = dirname(path);
            if (!fileExists(dir)) {
                char sys[PATH_MAX];
                sprintf(sys, "/bin/mkdir -p %s", dir);
                system(sys);
            }
            printf("copy file to new path: %s\n", newPath);
            //copy the file!
            uint64_t handle=0;
            afc_error_t err = afc_file_open(afc, path, AFC_FOPEN_RDONLY, &handle);
            
            if (err == AFC_E_SUCCESS) {
                
                char *writeMode = write_mode_for_file(path);
                
                char buf[CHUNKSZ];
                uint32_t bytes_read=0;
                size_t totbytes=0;
                
                FILE *outf = fopen(newPath, writeMode);
                if (outf) {
                    while((err=afc_file_read(afc, handle, buf, CHUNKSZ, &bytes_read)) == AFC_E_SUCCESS && bytes_read > 0) {
                        totbytes += fwrite(buf, 1, bytes_read, outf);
                        if (fsize > 0){
                            loadBar(totbytes, fsize, 50,basename((char*)newPath));
                        }
                    }
                    fclose(outf);
                    if (err) {
                        fprintf(stderr, "Error: Encountered error while reading %s: %s\n", path, idev_afc_strerror(err));
                        fprintf(stderr, "Warning! - %lu bytes read - incomplete data in %s may have resulted.\n", totbytes, newPath);
                    } else {
                        printf("Saved %lu bytes to %s\n", totbytes, newPath);
                        ret=EXIT_SUCCESS;
                    }
                    
                } else {
                    fprintf(stderr, "Error opening local file for writing: %s - %s\n", newPath, strerror(errno));
                }
                
                afc_file_close(afc, handle);
                // i assume you need to wait till afc_file_close to actually delete a file
                 if (ret == EXIT_SUCCESS) {
                    if (clean == true) {
                        fprintf(stderr, "File cloned successfully, clearing original: %s\n", path);
                        rm_file(afc, (char*)path);
                    }
                }
                
            } else {
                fprintf(stderr, "Error: afc open file %s failed: %s\n", path, idev_afc_strerror(err));
            }
            
        }
    }
    return ret;
}

int export_shallow_folder(afc_client_t afc, const char *src, const char *dst) {
    int ret=EXIT_FAILURE;
    
    if (idev_verbose)
        fprintf(stderr, "[debug] exporting %s to %s - creating afc file connection\n", src, dst);
    
    plist_t fileList = afc_list_path(afc, src, false);
    int fileCount = plist_array_get_size(fileList);
    
    if (idev_verbose)
        printf("fileCount: %i\n", fileCount);
    
    int i;
    
    for (i = 0; i < fileCount; i++) {
        char *path = NULL;
        plist_t item = plist_array_get_item(fileList, i);
        plist_t path_node = plist_dict_get_item(item, "path");
        plist_t fmt_node = plist_dict_get_item(item, "st_ifmt");
        char *fmt = NULL;
        char *size = NULL;
        plist_get_string_val(plist_dict_get_item(item, "st_size"), &size);
        long fsize = atol(size);
        plist_get_string_val(fmt_node, &fmt);
        plist_get_string_val(path_node, &path);
        char newPath[PATH_MAX];
        
        if (strcmp(fmt, "S_IFDIR") != 0) {
            sprintf(newPath,"%s/%s",dst, basename(path));
            printf("copy file to new path: %s\n", newPath);
            //copy the file!
            uint64_t handle=0;
            afc_error_t err = afc_file_open(afc, path, AFC_FOPEN_RDONLY, &handle);
            
            if (err == AFC_E_SUCCESS) {
                
                char *writeMode = write_mode_for_file(path);
                char buf[CHUNKSZ];
                uint32_t bytes_read=0;
                size_t totbytes=0;
                
                FILE *outf = fopen(newPath, writeMode);
                if (outf) {
                    while((err=afc_file_read(afc, handle, buf, CHUNKSZ, &bytes_read)) == AFC_E_SUCCESS && bytes_read > 0) {
                        totbytes += fwrite(buf, 1, bytes_read, outf);
                        if (fsize > 0){
                            loadBar(totbytes, fsize, 50,basename((char*)src));
                        }
                    }
                    fclose(outf);
                    if (err) {
                        fprintf(stderr, "Error: Encountered error while reading %s: %s\n", path, idev_afc_strerror(err));
                        fprintf(stderr, "Warning! - %lu bytes read - incomplete data in %s may have resulted.\n", totbytes, newPath);
                    } else {
                        printf("Saved %lu bytes to %s\n", totbytes, newPath);
                        ret=EXIT_SUCCESS;
                    }
                    
                } else {
                    fprintf(stderr, "Error opening local file for writing: %s - %s\n", newPath, strerror(errno));
                }
                
                afc_file_close(afc, handle);
                /*
                 
                 i assume you need to wait till afc_file_close to actually delete a file
                 
                 TODO: make it so if we are done with a folder and it is empty, we clear it out!
                 
                 */
                if (ret == EXIT_SUCCESS) {
                    if (clean == true) {
                        fprintf(stderr, "File cloned successfully, clearing original: %s\n", path);
                        rm_file(afc, (char*)path);
                    }
                }
                
            } else {
                fprintf(stderr, "Error: afc open file %s failed: %s\n", path, idev_afc_strerror(err));
            }
            
        }
    }
    return ret;
}

//if theres ever a need just to grab a single file and not do a whole clone, this can be used.

int get_afc_path(afc_client_t afc, const char *src, const char *dst) {
    int ret=EXIT_FAILURE;
    
    if (idev_verbose)
        fprintf(stderr, "[debug] Downloading %s to %s - creating afc file connection\n", src, dst);
    
    uint64_t handle=0;
    plist_t *node = afc_file_info_for_path(afc, src);
    off_t fsize = 0;
    if (node){
        char *size = NULL;
        plist_get_string_val(plist_dict_get_item(node, "st_size"), &size);
        fsize = atol(size);
    }
    afc_error_t err = afc_file_open(afc, src, AFC_FOPEN_RDONLY, &handle);
    
    if (err == AFC_E_SUCCESS) {
        char buf[CHUNKSZ];
        uint32_t bytes_read=0;
        size_t totbytes=0;
        char *writeMode = write_mode_for_file((char*)src);
        FILE *outf = fopen(dst, writeMode);
        if (outf) {
            while((err=afc_file_read(afc, handle, buf, CHUNKSZ, &bytes_read)) == AFC_E_SUCCESS && bytes_read > 0) {
                totbytes += fwrite(buf, 1, bytes_read, outf);
                if (fsize > 0){
                    loadBar(totbytes, fsize, 50,basename((char*)src));
                }
            }
            fclose(outf);
            if (err) {
                fprintf(stderr, "Error: Encountered error while reading %s: %s\n", src, idev_afc_strerror(err));
                fprintf(stderr, "Warning! - %lu bytes read - incomplete data in %s may have resulted.\n", totbytes, dst);
            } else {
                printf("Saved %lu bytes to %s\n", totbytes, dst);
                ret=EXIT_SUCCESS;
            }
            
        } else {
            fprintf(stderr, "Error opening local file for writing: %s - %s\n", dst, strerror(errno));
        }
        afc_file_close(afc, handle);
    } else {
        fprintf(stderr, "Error: afc open file %s failed: %s\n", src, idev_afc_strerror(err));
        //this is a little non standard for a return value, trying to make things easier for cross platform
        //detection of whether or not the device is currently "locked"
        ret = err;
    }
    return ret;
}

off_t fsize(const char *filename) {
    struct stat st;
    
    if (stat(filename, &st) == 0)
        return st.st_size;
    
    return -1;
}

int put_afc_path(afc_client_t afc, const char *src, const char *dst) {
    int ret=EXIT_FAILURE;
    
    uint64_t handle=0;
    struct stat st;
    off_t fsize = 0;
    if (stat(src, &st) == 0) {
        fsize = st.st_size;
    }
    FILE *inf = fopen(src, "r");
    if (inf) {
        if (idev_verbose)
            fprintf(stderr, "[debug] Uploading %s to %s - creating afc file connection\n", src, dst);
        
        afc_error_t err = afc_file_open(afc, dst, AFC_FOPEN_WRONLY, &handle);
        
        if (err == AFC_E_SUCCESS) {
            char buf[CHUNKSZ];
            size_t bytes_read=0;
            size_t totbytes=0;
            
            while(err==AFC_E_SUCCESS && (bytes_read=fread(buf, 1, CHUNKSZ, inf)) > 0) {
                uint32_t bytes_written=0;
                err=afc_file_write(afc, handle, buf, (uint32_t)bytes_read, &bytes_written);
                totbytes += bytes_written;
                loadBar(totbytes, fsize, 50,basename((char*)src));
            }
            
            if (err) {
                fprintf(stderr, "Error: Encountered error while writing %s: %s\n", src, idev_afc_strerror(err));
                fprintf(stderr, "Warning! - %lu bytes read - incomplete data in %s may have resulted.\n", totbytes, dst);
            } else {
                printf("Uploaded %lu bytes to %s\n", totbytes, dst);
                ret=EXIT_SUCCESS;
            }
            
            afc_file_close(afc, handle);
        } else {
            fprintf(stderr, "Error: afc open file %s failed: %s\n", src, idev_afc_strerror(err));
        }
        fclose(inf);
    } else {
        fprintf(stderr, "Error opening local file for reading: %s - %s\n", dst, strerror(errno));
    }
    
    return ret;
}


#pragma mark - Command handlers

int do_info(afc_client_t afc, int argc, char **argv) {
    int i, ret = EXIT_SUCCESS;
    if (argc > 1) {
        for (i=1; i<argc ; i++) {
            ret |= dump_afc_file_info(afc, argv[i]);
        }
    } else {
        fprintf(stderr, "Error: you must specify at least one path.\n");
        ret = EXIT_FAILURE;
    }
    
    return ret;
}

int do_list(afc_client_t afc, int argc, char **argv) {
    int i, ret = EXIT_SUCCESS;
    if (argc > 1) {
        for (i=1; i<argc ; i++) {
            ret |= dump_afc_list_path(afc, argv[i]);
        }
    } else {
        ret = dump_afc_list_path(afc, "");
    }
    return ret;
}


int do_mkdir(afc_client_t afc, int argc, char **argv) {
    int i, ret=EXIT_SUCCESS;
    if (argc > 1) {
        for (i=1; i<argc ; i++) {
            afc_error_t err = afc_make_directory(afc, argv[i]);
            
            if (err == AFC_E_SUCCESS) {
                printf("Created directory: %s\n", argv[i]);
            } else {
                fprintf(stderr, "Error: mkdir error: %s\n", idev_afc_strerror(err));
                ret = EXIT_FAILURE;
            }
        }
    } else {
        fprintf(stderr, "Error: you must specify at least one directory path.\n");
        ret = EXIT_FAILURE;
    }
    
    return ret;
}

//specialized method to only remove a single file rather than trying to pass in arc/argv directly from command line
//yeh i suck at C, sorry!


int rm_file(afc_client_t afc, char *filePath) {
    int ret=EXIT_SUCCESS;
    afc_error_t err = afc_remove_path(afc, filePath);
    
    if (err == AFC_E_SUCCESS) {
        printf("Removed: %s\n", filePath);
    } else {
        fprintf(stderr, "Error: mkdir error: %s\n", idev_afc_strerror(err));
        ret = EXIT_FAILURE;
    }
    
    return ret;
}

int do_rm(afc_client_t afc, int argc, char **argv) {
    int i, ret=EXIT_SUCCESS;
    if (argc > 1) {
        for (i=1; i<argc ; i++) {
            afc_error_t err = afc_remove_path(afc, argv[i]);
            
            if (err == AFC_E_SUCCESS) {
                printf("Removed: %s\n", argv[i]);
            } else {
                fprintf(stderr, "Error: mkdir error: %s\n", idev_afc_strerror(err));
                ret = EXIT_FAILURE;
            }
        }
    } else {
        fprintf(stderr, "Error: you must specify at least one path to remove.\n");
        ret = EXIT_FAILURE;
    }
    
    return ret;
}

int do_rename(afc_client_t afc, int argc, char **argv) {
    int ret = EXIT_FAILURE;
    
    if (argc == 3) {
        afc_error_t err = afc_rename_path(afc, argv[1], argv[2]);
        
        if (err == AFC_E_SUCCESS) {
            printf("Renamed %s to %s\n", argv[1], argv[2]);
            ret = EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Error: rename %s to %s - %s\n", argv[1], argv[2], idev_afc_strerror(err));
        }
        
    } else {
        fprintf(stderr, "Error: invalid number of arguments for rename.\n");
    }
    
    return ret;
}

int do_link(afc_client_t afc, int argc, char **argv) {
    int ret=EXIT_FAILURE;
    
    if (argc == 3) {
        afc_error_t err = afc_make_link(afc, AFC_HARDLINK, argv[1], argv[2]);
        
        if (err == AFC_E_SUCCESS) {
            printf("Created hard-link %s -> %s\n", argv[2], argv[1]);
            ret = EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Error: link %s -> %s - %s\n", argv[2], argv[1], idev_afc_strerror(err));
        }
        
    } else {
        fprintf(stderr, "Error: invalid number of arguments for link command.\n");
    }
    
    return ret;
}

int do_symlink(afc_client_t afc, int argc, char **argv) {
    int ret=EXIT_FAILURE;
    
    if (argc == 3) {
        afc_error_t err = afc_make_link(afc, AFC_SYMLINK, argv[1], argv[2]);
        
        if (err == AFC_E_SUCCESS) {
            printf("Created symbolic-link %s -> %s\n", argv[2], argv[1]);
            ret = EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Error: link %s -> %s - %s\n", argv[2], argv[1], idev_afc_strerror(err));
        }
        
    } else {
        fprintf(stderr, "Error: invalid number of arguments for link command.\n");
    }
    
    return ret;
}

int do_cat(afc_client_t afc, int argc, char **argv) {
    int ret=EXIT_FAILURE;
    
    if (argc == 2) {
        ret = dump_afc_path(afc, argv[1], stdout);
    } else {
        fprintf(stderr, "Error: invalid number of arguments for cat command.\n");
    }
    
    return ret;
}

int do_get(afc_client_t afc, int argc, char **argv) {
    int ret=EXIT_FAILURE;
    
    if (argc == 2) {
        ret = get_afc_path(afc, argv[1], basename(argv[1]));
    } else if (argc == 3) {
        char *dst = argv[2];
        char dpath[PATH_MAX];
        if (is_dir(dst)) {
            snprintf(dpath, PATH_MAX-1, "%s/%s", dst, basename(argv[1]));
            dst = dpath;
        }
        ret = get_afc_path(afc, argv[1], dpath);
    } else {
        fprintf(stderr, "Error: invalid number of arguments for get command.\n");
    }
    
    return ret;
}

int do_put(afc_client_t afc, int argc, char **argv) {
    int ret=EXIT_FAILURE;
    
    if (argc == 2) {
        ret = put_afc_path(afc, argv[1], basename(argv[1]));
    } else if (argc == 3) {
        ret = put_afc_path(afc, argv[1], argv[2]);
    } else {
        fprintf(stderr, "Error: invalid number of arguments for put command.\n");
    }
    
    return ret;
}

int list_devices(FILE *outf) {
    int counts = 0;
    afc_idevice_info_t **devices = get_attached_devices(&counts);
    
    if (devices == NULL)
    {
        fprintf(stderr, "No devices attached!, bail!!!");
        return -1;
    }
    
    char *xmlData = devices_to_xml(devices, counts);
    fprintf(outf, "%s\n", xmlData);
    free(devices);
    return 0;
}


int recursive_document_list(afc_client_t afc, FILE *outf) {
    plist_t *documentList = afc_list_path(afc, "Documents", true);
    char *xmlData = NULL;
    uint32_t length = 0;
    plist_to_xml(documentList, &xmlData, &length);
    fprintf(outf, "%s\n", xmlData);
    return 0;
}

int cmd_main(afc_client_t afc, int argc, char **argv) {
    int ret=0;
    
    char *cmd = argv[0];
    
    if (!strcmp(cmd, "devinfo") || !strcmp(cmd, "deviceinfo")) {
        if (argc == 1) {
            ret = dump_afc_device_info(afc);
        } else {
            fprintf(stderr, "Error: unexpected extra arguments for devinfo\n");
            usage(stderr);
            ret=EXIT_FAILURE;
        }
    }
    else if (!strcmp(cmd, "info")) {
        ret = do_info(afc, argc, argv);
    }
    else if (!strcmp(cmd, "ls") || !strcmp(cmd, "list")) {
        ret = do_list(afc, argc, argv);
    }
    else if (!strcmp(cmd, "mkdir")) {
        ret = do_mkdir(afc, argc, argv);
    }
    else if (!strcmp(cmd, "rm") || !strcmp(cmd, "remove")) {
        ret = do_rm(afc, argc, argv);
    }
    else if (!strcmp(cmd, "rename")) {
        ret = do_rename(afc, argc, argv);
    }
    else if (!strcmp(cmd, "link") || !strcmp(cmd, "hardlink")) {
        ret = do_link(afc, argc, argv);
    }
    else if (!strcmp(cmd, "symlink")) {
        ret = do_symlink(afc, argc, argv);
    }
    else if (!strcmp(cmd, "cat")) {
        ret = do_cat(afc, argc, argv);
    }
    else if (!strcmp(cmd, "get")) {
        ret = do_get(afc, argc, argv);
    }
    else if (!strcmp(cmd, "put")) {
        ret = do_put(afc, argc, argv);
        
        //kevins additions
        
    } else if (!strcmp(cmd, "export")) {
        char *input = argv[1];
        char *output = argv[2];
        ret = export_shallow_folder(afc, input, output);
    }  else if (!strcmp(cmd, "clone")) {
        if (argc >=3){
            char *input = argv[1];
            char *output = argv[2];
            ret = clone_afc_path(afc, input, output);
        } else if (hasAppID == false) {
            ret = -1;
            printf("clone requires an appid to be set!\n");
        } else {
            ret = clone_afc_path(afc, "Documents", ".");
        }
    } else if (!strcmp(cmd, "documents"))
    {
        if (hasAppID == false)
        {
            ret = -1;
            printf("document listing requires an appid to be set!\n");
        } else {
            ret = recursive_document_list(afc, stdout);
        }
    }
    else {
        fprintf(stderr, "Error: unknown command: %s\n", cmd);
        usage(stderr);
        ret = EXIT_FAILURE;
    }
    
    return ret;
}

#define OPTION_FLAGS "rs:a:u:vhlcRAfxq"
void usage(FILE *outf) {
    fprintf(outf,
            "Usage: %s %s [%s] command cmdargs...\n\n"
            "  Options:\n"
            "    -r, --root                 Use the afc2 server if jailbroken (ignored with -a)\n"
            "    -s, --service=NAME>        Use the specified lockdown service (ignored with -a)\n"
            "    -a, --appid=<APP-ID>       Access bundle directory for app-id\n"
            "    -u, --uuid=<UDID>          Specify the device udid\n"
            "    -v, --verbose              Enable verbose debug messages\n"
            "    -h, --help                 Display this help message\n"
            "    -l, --list                 List devices\n"
            "    -A, --apps                 List installed Applications\n"
            "    -f, --filesharing          List Only Applications that have file sharing enabled (only applicable when listing applications)\n"
            "    -x, --xml                  Output file/application lists in XML format\n"
            "    -R, --recursive            List the specified folder recursively\n"
            "    -q, --quiet                Don't show the progress bar when applicable (putting/getting/cloning files)\n"
            "    -c, --clean                Cleans out folder after exporting/cloning\n\n"
            
            "  Where \"command\" and \"cmdargs...\" are as follows:\n\n"
            "  New commands:\n\n"
            "    clone  [localpath]         clone app Documents folder into a local folder. (requires appid)\n"
            "    clone  [path] [localpath]  clone directory folder into a local folder. (requires path and localpath)\n"
            "    export [path] [localpath]  export a specific directory to a local one (not recursive)\n"
            "    documents                  recursive plist formatted list of entire application Documents folder (requires appid)\n\n"
            "  Standard afcclient commands:\n\n"
            "    devinfo                    dump device info from AFC server\n"
            "    list <dir> [dir2...]       list remote directory contents\n"
            "    info <path> [path2...]     dump remote file information\n"
            "    mkdir <path> [path2...]    create directory at path\n"
            "    rm <path> [path2...]       remove directory at path\n"
            "    rename <from> <to>         rename path 'from' to path 'to'\n"
            "    link <target> <link>       create a hard-link from 'link' to 'target'\n"
            "    symlink <target> <link>    create a symbolic-link from 'link' to 'target'\n"
            "    cat <path>                 cat contents of <path> to stdout\n"
            "    get <path> [localpath]     download a file (default: current dir)\n"
            "    put <localpath> [path]     upload a file (default: remote top-level dir)\n\n"
            , progname, AFVersionNumber, OPTION_FLAGS);
}


static struct option longopts[] = {
    { "root",       no_argument,            NULL,   'r' },
    { "service",    required_argument,      NULL,   's' },
    { "appid",      required_argument,      NULL,   'a' },
    { "udid",       required_argument,      NULL,   'u' },
    { "verbose",    no_argument,            NULL,   'v' },
    { "help",       no_argument,            NULL,   'h' },
    { "list",       no_argument,            NULL,   'l' },
    { "clean",      no_argument,            NULL,   'c' },
    { "recursive",  no_argument,            NULL,   'R' },
    { "apps",       no_argument,            NULL,   'A' },
    { "xml",        no_argument,            NULL,   'x' },
    { "filesharing",no_argument,            NULL,   'f' },
    { "quiet",      no_argument,            NULL,   'q' },
    { NULL,         0,                      NULL,   0 }
};

int main(int argc, char **argv) {
    progname = basename(argv[0]);
    _relativeYear = currentRelativeYear();
    recursiveList = false;
    root = false;
    udid = NULL;
    appMode = false;
    quiet = false;
    char *appid=NULL, *svcname=NULL;;
    hasAppID = false;
    clean = false;
    xml = false;
    fs = false;
    svcname = AFC_SERVICE_NAME;
    int flag;
    while ((flag = getopt_long(argc, argv, OPTION_FLAGS, longopts, NULL)) != -1) {
        switch(flag) {
            case 'r':
                svcname = AFC2_SERVICE_NAME;
                root = true;
                break;
                
            case 's':
                svcname = optarg;
                break;
                
            case 'a':
                appid = optarg;
                hasAppID = true;
                break;
                
            case 'u':
                if ((strlen(optarg) != 40)) {
                    fprintf(stderr, "Error: invalid udid (wrong length): %s\n", optarg);
                    return EXIT_FAILURE;
                }
                
                udid = optarg;
                break;
                
            case 'v':
                idevice_set_debug_level(1);
                idev_verbose=true;
                break;
                
            case 'h':
                usage(stdout);
                return EXIT_SUCCESS;
                
            case 'l':
                list_devices(stdout);
                return EXIT_SUCCESS;
                
            case 'c':
                clean = true;
                break;
                
            case 'R':
                recursiveList = true;
                break;
                
            case 'x':
                xml = true;
                break;
                
            case 'f':
                fs = true;
                break;
                
            case 'A':
                appMode = true;
                break;
                
            case 'q':
                quiet = true;
                break;
                
            default:
                usage(stderr);
                return EXIT_FAILURE;
                
        }
    }
    
    argc -= optind;
    argv += optind;
    
    if (argc < 1 && appMode == false) {
        fprintf(stderr, "Missing command argument\n");
        usage(stderr);
        return EXIT_FAILURE;
    }
    
    if (appMode) {
        idevice_t phone = NULL;
        if (udid) {
            idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
            ret = idevice_new(&phone, NULL);
        }
        idev_list_installed_apps(phone, fs, xml);
        return 0;
    }
    
    if (appid) {
        return idev_afc_app_client(progname, udid, appid, ^int(afc_client_t afc) {
            return cmd_main(afc, argc, argv);
        });
        
    } else {
        
        //no appid
        return idev_afc_client_ex(progname, udid, svcname, ^int(idevice_t idev, lockdownd_client_t client, lockdownd_service_descriptor_t ldsvc, afc_client_t afc) {
            return cmd_main(afc, argc, argv);
        });
    }
}

