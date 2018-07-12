#include "tracker.h"

static int initMysql () {
    MYSQL_RES *res;
    MYSQL_ROW row;
    const int mysql_reconnect_flag = 1;
    mysql = mysql_init(NULL);

    mysql_optionsv(mysql, MYSQL_OPT_RECONNECT, &mysql_reconnect_flag);
    fprintf(stderr, "\n\n%s\n\n", mysql_get_client_info());
    mysql = mysql_real_connect(mysql, getenv("MYSQL_HOST"), "tracker", "trackerX", "tracker", 0, NULL, 0);
    //if (mysql -> server_status == MYSQL_STATUS)
    if (mysql == NULL) {
        fprintf(stderr, "Error connecting to MySQL server. \n");
        return 1;
    }
    fprintf(stderr, "\n\n%s\n\n", mysql_get_server_info(mysql));

    updateDeviceStmt = mysql_stmt_init(mysql);
    if (mysql_stmt_prepare(updateDeviceStmt, UPDATE_DEVICE_SQL, strlen(UPDATE_DEVICE_SQL))) check_mysql();
    memset(updateDeviceBind, 0, sizeof(updateDeviceBind));
    updateDeviceBind[0].buffer_type = MYSQL_TYPE_STRING;
    updateDeviceBind[0].buffer_length = 20;
    updateDeviceBind[0].buffer = device_param;
    updateDeviceBind[0].is_null = 0;
    updateDeviceBind[0].length = &device_param_length;
    if (mysql_stmt_bind_param(updateDeviceStmt, updateDeviceBind)) check_mysql();

    insertHotspotStmt = mysql_stmt_init(mysql);
    if (mysql_stmt_prepare(insertHotspotStmt, INSERT_HOTSPOT_SQL, strlen(INSERT_HOTSPOT_SQL))) check_mysql();
    memset(insertHotspotBind, 0, sizeof(insertHotspotBind));
    insertHotspotBind[0].buffer_type = MYSQL_TYPE_STRING;
    insertHotspotBind[0].buffer_length = 32;
    insertHotspotBind[0].buffer = ssid_param;
    insertHotspotBind[0].is_null = 0;
    insertHotspotBind[0].length = &ssid_param_length;
    insertHotspotBind[1].buffer_type = MYSQL_TYPE_STRING;
    insertHotspotBind[1].buffer_length = 21;
    insertHotspotBind[1].buffer = bssid_param;
    insertHotspotBind[1].is_null = 0;
    insertHotspotBind[1].length = &bssid_param_length;
    insertHotspotBind[2].buffer_type = MYSQL_TYPE_TINY;
    insertHotspotBind[2].buffer = (char*)&channel_param;
    insertHotspotBind[2].is_null = 0;
    if (mysql_stmt_bind_param(insertHotspotStmt, insertHotspotBind)) check_mysql();

    if (mysql_query(mysql, "SELECT `id`, `device` FROM `tracker_device`")) check_mysql();
    res = mysql_store_result(mysql);
    if (res == NULL) check_mysql();
    device_to_id.clear();
    while (row = mysql_fetch_row(res)) {
        string id = string(row[0]);
        string device = string(row[1]);
        device_to_id[device] = id;
        cout
            << "device: "
            << id
            << ": "
            << device
            << endl;
    }
    mysql_free_result(res);

    if (mysql_query(mysql, "SELECT `id`, `bssid` FROM `tracker_hotspot`")) {
        check_mysql();
    }
    res = mysql_store_result(mysql);
    if (res == NULL) check_mysql();
    bssid_to_id.clear();
    while (row = mysql_fetch_row(res)) {
        string id = string(row[0]);
        string bssid = string(row[1]);
        bssid_to_id[bssid] = id;
        cout
            << "bssid: "
            << id
            << " : "
            << bssid
            << endl;
    }
    mysql_free_result(res);

    return 0;
}

static void closeMysql () {
    mysql_close(mysql);
    mysql_stmt_close(updateDeviceStmt);
    mysql_stmt_close(insertHotspotStmt);
}

static string updateDevice (string device) {
    strncpy(device_param, device.data(), 20);
    device_param_length = device.length();
    if (mysql_stmt_execute(updateDeviceStmt)) {
        warn_mysql("Update device");
        return "";
    }
    auto id = mysql_insert_id(mysql);
    cerr
        << "new device: "
        << id
        << " : "
        << device
        << endl;

    return to_string(id);
}

static unsigned long long addHotspot () {
    ssid_param_length = strlen(ssid_param);
    bssid_param_length = strlen(bssid_param);
    if (mysql_stmt_execute(insertHotspotStmt)) {
        warn_mysql("Add hotspot");
        return 0;
    }
    auto id = mysql_insert_id(mysql);
    cerr
        << "new hotspot: "
        << id
        << " : "
        << bssid_param
        << " : "
        << ssid_param
        << endl;

    return id;
}

static bool reportPoint (string device_id, string hotspot_id, int rssi) {
    CURLcode code;
    string url = string(getenv("INFLUXDB_URL")) + "/write?db=tracker";
    std::ostringstream send_data;
    send_data
        << "signal,device="
        << device_id
        << ",hotspot="
        << hotspot_id
        << " rssi="
        << rssi;
    string content = send_data.str();
    curl_easy_setopt(curl, CURLOPT_URL, url.data());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Tracker/1.0 bdbai");
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, content.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, content.length());
    if ((code = curl_easy_perform(curl)) != CURLE_OK) {
        warn_curl(code);
        return false;
    }

    long res_code;
    curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &res_code);
    if (res_code == 204) {
        return true;
    } else {
        cerr
            << "Warning: InfluxDB returned code "
            << res_code
            << endl;
        return false;
    }
}

static inline void cTrim (char *str) {
    int i = strlen(str);
    while (--i >= 0 && (str[i] == '\0' || str[i] == '\n' || str[i] == '\r')) {
        str[i] = '\0';
    }
}

static void PrintEnv(FCGX_Stream *out, char *label, char **envp)
{
    FCGX_FPrintF(out, "%s:<br>\n<pre>\n", label);
    for( ; *envp != NULL; envp++)
        FCGX_FPrintF(out, "%s\n", *envp);
    
    FCGX_FPrintF(out, "</pre><p>\n");
}

static void PrintResponse (FCGX_Stream *out, string status, string body) {
    FCGX_FPrintF(out,
    "Status: %s\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: %ld\r\n"
    "\r\n%s", status.c_str(), body.length(), body.c_str());
}

int main ()
{
    FCGX_Stream *in, *out, *err;
    FCGX_ParamArray envp;
    curl = curl_easy_init();
    if (initMysql()) {
        return 1;
    }

    while (FCGX_Accept(&in, &out, &err, &envp) >= 0) {
        if (mysql == NULL && initMysql()) {
            PrintResponse(out, "500 Internal Server Error", "error connecting to mysql");
            fprintf(stderr, "Error reconnecting to mysql\n");
            continue; 
        }
        if (mysql_ping(mysql)) {
            warn_mysql("Ping");
            fprintf(stderr, "Reconnecting to mysql\n");
            closeMysql();
            if (initMysql()) {
                PrintResponse(out, "500 Internal Server Error", "error connecting to mysql");
                fprintf(stderr, "Error reconnecting to mysql\n");
                continue;
            }
        }
        char *contentLength = FCGX_GetParam("CONTENT_LENGTH", envp);
        int len = 0;
        char channel_c[3], rssi_c[5];

        if (contentLength != NULL)
            len = strtol(contentLength, NULL, 10);

        if (len <= 0) {
            PrintResponse(out, "400 Bad Request", "No data from input.");
        }
        else {
            int i, ch;

            char* device_c = FCGX_GetParam("DEVICE_ID", envp);
            if (device_c == NULL || device_c[0] == '\0') {
                PrintResponse(out, "400 Bad Request", "No device id.");
                continue;
            }
            string device = string(device_c);
            string device_id = device_to_id[device];
            if (device_id.empty()) {
                if ((device_id = updateDevice(device)).empty()) {
                    PrintResponse(out, "400 Bad Request", "Bad device id.");
                    continue;
                } else {
                    device_to_id[device] = device_id;
                }
            }
            
            bool succeed = true;
            while (FCGX_GetLine(ssid_param, 32, in) != NULL) {
                cTrim(ssid_param);
                if (ssid_param[0] == '\0') break;
                if (!FCGX_GetLine(channel_c, 16, in)) goto INVALID_BODY;
                if (!FCGX_GetLine(rssi_c, 16, in)) goto INVALID_BODY;
                if (!FCGX_GetLine(bssid_param, 32, in)) goto INVALID_BODY;
                channel_param = strtol(channel_c, NULL, 10);
                rssi_param = strtol(rssi_c, NULL, 10);
                cTrim(bssid_param);

                string bssid = string(bssid_param);
                string hotspot_id = bssid_to_id[bssid];
                if (hotspot_id.empty()) {
                    int hotspot_new_id = addHotspot();
                    if (hotspot_new_id == 0) {
                        goto INVALID_BODY;
                    }
                    hotspot_id = to_string(hotspot_new_id);
                    bssid_to_id[bssid] = hotspot_id;
                }

                succeed = reportPoint(device_id, hotspot_id, rssi_param) && succeed;
            }
            /*
            for (i = 0; i < len; i++) {
                if ((ch = FCGX_GetChar(in)) < 0) {
                    FCGX_FPrintF(out,
                        "Error: Not enough bytes received on standard input<p>\n");
                    break;
                }
                FCGX_PutChar(ch, out);
            }*/
            if (succeed) {
                PrintResponse(out, "200 OK", "success");
            } else {
                PrintResponse(out, "500 Internal Server Error", "unable to write");
            }
            continue;
            INVALID_BODY:
            PrintResponse(out, "400 Bad Request", "Invalid parameters.");
        }
    } /* while */
    closeMysql();
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return 0;
}
