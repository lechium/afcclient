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
 */

#include "afcclient.h"

#ifdef __linux
  #include <limits.h>
#endif

#ifdef __APPLE__
  #include <sys/syslimits.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "libidev.h"


#define CHUNKSZ 8192

#pragma mark - AFC Implementation Utility Functions

char *progname;
bool hasAppID;
bool clean;

void usage(FILE *outf);

bool is_dir(char *path)
{
    struct stat s;
    return (stat(path, &s) == 0 && s.st_mode & S_IFDIR);
}

int dump_afc_device_info(afc_client_t afc)
{
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
        for(i=0; infolist[i]; i++)
        {
            arraySize++;
        }
        
        for(i=0; infolist[i]; i++)
        {
    
            if (i+1 < arraySize && (i % 2 == 0))
            {
                if (strcmp("st_birthtime", infolist[i])== 0 || strcmp("st_mtime", infolist[i]) == 0)
                {
                    //make the time values a little saner
                    long timeValue = atol(infolist[i+1])/1000000000;
                    char str[11];
                    sprintf(str,"%ld",timeValue);
                    
                  //  printf("timeValue: %s\n", str);
                     plist_dict_set_item(currentDevicePlist, infolist[i], plist_new_string(str));
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

plist_t * afc_list_path(afc_client_t afc, const char *path, int8_t recursive)
{
    int ret=EXIT_FAILURE;
    
    char **list=NULL;
    plist_t fileList = plist_new_array();
    if (idev_verbose)
        fprintf(stderr, "[debug] reading afc directory contents at \"%s\"\n", path);
    
    afc_error_t err = afc_read_directory(afc, path, &list);
    
    if (err == AFC_E_SUCCESS && list) {
        int i;
        
        
        printf("AFC Device Listing path=\"%s\":\n", path);
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
            
            if (strcmp(list[i], ".") != 0 && strcmp(list[i], "..") != 0  )
            {
                plist_t *fileInfo = afc_file_info_for_path(afc, lpath);
                
                //if is a directory recurse into it
                plist_t fmt_node = plist_dict_get_item(fileInfo, "st_ifmt");
                char *fmt = NULL;
                plist_get_string_val(fmt_node, &fmt);
                
                if ((strcmp(fmt, "S_IFDIR") == 0) && (recursive == true))
                {
                    plist_t infoCopy = plist_copy(fileInfo);
                    plist_array_append_item(fileList, infoCopy);
                    printf("%s folder, recurse!!\n", lpath);
                    plist_t *dirInfo = afc_list_path(afc, lpath, true);
                    //          printf("lpath: %s\n", lpath);
                    
                    
                    uint32_t arrayCount = plist_array_get_size(dirInfo);
                    
                    int j;
                    
                    for (j = 0; j < arrayCount; j++)
                    {
                        //  printf("j: %i\n", j);
                        plist_t arrayItem = NULL;
                        
                        arrayItem = plist_array_get_item(dirInfo, j);
                        
                        if (arrayItem != NULL)
                        {
                            //need to make a copy of the item for some reason... things just disappear otherwise
                            plist_t itemCopy = plist_copy(arrayItem);
                            plist_array_append_item(fileList, itemCopy);
                        }
                        
                        
                        
                    }
                    
                    
                } else if (strcmp(fmt, "S_IFREG") == 0){ //not a directory, just a file
                    
                    printf("%s file, treat normally!!\n", lpath);
                    
                    
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



int dump_afc_file_info(afc_client_t afc, const char *path)
{
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



int dump_afc_list_path(afc_client_t afc, const char *path)
{
    int ret=EXIT_FAILURE;

    char **list=NULL;

    if (idev_verbose)
        fprintf(stderr, "[debug] reading afc directory contents at \"%s\"\n", path);

    afc_error_t err = afc_read_directory(afc, path, &list);

    if (err == AFC_E_SUCCESS && list) {
        int i;
        printf("AFC Device Listing path=\"%s\":\n", path);
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


int dump_afc_path(afc_client_t afc, const char *path, FILE *outf)
{
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



//obsolete, was being used when i was trying to convert paths.

/*

char *str_replace(char *orig, char *rep, char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep
    int len_with; // length of with
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements
    
    if (!orig)
        return NULL;
    if (!rep)
        rep = "";
    len_rep = strlen(rep);
    if (!with)
        with = "";
    len_with = strlen(with);
    
    ins = orig;
    for (count = 0; tmp = strstr(ins, rep); ++count) {
        ins = tmp + len_rep;
    }
    
    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);
    
    if (!result)
        return NULL;
    
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

*/

/*
    i think text files writing in binary will not be okay, 
    but windows fopen defaults to text, so files get corrupt
    so this gets called every time we use fopen, make 
    sure our plist files can get read back and forth without
    trouble.
 
    right now only focusing on plist, doubt it should matter with anything else.
 
*/

char * write_mode_for_file(char *filename)
{
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
 
 it SHOULD work cross platform (once i comment the win32 stuff back in)
 
 TODO: check for permissions on file folder creation on the selected directory.
 
 */

int clone_afc_path(afc_client_t afc, const char *src, const char *dst)
{
    int ret=EXIT_FAILURE;
    
    if (idev_verbose)
        fprintf(stderr, "[debug] Cloning %s to %s - creating afc file connection\n", src, dst);
    
    plist_t fileList = afc_list_path(afc, src, true);
    
    int fileCount = plist_array_get_size(fileList);
    
    if (idev_verbose)
        printf("fileCount: %i\n", fileCount);
    
    int i;
    
    //make documents folders
    
    char documentsPath[PATH_MAX];
    sprintf(documentsPath,"%s/Documents",dst);

    
    //windows mkdir is different!
    
#if defined(_WIN32)

    _mkdir(dst);
    _mkdir(documentsPath);

#else
    //sprintf(newPath,"%s/%s/",dst, path);
    mkdir(dst, 0777); // notice that 777 is different than 0777
    mkdir(documentsPath, 0777); // notice that 777 is different than 0777

#endif
    
    for (i = 0; i < fileCount; i++)
    {
        char *path = NULL;
        plist_t item = plist_array_get_item(fileList, i);
        plist_t path_node = plist_dict_get_item(item, "path");
        plist_t fmt_node = plist_dict_get_item(item, "st_ifmt");
        char *fmt = NULL;
        plist_get_string_val(fmt_node, &fmt);
        plist_get_string_val(path_node, &path);
        char newPath[PATH_MAX];

        
        if (strcmp(fmt, "S_IFDIR") == 0)
        {
            sprintf(newPath,"%s/%s/",dst, path);
            
#if defined(_WIN32)
            _mkdir(newPath);

#else
            mkdir(newPath, 0777); // notice that 777 is different than 0777
#endif
            printf("mkdir at new path: %s\n", newPath);
        } else {
            
             sprintf(newPath,"%s/%s",dst, path);
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
                if (ret == EXIT_SUCCESS)
                {
                    if (clean == true)
                    {
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

int export_shallow_folder(afc_client_t afc, const char *src, const char *dst)
{
    int ret=EXIT_FAILURE;
    
    if (idev_verbose)
        fprintf(stderr, "[debug] exporting %s to %s - creating afc file connection\n", src, dst);
    
    plist_t fileList = afc_list_path(afc, src, false);
    
    int fileCount = plist_array_get_size(fileList);
    
    if (idev_verbose)
        printf("fileCount: %i\n", fileCount);
    
    int i;
    
    for (i = 0; i < fileCount; i++)
    {
        char *path = NULL;
        plist_t item = plist_array_get_item(fileList, i);
        plist_t path_node = plist_dict_get_item(item, "path");
        plist_t fmt_node = plist_dict_get_item(item, "st_ifmt");
        char *fmt = NULL;
        plist_get_string_val(fmt_node, &fmt);
        plist_get_string_val(path_node, &path);
        char newPath[PATH_MAX];
        
        if (strcmp(fmt, "S_IFDIR") != 0)
        {
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
                if (ret == EXIT_SUCCESS)
                {
                    if (clean == true)
                    {
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

int get_afc_path(afc_client_t afc, const char *src, const char *dst)
{
    int ret=EXIT_FAILURE;

    if (idev_verbose)
        fprintf(stderr, "[debug] Downloading %s to %s - creating afc file connection\n", src, dst);

    uint64_t handle=0;
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

        
                    
int put_afc_path(afc_client_t afc, const char *src, const char *dst)
{
    int ret=EXIT_FAILURE;

    uint64_t handle=0;

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

int do_info(afc_client_t afc, int argc, char **argv)
{
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

int do_list(afc_client_t afc, int argc, char **argv)
{
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


int do_mkdir(afc_client_t afc, int argc, char **argv)
{
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


int rm_file(afc_client_t afc, char *filePath)
{
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

int do_rm(afc_client_t afc, int argc, char **argv)
{
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

int do_rename(afc_client_t afc, int argc, char **argv)
{
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

int do_link(afc_client_t afc, int argc, char **argv)
{
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

int do_symlink(afc_client_t afc, int argc, char **argv)
{
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

int do_cat(afc_client_t afc, int argc, char **argv)
{
    int ret=EXIT_FAILURE;

    if (argc == 2) {
        ret = dump_afc_path(afc, argv[1], stdout);
    } else {
        fprintf(stderr, "Error: invalid number of arguments for cat command.\n");
    }

    return ret;
}

int do_get(afc_client_t afc, int argc, char **argv)
{
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

int do_put(afc_client_t afc, int argc, char **argv)
{
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

int list_devices(FILE *outf)
{
    int counts = 0;
    idevice_info_t **devices = get_attached_devices(&counts);
    
    if (devices == NULL)
    {
        fprintf(stderr, "No devices attached!, bail!!!");
        return -1;
    }
    
    char *xmlData = devices_to_xml(devices, counts);
    fprintf(outf, "print_device_xml: %s\n", xmlData);
    free(devices);
    return 0;
}



int recursive_document_list(afc_client_t afc, FILE *outf)
{
    plist_t *documentList = afc_list_path(afc, "Documents", true);
    char *xmlData = NULL;
    uint32_t length = 0;
    plist_to_xml(documentList, &xmlData, &length);
    fprintf(outf, "%s\n", xmlData);
    return 0;
}

int cmd_main(afc_client_t afc, int argc, char **argv)
{
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
            if (hasAppID == false)
            {
                ret = -1;
                printf("export requires an appid to be set!\n");
                
            } else {
                char *input = argv[1];
                char *output = argv[2];
                ret = export_shallow_folder(afc, input, output);
            }
            
        }  else if (!strcmp(cmd, "clone")) {
            if (hasAppID == false)
            {
                ret = -1;
                printf("clone requires an appid to be set!\n");
            } else {
                
                char *output = argv[1];
                ret = clone_afc_path(afc, "Documents", output);
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

#define OPTION_FLAGS "rs:a:u:vhlc"
void usage(FILE *outf)
{
    fprintf(outf,
        "Usage: %s [%s] command cmdargs...\n\n"
        "  Options:\n"
        "    -r, --root                 Use the afc2 server if jailbroken (ignored with -a)\n"
        "    -s, --service=NAME>        Use the specified lockdown service (ignored with -a)\n"
        "    -a, --appid=<APP-ID>       Access bundle directory for app-id\n"
        "    -u, --uuid=<UDID>          Specify the device udid\n"
        "    -v, --verbose              Enable verbose debug messages\n"
        "    -h, --help                 Display this help message\n"
        "    -l, --list                 List devices\n"
        "    -c, --clean                Cleans out folder after exporting/cloning\n\n"
     
        "  Where \"command\" and \"cmdargs...\" are as follows:\n\n"
        "  New commands:\n\n"
        "    clone  [localpath]         clone Documents folder into a local folder. (requires appid)\n"
        "    export [path] [localpath]  export a specific directory to a local one (not recursive)\n"
        "    documents                  recursive plist formatted list of entire ~/Documents folder (requires appid)\n\n"
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
        , progname, OPTION_FLAGS);
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
    { NULL,         0,                      NULL,   0 }
};

int main(int argc, char **argv)
{
    progname = basename(argv[0]);

    char *appid=NULL, *udid=NULL, *svcname=NULL;;
    hasAppID = false;
    clean = false;
    svcname = AFC_SERVICE_NAME;
    int flag;
    while ((flag = getopt_long(argc, argv, OPTION_FLAGS, longopts, NULL)) != -1) {
        switch(flag) {
            case 'r':
                svcname = AFC2_SERVICE_NAME;
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
                
            default:
                usage(stderr);
                return EXIT_FAILURE;

        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        fprintf(stderr, "Missing command argument\n");
        usage(stderr);
        return EXIT_FAILURE;
    }

    if (appid) {
       // return idev_afc_app_client(progname, udid, appid, ^int(afc_client_t afc) {
         //   return cmd_main(afc, argc, argv);
        //});
        int error;
        afc_client_t afc = idev_afc_app_client(progname, udid, appid, &error);
        
       if (afc == NULL)
       {
           return EXIT_FAILURE;
       }
        return cmd_main(afc, argc, argv);
        
        
    } else {
       
        //no appid
        int error;
        
        afc_client_t afc = idev_afc_client(progname, udid, svcname, &error);
        
        //afc_client_t afc = idev_afc_app_client(progname, udid, appid, &error);
        
        if (afc == NULL)
        {
            return EXIT_FAILURE;
        }
        return cmd_main(afc, argc, argv);
        
        //return idev_afc_client_ex(progname, udid, svcname, ^int(idevice_t idev, lockdownd_client_t client, l//ockdownd_service_descriptor_t ldsvc, afc_client_t afc) {
            //return cmd_main(afc, argc, argv);
      //  });
    }
}

