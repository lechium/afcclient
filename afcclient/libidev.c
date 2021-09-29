/*
 * libidev
 * Date: Nov 2013
 * Eric Monti - esmonti at gmail dot com
 *
 * just a set of helper functions for and wrappers around the
 * libimobiledevice library -- often making gratuitous and
 * obscene use of clang blocks.
 *
 * updated in 2014 by Kevin Bradley to remove gratuitous blocks and add
 * some code written in an unreleased project in 2010.
 *
 * The rationale behind removing clang blocks is easier cross-platform windows support
 * spent days trying to get clang to work with mingw with 0 success, easier to refactor
 * without blocks rather than building clang from scratch.
 *
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include "libimobiledevice/libimobiledevice.h"
#include "libimobiledevice/lockdown.h"

#ifdef __APPLE__
#include <IOKit/IOTypes.h>
#include <IOKit/IOCFBundle.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#endif

#ifndef WIN32
#include "libusb-1.0/libusb.h"
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#endif
#define BUFFER_SIZE 0x1000

bool idev_verbose=false;

#include "libidev.h"

#include "plist/plist.h"

//transplants from libdioxin

/**
 
 takes pointer array of afc_idevice_info_t structs and converts them into a plist array of dictionary plists
 
 */

char * devices_to_xml(afc_idevice_info_t **devices, int itemCount) {
    
    char *xmlData = NULL;
    uint32_t length = 0;
    int i;
    plist_t deviceList = plist_new_array();
    
    for (i = 0; i < itemCount; i++) {
        afc_idevice_info_t *currentDevice = devices[i];
        plist_t currentDevicePlist = plist_new_dict();
        
        plist_dict_set_item(currentDevicePlist, "ProductType", plist_new_string(currentDevice->productType));
        plist_dict_set_item(currentDevicePlist, "ProductVersion", plist_new_string(currentDevice->productVersion));
        plist_dict_set_item(currentDevicePlist, "BuildVersion", plist_new_string(currentDevice->buildVersion));
        plist_dict_set_item(currentDevicePlist, "DeviceName", plist_new_string(currentDevice->deviceName));
        plist_dict_set_item(currentDevicePlist, "DeviceClass", plist_new_string(currentDevice->deviceClass));
        plist_dict_set_item(currentDevicePlist, "HardwareModel", plist_new_string(currentDevice->hardwareModel));
        plist_dict_set_item(currentDevicePlist, "HardwarePlatform", plist_new_string(currentDevice->hardwarePlatform));
        plist_dict_set_item(currentDevicePlist, "UniqueDeviceID", plist_new_string(currentDevice->uniqueDeviceID));
        plist_dict_set_item(currentDevicePlist, "UniqueChipID", plist_new_uint(currentDevice->uniqueChipID));
        plist_dict_set_item(currentDevicePlist, "PasswordProtected", plist_new_bool(currentDevice->passwordProtected));
        //if (currentDevice->URL != NULL)
        //	plist_dict_insert_item(currentDevicePlist, "URL", plist_new_string(currentDevice->URL));
        
        free(currentDevice);
        plist_array_append_item(deviceList, currentDevicePlist);
        //free(currentDevicePlist);
    }
    plist_to_xml(deviceList, &xmlData, &length);
    return xmlData;
}

/**
 
 creates an afc_idevice_info_t struct for a particular device based on its uuid
 
 */

afc_idevice_info_t * device_get_info(char *uuids) {
    
    lockdownd_client_t client = NULL;
    idevice_t phone = NULL;
    idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
    int simple = 0;
    char *domain = NULL;
    char *key = NULL;
    plist_t node = NULL;
    
    if (uuids != 0) {
        ret = idevice_new_with_options(&phone, uuids, IDEVICE_LOOKUP_USBMUX);
        if (ret != IDEVICE_E_SUCCESS) {
            printf("No device found with uuid %s, is it plugged in?\n", uuids);
            return NULL;
        }
    }
    else {
        ret = idevice_new(&phone, NULL);
        if (ret != IDEVICE_E_SUCCESS) {
            printf("No device found, is it plugged in?\n");
            return NULL;
        }
    }
    if (LOCKDOWN_E_SUCCESS != (simple ?
                               lockdownd_client_new(phone, &client, "libdioxin"):
                               lockdownd_client_new_with_handshake(phone, &client, "libdioxin"))) {
        idevice_free(phone);
        return NULL;
    }
    idevice_set_debug_level(5);
    afc_idevice_info_t *device_info = (afc_idevice_info_t*)malloc(sizeof(afc_idevice_info_t));
    /* run query and output information */
    if(lockdownd_get_value(client, domain, key, &node) == LOCKDOWN_E_SUCCESS) {
        if (node) {
            /*
             char *xmlData = NULL;
             uint32_t length = 0;
             plist_to_xml(node, &xmlData, &length);
             printf("plist: %s\n", xmlData);
             */
            //this is where my lack of C skills peeks out, some of these could probably be re-used rather than
            //all new variables.
            
            char *bv = NULL;
            char *pt = NULL;
            char *pv = NULL;
            char *dn = NULL;
            char *hm = NULL;
            char *hp = NULL;
            char *ud = NULL;
            char *dc = NULL;
            uint64_t uc;
            uint8_t pwProtected = false;
            plist_t pt_node = plist_dict_get_item(node, "ProductType");
            if (pt_node != NULL) {
                plist_get_string_val(pt_node, &pt);
            }
            if (pt == NULL) {
                pt = "not available";
            }
            plist_get_bool_val(plist_dict_get_item(node, "PasswordProtected"), &pwProtected);
            plist_get_string_val(plist_dict_get_item(node, "BuildVersion"), &bv);
            plist_get_string_val(plist_dict_get_item(node, "ProductVersion"), &pv);
            plist_get_string_val(plist_dict_get_item(node, "DeviceName"), &dn);
            plist_get_string_val(plist_dict_get_item(node, "HardwareModel"), &hm);
            plist_get_string_val(plist_dict_get_item(node, "DeviceClass"), &dc);
            plist_t hp_node = plist_dict_get_item(node, "HardwarePlatform");
            if (hp_node != NULL) {
                plist_get_string_val(hp_node, &hp);
            }
            if (hp == NULL) {
                hp = "not available";
            }
            plist_get_string_val(plist_dict_get_item(node, "UniqueDeviceID"), &ud);
            plist_get_uint_val(plist_dict_get_item(node, "UniqueChipID"), &uc);
            
            device_info->buildVersion = bv;
            device_info->productType = pt;
            device_info->productVersion = pv;
            device_info->deviceName	= dn;
            device_info->hardwareModel = hm;
            device_info->hardwarePlatform = hp;
            device_info->uniqueDeviceID = ud;
            device_info->uniqueChipID = uc;
            device_info->passwordProtected = pwProtected;
            device_info->deviceClass = dc;
            
            plist_free(pt_node);
            plist_free(hp_node);
            
            if (domain != NULL) {
                free(domain);
            }
            lockdownd_client_free(client);
            idevice_free(phone);
            return device_info;
        }
    }
    
    if (domain != NULL)
        free(domain);
    lockdownd_client_free(client);
    idevice_free(phone);
    return 0;
}

/**
 
 return afc_idevice_info_t struct of the first device of a particular type
 ie: iPod5,1 or iPhone6,1
 
 */

afc_idevice_info_t * first_device_of_type(char *deviceType) {
    char **dev_list = NULL;
    afc_idevice_info_t *compatDevice = NULL;
    int i;
    if (idevice_get_device_list(&dev_list, &i) < 0) {
        
        fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
        return compatDevice;
    }
    for (i = 0; dev_list[i] != NULL; i++) {
        
        afc_idevice_info_t *currentDevice = device_get_info(dev_list[i]);
        char *productType = currentDevice->productType;
        if (strcmp(productType, deviceType) == 0) {
            compatDevice = currentDevice;
        }
    }
    idevice_device_list_free(dev_list);
    return compatDevice;
}

//deprecated?

char * get_deviceid_from_type(char *deviceType) {
    
    char **dev_list = NULL;
    char *deviceUDID = NULL;
    int i;
    if (idevice_get_device_list(&dev_list, &i) < 0) {
        fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
        return NULL;
    }
    for (i = 0; dev_list[i] != NULL; i++) {
        
        afc_idevice_info_t *currentDevice = device_get_info(dev_list[i]);
        char *productType = currentDevice->productType;
        if (strcmp(productType, deviceType) == 0)
        {
            deviceUDID = currentDevice->uniqueDeviceID;
            
        }
    }
    idevice_device_list_free(dev_list);
    
    return deviceUDID;
}

/**
 
 get afc_idevice_info_t pointer array of all the attached iOS devices
 
 */

afc_idevice_info_t ** get_attached_devices(int *deviceCount) {
    
    char **dev_list = NULL;
    
    int i;
    if (idevice_get_device_list(&dev_list, &i) < 0) {
        fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
        return NULL;
    }
    afc_idevice_info_t** list = (afc_idevice_info_t**)malloc((i+1)*sizeof(afc_idevice_info_t*));
    for (i = 0; dev_list[i] != NULL; i++) {
        list[i] = device_get_info(dev_list[i]);
    }
    idevice_device_list_free(dev_list);
    *deviceCount = i;
    return list;
}

/**
 
 get raw char array formatted in plist XML format
 
 */

char * get_attached_devices_xml(int *deviceCount) {
    
    int counts = 0;
    afc_idevice_info_t **devices = get_attached_devices(&counts);
    
    if (devices == NULL)
    {
        fprintf(stderr, "No devices attached!, bail!!!");
        return NULL;
    }
    *deviceCount = counts;
    char *xmlData = devices_to_xml(devices, counts);
    //fprintf(stderr, "print_device_xml: %s\n", xmlData);
    free(devices);
    return xmlData;
}


/**
 
 based more around CLI implementations to just print out raw data
 
 */


int print_device_xml() {
    int counts = 0;
    afc_idevice_info_t **devices = get_attached_devices(&counts);
    char *xmlData = devices_to_xml(devices, counts);
    fprintf(stderr, "print_device_xml: %s\n", xmlData);
    free(xmlData);
    free(devices);
    return 0;
}


int print_device_info() {
    
    int i = 0;
    int counts = 0;
    afc_idevice_info_t **devices = get_attached_devices(&counts);
    for (i = 0; i < counts; i++) {
        afc_idevice_info_t *deviceInfo = devices[i];
        printf("device %i of %i uuid: %s\n\n", i+1, counts, deviceInfo->uniqueDeviceID);
        printf("%s hardware model: %s hardware platform: %s\n", deviceInfo->deviceName, deviceInfo->hardwareModel, deviceInfo->hardwarePlatform);
        printf("%s running %s (%s)\n\n", deviceInfo->productType, deviceInfo->productVersion, deviceInfo->buildVersion);
        free(deviceInfo);
    }
    
    free(devices);
    return 0;
}

//end of methods shoehorned in from libdioxin

const char *idev_idevice_strerror(idevice_error_t errnum) {
    switch(errnum) {
        case IDEVICE_E_SUCCESS:
            return "SUCCESS";
        case IDEVICE_E_INVALID_ARG:
            return "INVALID_ARG";
        case IDEVICE_E_UNKNOWN_ERROR:
            return "UNKNOWN_ERROR";
        case IDEVICE_E_NO_DEVICE:
            return "NO_DEVICE";
        case IDEVICE_E_NOT_ENOUGH_DATA:
            return "NOT_ENOUGH_DATA";
        case IDEVICE_E_SSL_ERROR:
            return "BAD_SSL";
        default:
            return "UNKNOWN_EROR";
    }
}

const char *idev_lockdownd_strerror(lockdownd_error_t errnum) {
    switch(errnum) {
        case LOCKDOWN_E_SUCCESS:
            return "SUCCESS";
        case LOCKDOWN_E_INVALID_ARG:
            return "INVALID_ARG";
        case LOCKDOWN_E_INVALID_CONF:
            return "INVALID_CONF";
        case LOCKDOWN_E_PLIST_ERROR:
            return "PLIST_ERROR";
        case LOCKDOWN_E_PAIRING_FAILED:
            return "PAIRING_FAILED";
        case LOCKDOWN_E_SSL_ERROR:
            return "SSL_ERROR";
        case LOCKDOWN_E_DICT_ERROR:
            return "DICT_ERROR";
        case LOCKDOWN_E_RECEIVE_TIMEOUT:
            return "LOCKDOWN_E_RECEIVE_TIMEOUT";
        case LOCKDOWN_E_SET_PROHIBITED:
            return "SET_VALUE_PROHIBITED";
        case LOCKDOWN_E_GET_PROHIBITED:
            return "GET_VALUE_PROHIBITED";
        case LOCKDOWN_E_REMOVE_PROHIBITED:
            return "REMOVE_VALUE_PROHIBITED";
        case LOCKDOWN_E_MUX_ERROR:
            return "MUX_ERROR";
        case LOCKDOWN_E_PASSWORD_PROTECTED:
            return "PASSWORD_PROTECTED";
        case LOCKDOWN_E_NO_RUNNING_SESSION:
            return "NO_RUNNING_SESSION";
        case LOCKDOWN_E_INVALID_HOST_ID:
            return "INVALID_HOST_ID";
        case LOCKDOWN_E_INVALID_SERVICE:
            return "INVALID_SERVICE";
        case LOCKDOWN_E_INVALID_ACTIVATION_RECORD:
            return "INVALID_ACTIVATION_RECORD";
        case LOCKDOWN_E_UNKNOWN_ERROR:
        default:
            return "UNKNOWN_EROR";
    }
}

const char *idev_file_relay_strerror(file_relay_error_t errnum) {
    switch(errnum) {
        case FILE_RELAY_E_SUCCESS:
            return "SUCCESS";
        case FILE_RELAY_E_INVALID_ARG:
            return "INVALID_ARG";
        case FILE_RELAY_E_PLIST_ERROR:
            return "PLIST_ERROR";
        case FILE_RELAY_E_MUX_ERROR:
            return "MUX_ERROR";
        case FILE_RELAY_E_INVALID_SOURCE:
            return "INVALID_SOURCE";
        case FILE_RELAY_E_STAGING_EMPTY:
            return "STAGING_EMPTY";
        case FILE_RELAY_E_UNKNOWN_ERROR:
        default:
            return "UNKNOWN_EROR";
    }
}

const char *idev_instproxy_strerror(instproxy_error_t errnum) {
    switch(errnum) {
        case INSTPROXY_E_SUCCESS:
            return "SUCCESS";
        case INSTPROXY_E_INVALID_ARG:
            return "INVALID_ARG";
        case INSTPROXY_E_PLIST_ERROR:
            return "PLIST_ERROR";
        case INSTPROXY_E_CONN_FAILED:
            return "CONN_FAILED";
        case INSTPROXY_E_OP_IN_PROGRESS:
            return "OP_IN_PROGRESS";
        case INSTPROXY_E_OP_FAILED:
            return "OP_FAILED";
        case INSTPROXY_E_UNKNOWN_ERROR:
        default:
            return "UNKNOWN_EROR";
    }
}

const char *idev_afc_strerror(afc_error_t errnum) {
    switch(errnum) {
        case AFC_E_SUCCESS:
            return "SUCCESS";
        case AFC_E_UNKNOWN_ERROR:
            return "UNKNOWN_ERROR";
        case AFC_E_OP_HEADER_INVALID:
            return "OP_HEADER_INVALID";
        case AFC_E_NO_RESOURCES:
            return "NO_RESOURCES";
        case AFC_E_READ_ERROR:
            return "READ_ERROR";
        case AFC_E_WRITE_ERROR:
            return "WRITE_ERROR";
        case AFC_E_UNKNOWN_PACKET_TYPE:
            return "UNKNOWN_PACKET_TYPE";
        case AFC_E_INVALID_ARG:
            return "INVALID_ARG";
        case AFC_E_OBJECT_NOT_FOUND:
            return "OBJECT_NOT_FOUND";
        case AFC_E_OBJECT_IS_DIR:
            return "OBJECT_IS_DIR";
        case AFC_E_PERM_DENIED:
            return "PERM_DENIED";
        case AFC_E_SERVICE_NOT_CONNECTED:
            return "SERVICE_NOT_CONNECTED";
        case AFC_E_OP_TIMEOUT:
            return "OP_TIMEOUT";
        case AFC_E_TOO_MUCH_DATA:
            return "TOO_MUCH_DATA";
        case AFC_E_END_OF_DATA:
            return "END_OF_DATA";
        case AFC_E_OP_NOT_SUPPORTED:
            return "OP_NOT_SUPPORTED";
        case AFC_E_OBJECT_EXISTS:
            return "OBJECT_EXISTS";
        case AFC_E_OBJECT_BUSY:
            return "OBJECT_BUSY";
        case AFC_E_NO_SPACE_LEFT:
            return "NO_SPACE_LEFT";
        case AFC_E_OP_WOULD_BLOCK:
            return "OP_WOULD_BLOCK";
        case AFC_E_IO_ERROR:
            return "IO_ERROR";
        case AFC_E_OP_INTERRUPTED:
            return "OP_INTERRUPTED";
        case AFC_E_OP_IN_PROGRESS:
            return "OP_IN_PROGRESS";
        case AFC_E_INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        case AFC_E_MUX_ERROR:
            return "MUX_ERROR";
        case AFC_E_NO_MEM:
            return "NO_MEM";
        case AFC_E_NOT_ENOUGH_DATA:
            return "NOT_ENOUGH_DATA";
        case AFC_E_DIR_NOT_EMPTY:
            return "DIR_NOT_EMPTY";
        default:
            return "UNKNOWN_EROR";
    }
}

const char *idev_house_arrest_strerror(house_arrest_error_t errnum) {
    switch(errnum) {
        case HOUSE_ARREST_E_SUCCESS:
            return "SUCCESS";
        case HOUSE_ARREST_E_INVALID_ARG:
            return "INVALID_ARG";
        case HOUSE_ARREST_E_PLIST_ERROR:
            return "PLIST_ERROR";
        case HOUSE_ARREST_E_CONN_FAILED:
            return "CONN_FAILED";
        case HOUSE_ARREST_E_INVALID_MODE:
            return "INVALID_MODE";
        case HOUSE_ARREST_E_UNKNOWN_ERROR:
        default:
            return "UNKNOWN_EROR";
    }
}


/* 
 
 rather than whole-sale remove the old methods that used blocks, i figured i would comment them out,
 in case someone with more windows experience than myself found a way to make it easier to get
 clang working with mingw.
 
 */

/*
 
 
 // note: clientname may be null in which case a default value of "idevtool" is used
 int idev_lockdownd_client(char *clientname, char *udid, int(^callback)(idevice_t idev, lockdownd_client_t client)) {
 int ret=EXIT_FAILURE;
 idevice_t idev = NULL;
 
 if (!clientname)
 clientname = "idevtool";
 
 idevice_error_t ierr=idevice_new(&idev, udid);
 
 if (ierr == IDEVICE_E_SUCCESS && idev) {
 lockdownd_client_t client = NULL;
 lockdownd_error_t ldret = lockdownd_client_new_with_handshake(idev, &client, clientname);
 
 if (ldret == LOCKDOWN_E_SUCCESS && client) {
 ret = callback(idev, client);
 } else {
 fprintf(stderr, "Error: Can't connect to lockdownd: %s.\n", idev_lockdownd_strerror(ldret));
 }
 
 if (client)
 lockdownd_client_free(client);
 
 } else if (ierr == IDEVICE_E_NO_DEVICE) {
 fprintf(stderr, "Error: No device found -- Is it plugged in?\n");
 } else {
 fprintf(stderr, "Error: Cannot connect to device: %s\n", idev_idevice_strerror(ierr));
 }
 
 if (idev)
 idevice_free(idev);
 
 return ret;
 }
 */
/*
 
 //more commenting out stuff that uses blocks
 
 // starts a remote lockdownd service instance and returns an initialized lockdownd_service_descriptor_t
 int idev_lockdownd_start_service (
 char *progname,
 char *udid,
 char *servicename,
 int(^block)(idevice_t idev, lockdownd_client_t client, lockdownd_service_descriptor_t ldsvc) )
 {
 return idev_lockdownd_client(progname, udid,
 ^int(idevice_t idev, lockdownd_client_t client)
 {
 int ret=EXIT_FAILURE;
 
 if (idev_verbose) fprintf(stderr, "[debug] starting '%s' lockdownd service\n", servicename);
 
 lockdownd_service_descriptor_t ldsvc = NULL;
 lockdownd_error_t ldret = lockdownd_start_service(client, servicename, &ldsvc);
 
 if ((ldret == LOCKDOWN_E_SUCCESS) && ldsvc) {
 
 block(idev, client, ldsvc);
 
 } else {
 fprintf(stderr, "Error: could not start service: %s", servicename);
 }
 
 if (ldsvc) lockdownd_service_descriptor_free(ldsvc);
 
 return ret;
 
 });
 
 }
 */

//trying to re-factor without blocks, it appears this is never even used.

/*
 
 // connects to a lockdownd service by name and returns an initialized idevice_connection_t
 int idev_lockdownd_connect_service (
 char *progname,
 char *udid,
 char *servicename,
 int(^block)(idevice_t idev, lockdownd_service_descriptor_t ldsvc, idevice_connection_t con) )
 {
 return idev_lockdownd_start_service(progname, udid, servicename,
 ^int(idevice_t idev, lockdownd_client_t client, lockdownd_service_descriptor_t ldsvc)
 {
 int ret=EXIT_FAILURE;
 
 if (idev_verbose) fprintf(stderr, "[debug] connecting to service: %s\n", servicename);
 
 idevice_connection_t con=NULL;
 idevice_error_t ierr = idevice_connect(idev, ldsvc->port, &con);
 if (ierr == IDEVICE_E_SUCCESS && con) {
 if (idev_verbose) fprintf(stderr, "[debug] successfully connected to %s\n", servicename);
 
 ret = block(idev, ldsvc, con);
 
 } else {
 fprintf(stderr, "Error: could not connect to lockdownd service");
 }
 
 if (con) idevice_disconnect(con);
 
 return ret;
 });
 }
 
 */

void idev_list_installed_apps(idevice_t idevice, bool filterSharing, bool xml) {
    int simple = 0;
    plist_t appList = plist_new_array();
    if (idevice == NULL) {
        idevice_t phone = NULL;
        idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
        ret = idevice_new(&phone, NULL);
        if (ret != IDEVICE_E_SUCCESS) {
            printf("No device found, is it plugged in?\n");
            return;
        }
        idevice = phone;
    }
    lockdownd_client_t lockd = NULL;
    if (LOCKDOWN_E_SUCCESS != (simple ?
                               lockdownd_client_new(idevice, &lockd, "libdioxin"):
                               lockdownd_client_new_with_handshake(idevice, &lockd, "libdioxin"))) {
        idevice_free(idevice);
        return;
    }
    lockdownd_service_descriptor_t ldsvc = NULL;
    if ((lockdownd_start_service(lockd, "com.apple.mobile.installation_proxy", &ldsvc) == LOCKDOWN_E_SUCCESS) && ldsvc) {
        
        instproxy_client_t ipc = NULL;
        if (instproxy_client_new(idevice, ldsvc, &ipc) == INSTPROXY_E_SUCCESS) {
            
            plist_t client_opts = instproxy_client_options_new();
            /*          "ApplicationType" -> "System"
             *          "ApplicationType" -> "User"
             *          "ApplicationType" -> "Internal"
             *          "ApplicationType" -> "Any"
             */
            plist_dict_set_item(client_opts, "ApplicationType", plist_new_string("Any"));
            plist_t apps = NULL;
            instproxy_error_t err = instproxy_browse(ipc, client_opts, &apps);
            if (err == INSTPROXY_E_SUCCESS) {
                uint32_t i;
                char *xmlData = NULL;
                uint32_t length = 0;
                for (i = 0; i < plist_array_get_size(apps); i++) {
                    char *appid_str = NULL, *name_str = NULL, *appType = NULL;
                    uint8_t sharing = false;
                    
                    
                    
                    plist_t app_info = plist_array_get_item(apps, i);
                    plist_get_string_val(plist_dict_get_item(app_info, "ApplicationType"), &appType);
                    plist_get_bool_val(plist_dict_get_item(app_info, "UIFileSharingEnabled"), &sharing);
                    bool systemApp = false;
                    if (strcmp(appType, "System") == 0) {
                        systemApp = true;
                    }
                    plist_t appid_p = plist_dict_get_item(app_info, "CFBundleIdentifier");
                    if (appid_p)
                        plist_get_string_val(appid_p, &appid_str);
                    plist_t disp_p = plist_dict_get_item(app_info, "CFBundleDisplayName");
                    if (disp_p)
                        plist_get_string_val(disp_p, &name_str);
                    /*
                     char *path_str=NULL;
                     plist_t path_p = plist_dict_get_item(app_info, "Path");
                     if (path_p)
                     plist_get_string_val(path_p, &path_str);
                     */
                    //fprintf(stderr, "Identifier: %s, Name: %s Path: %s\n", appid_str, name_str, path_str);
                    
                    if (filterSharing == true && sharing == true && systemApp == false){
                        if (xml){
                            plist_array_append_item(appList, app_info);
                        } else {
                            fprintf(stderr, "%s : %s\n", name_str, appid_str);
                        }
                        
                    } else if (filterSharing == false) {
                        if (xml){
                            
                            plist_array_append_item(appList, app_info);
                        } else {
                            fprintf(stderr, "%s : %s\n", name_str, appid_str);
                        }
                    }
                    if (appid_str)
                        free(appid_str);
                    if (name_str)
                        free(name_str);
                    
                }
                
                if (xml) {
                    plist_to_xml(appList, &xmlData, &length);
                    printf("%s\n", xmlData);
                    
                }
                if (appList) {
                    plist_free(appList);
                }
                
            } else {
                fprintf(stderr, "Error: Unable to browse applications. Error code %s\n", idev_instproxy_strerror(err));
            }
            
            //if (apps)
            //  plist_free(apps);
            
            if (client_opts) {
                plist_free(client_opts);
            }
        } else {
            fprintf(stderr, "Error: Could not connect to installation_proxy!\n");
        }
        
        if (ipc) {
            instproxy_client_free(ipc);
        }
    } else {
        fprintf(stderr, "Error: Could not start com.apple.mobile.installation_proxy!\n");
    }
    
    if (ldsvc){
        lockdownd_service_descriptor_free(ldsvc);
    }
}

// Retrieve the device local path to the app binary based on its display name or bundle id
char * idev_get_app_path(idevice_t idevice, lockdownd_client_t lockd, const char *app) {
    char *ret=NULL;
    char *path_str=NULL;
    char *exec_str=NULL;
    
    if (idev_verbose) { fprintf(stderr, "[debug]: looking up exec path for %s\n", app); }
    
    lockdownd_service_descriptor_t ldsvc = NULL;
    if ((lockdownd_start_service(lockd, "com.apple.mobile.installation_proxy", &ldsvc) == LOCKDOWN_E_SUCCESS) && ldsvc) {
        
        instproxy_client_t ipc = NULL;
        if (instproxy_client_new(idevice, ldsvc, &ipc) == INSTPROXY_E_SUCCESS) {
            
            plist_t client_opts = instproxy_client_options_new();
            
            plist_t apps = NULL;
            instproxy_error_t err = instproxy_browse(ipc, client_opts, &apps);
            if (err == INSTPROXY_E_SUCCESS) {
                plist_t app_found = NULL;
                
                app_found = NULL;
                uint32_t i;
                for (i = 0; i < plist_array_get_size(apps); i++) {
                    char *appid_str = NULL;
                    char *name_str = NULL;
                    
                    plist_t app_info = plist_array_get_item(apps, i);
                    plist_t appid_p = plist_dict_get_item(app_info, "CFBundleIdentifier");
                    if (appid_p){
                        plist_get_string_val(appid_p, &appid_str);
                    }
                    plist_t disp_p = plist_dict_get_item(app_info, "CFBundleDisplayName");
                    if (disp_p) {
                        plist_get_string_val(disp_p, &name_str);
                    }
                    if (appid_str && strcmp(app, appid_str) == 0) {
                        if (!app_found)
                            app_found = app_info;
                        else
                            fprintf(stderr, "Error: ambigous bundle ID: %s\n", app);
                    }
                    
                    if (name_str && strcmp(app, name_str) == 0) {
                        if (!app_found) {
                            app_found = app_info;
                        } else {
                            fprintf(stderr, "Error: ambigous app name: %s\n", app);
                        }
                    }
                    
                    if (appid_str) {
                        free(appid_str);
                    }
                    if (name_str) {
                        free(name_str);
                    }
                }
                
                if (app_found) {
                    plist_t path_p = plist_dict_get_item(app_found, "Path");
                    if (path_p)
                        plist_get_string_val(path_p, &path_str);
                    
                    
                    plist_t exe_p = plist_dict_get_item(app_found, "CFBundleExecutable");
                    if (exe_p)
                        plist_get_string_val(exe_p, &exec_str);
                    
                } else {
                    fprintf(stderr, "Error: No app found with name or bundle id: %s\n", app);
                }
                
            } else {
                fprintf(stderr, "Error: Unable to browse applications. Error code %s\n", idev_instproxy_strerror(err));
            }
            
            if (apps) {
                plist_free(apps);
            }
            if (client_opts) {
                plist_free(client_opts);
            }
        } else {
            fprintf(stderr, "Error: Could not connect to installation_proxy!\n");
        }
        
        if (ipc) {
            instproxy_client_free(ipc);
        }
        
    } else {
        fprintf(stderr, "Error: Could not start com.apple.mobile.installation_proxy!\n");
    }
    
    if (ldsvc) {
        lockdownd_service_descriptor_free(ldsvc);
    }
    if (path_str) {
        if (exec_str) {
            //  asprintf(&ret, "%s/%s", path_str, exec_str);
            if (idev_verbose) { fprintf(stderr, "[debug]: found exec path: %s\n", ret); }
            
        } else {
            fprintf(stderr, "Error: bundle executable not found\n");
        }
    } else {
        fprintf(stderr, "Error: app path not found\n");
    }
    return ret;
}


#pragma mark - AFC helpers


/* 
 
 this appears to be used when there is no application being targeted for use with the afc client.
 
 not creating any direct equivalent because its use case will be covered with idev_afc_client
 
 */

/*
 
 int idev_afc_client_ex(
 char *clientname,
 char *udid,
 char *afc_servicename,
 int(^block)(idevice_t idev, lockdownd_client_t client, lockdownd_service_descriptor_t ldsvc, afc_client_t afc) )
 {
 return idev_lockdownd_start_service (
 clientname,
 udid,
 afc_servicename,
 ^int(idevice_t idev, lockdownd_client_t client, lockdownd_service_descriptor_t ldsvc)
 {
 int ret=EXIT_FAILURE;
 afc_client_t afc = NULL;
 afc_error_t afc_err = afc_client_new(idev, ldsvc, &afc);
 
 if (afc_err == AFC_E_SUCCESS && afc) {
 
 ret = block(idev, client, ldsvc, afc);
 
 } else {
 fprintf(stderr, "Error: unable to create afc client: %s\n", idev_afc_strerror(afc_err));
 }
 
 if (afc) afc_client_free(afc);
 
 return ret;
 });
 }
 */

//this is for when we don't have a specific app to target, sans blocks.

afc_client_t idev_afc_client(char *clientname, char *udid, char *servicename, int *error)
{
    afc_client_t afc = NULL;
    idevice_t idev = NULL;
    
    if (!clientname)
        clientname = "idevtool";
    
    idevice_error_t ierr=idevice_new(&idev, udid);
    
    if (ierr == IDEVICE_E_SUCCESS && idev) {
        lockdownd_client_t client = NULL;
        lockdownd_error_t ldret = lockdownd_client_new_with_handshake(idev, &client, clientname);
        
        if (ldret == LOCKDOWN_E_SUCCESS && client) {
            
            
            //if (idev_verbose) fprintf(stderr, "[debug] starting '%s' lockdownd service\n", servicename);
            
            lockdownd_service_descriptor_t ldsvc = NULL;
            lockdownd_error_t ldret = lockdownd_start_service(client, servicename, &ldsvc);
            
            if ((ldret == LOCKDOWN_E_SUCCESS) && ldsvc) {
                
                
                afc_error_t afc_err = afc_client_new(idev, ldsvc, &afc);
                if (afc_err == AFC_E_SUCCESS && afc) {
                    
                    *error = 0;
                    return afc;
                    
                } else {
                    
                    fprintf(stderr, "Error: could not start afc client %s\n", idev_afc_strerror(afc_err));
                    *error = afc_err;
                }
                
                if (afc)
                    afc_client_free(afc);
                
            } else {
                fprintf(stderr, "Error: could not start service: %s", servicename);
                *error = ldret;
            }
            
            if (ldsvc) lockdownd_service_descriptor_free(ldsvc);
            
            return afc;
            
            
        } else {
            fprintf(stderr, "Error: Can't connect to lockdownd: %s.\n", idev_lockdownd_strerror(ldret));
        }
        
        if (client)
            lockdownd_client_free(client);
        
    } else if (ierr == IDEVICE_E_NO_DEVICE) {
        fprintf(stderr, "Error: No device found -- Is it plugged in?\n");
    } else {
        fprintf(stderr, "Error: Cannot connect to device: %s\n", idev_idevice_strerror(ierr));
    }
    
    if (idev)
        idevice_free(idev);
    
    return afc;
    
}


//re-written above without blocks.

/*
 
 int idev_afc_client(char *clientname, char *udid, bool root, int(^block)(afc_client_t afc))
 {
 return idev_afc_client_ex (
 clientname,
 udid,
 ((root)? AFC2_SERVICE_NAME : AFC_SERVICE_NAME),
 ^int(idevice_t i, lockdownd_client_t c, lockdownd_service_descriptor_t l, afc_client_t afc)
 {
 return block(afc);
 });
 }
 */

//re-written without blocks, this is when we ARE targeting a specific application.

afc_client_t idev_afc_app_client(char *clientname, char *udid, char *appid, int *error) {
    afc_client_t afc=NULL;
    idevice_t idev = NULL;
    
    if (!clientname)
        clientname = "idevtool";
    
    idevice_error_t ierr=idevice_new(&idev, udid);
    
    if (ierr == IDEVICE_E_SUCCESS && idev) {
        lockdownd_client_t client = NULL;
        lockdownd_error_t ldret = lockdownd_client_new_with_handshake(idev, &client, clientname);
        
        if (ldret == LOCKDOWN_E_SUCCESS && client) {
            *error = EXIT_FAILURE;
            
            lockdownd_service_descriptor_t ldsvc=NULL;
            lockdownd_error_t lret = lockdownd_start_service(client, HOUSE_ARREST_SERVICE_NAME, &ldsvc);
            
            if (lret == LOCKDOWN_E_SUCCESS && ldsvc) {
                house_arrest_client_t ha_client=NULL;
                house_arrest_error_t ha_err = house_arrest_client_new(idev, ldsvc, &ha_client);
                
                if (ha_err == HOUSE_ARREST_E_SUCCESS && ha_client) {
                    ha_err = house_arrest_send_command(ha_client, "VendDocuments", appid);
                    
                    if (ha_err == HOUSE_ARREST_E_SUCCESS) {
                        plist_t dict = NULL;
                        ha_err = house_arrest_get_result(ha_client, &dict);
                        
                        if (ha_err == HOUSE_ARREST_E_SUCCESS && dict) {
                            plist_t errnode = plist_dict_get_item(dict, "Error");
                            
                            if (!errnode) {
                                afc_client_t afc=NULL;
                                afc_error_t afc_err = afc_client_new_from_house_arrest_client(ha_client, &afc);
                                
                                if (afc_err == AFC_E_SUCCESS && afc) {
                                    
                                    *error = 0;
                                    return afc;
                                    
                                } else {
                                    fprintf(stderr, "Error: could not get afc client from house arrest: %s\n", idev_afc_strerror(afc_err));
                                }
                                
                                if (afc)
                                    afc_client_free(afc);
                                
                            } else {
                                char *str = NULL;
                                plist_get_string_val(errnode, &str);
                                fprintf(stderr, "Error: house_arrest service responded: %s\n", str);
                                if (strcmp("InstallationLookupFailed", str) == 0)
                                {
                                    *error = 20;
                                }
                                if (str)
                                    free(str);
                            }
                        } else {
                            fprintf(stderr, "Error: Could not get result form house_arrest service: %s\n",
                                    idev_house_arrest_strerror(ha_err));
                        }
                        
                        if (dict)
                            plist_free(dict);
                        
                    } else {
                        fprintf(stderr, "Error: Could not send VendContainer command with argument:%s - %s\n",
                                appid, idev_house_arrest_strerror(ha_err));
                    }
                    
                } else {
                    fprintf(stderr, "Error: Unable to create house arrest client: %s\n", idev_house_arrest_strerror(ha_err));
                }
                
                if (ha_client)
                    house_arrest_client_free(ha_client);
                
            } else {
                fprintf(stderr, "Error: unable to start service: %s - %s\n", HOUSE_ARREST_SERVICE_NAME, idev_lockdownd_strerror(lret));
            }
            
            return afc;
        } else {
            fprintf(stderr, "Error: Can't connect to lockdownd: %s.\n", idev_lockdownd_strerror(ldret));
        }
        
        if (client)
            lockdownd_client_free(client);
        
    } else if (ierr == IDEVICE_E_NO_DEVICE) {
        fprintf(stderr, "Error: No device found -- Is it plugged in?\n");
    } else {
        fprintf(stderr, "Error: Cannot connect to device: %s\n", idev_idevice_strerror(ierr));
    }
    
    if (idev)
        idevice_free(idev);
    return afc;
}

//definitely use this method, so it needs to be refactored because it uses blocks

//last one to comment out and test to see if stuff still works!!

/*
 
 int idev_afc_app_client(char *clientname, char *udid, char *appid, int(^block)(afc_client_t afc))
 {
 return idev_lockdownd_client(clientname, udid, ^int(idevice_t idev, lockdownd_client_t client) {
 int ret = EXIT_FAILURE;
 
 lockdownd_service_descriptor_t ldsvc=NULL;
 lockdownd_error_t lret = lockdownd_start_service(client, HOUSE_ARREST_SERVICE_NAME, &ldsvc);
 
 if (lret == LOCKDOWN_E_SUCCESS && ldsvc) {
 
 house_arrest_client_t ha_client=NULL;
 house_arrest_error_t ha_err = house_arrest_client_new(idev, ldsvc, &ha_client);
 
 if (ha_err == HOUSE_ARREST_E_SUCCESS && ha_client) {
 
 ha_err = house_arrest_send_command(ha_client, "VendContainer", appid);
 
 if (ha_err == HOUSE_ARREST_E_SUCCESS) {
 plist_t dict = NULL;
 ha_err = house_arrest_get_result(ha_client, &dict);
 
 if (ha_err == HOUSE_ARREST_E_SUCCESS && dict) {
 plist_t errnode = plist_dict_get_item(dict, "Error");
 
 if (!errnode) {
 afc_client_t afc=NULL;
 afc_error_t afc_err = afc_client_new_from_house_arrest_client(ha_client, &afc);
 
 if (afc_err == AFC_E_SUCCESS && afc) {
 
 ret = block(afc);
 
 } else {
 fprintf(stderr, "Error: could not get afc client from house arrest: %s\n", idev_afc_strerror(afc_err));
 }
 
 if (afc)
 afc_client_free(afc);
 
 } else {
 char *str = NULL;
 plist_get_string_val(errnode, &str);
 fprintf(stderr, "Error: house_arrest service responded: %s\n", str);
 if (strcmp("InstallationLookupFailed", str) == 0)
 {
 ret = 20;
 }
 if (str)
 free(str);
 }
 } else {
 fprintf(stderr, "Error: Could not get result form house_arrest service: %s\n",
 idev_house_arrest_strerror(ha_err));
 }
 
 if (dict)
 plist_free(dict);
 
 } else {
 fprintf(stderr, "Error: Could not send VendContainer command with argument:%s - %s\n",
 appid, idev_house_arrest_strerror(ha_err));
 }
 
 } else {
 fprintf(stderr, "Error: Unable to create house arrest client: %s\n", idev_house_arrest_strerror(ha_err));
 }
 
 if (ha_client)
 house_arrest_client_free(ha_client);
 
 } else {
 fprintf(stderr, "Error: unable to start service: %s - %s\n", HOUSE_ARREST_SERVICE_NAME, idev_lockdownd_strerror(lret));
 }
 
 return ret;
 });
 }
 */
