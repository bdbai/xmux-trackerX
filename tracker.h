/*
 * echo2.c --
 *
 *      Produce a page containing all the inputs (fcgiapp version)
 *
 *
 * Copyright (c) 1996 Open Market, Inc.
 *
 * See the file "LICENSE.TERMS" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "fcgi_config.h"

#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef _WIN32
#include <process.h>
#else
extern char **environ;
#endif

#include "fcgiapp.h"
#include "mysql/mysql.h"
#include "curl/curl.h"
#include "stdio.h"
#include <map>
#include <list>
#include <cstring>
#include <iostream>
#include <sstream>

using namespace std;

#define UPDATE_DEVICE_SQL "INSERT INTO `tracker_device` (`device`) VALUES (?)"
#define INSERT_HOTSPOT_SQL "INSERT INTO `tracker_hotspot` (`ssid`, `bssid`, `channel`) VALUES (?, ?, ?)"

map<string, string> device_to_id;
map<string, string> bssid_to_id;
CURL *curl;
MYSQL *mysql;
MYSQL_STMT *updateDeviceStmt, *insertHotspotStmt;
MYSQL_BIND updateDeviceBind[1], insertHotspotBind[3];
char device_param[32], ssid_param[32], bssid_param[32];
int channel_param, rssi_param;
unsigned long device_param_length, ssid_param_length, bssid_param_length;

void check_mysql() {
  if (mysql_errno(mysql)) {
    cerr << "Error connecting to mysql server: " << string(mysql_error(mysql)) << endl;
    exit(1);
  }
}

void warn_mysql() {
  if (mysql_errno(mysql)) {
    cerr << "Warning: mysql error: " << string(mysql_error(mysql)) << endl;
  }
}

void warn_curl(CURLcode code) {
  cerr << "Warning: curl error: " << string(curl_easy_strerror(code)) << endl;
}
