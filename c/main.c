#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <gpiod.h>
#include <math.h>
#include <time.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include "mqttclient.h"
#include "cJSON.h"

// 设备信息
#define TUYA_DEVICE_ID			        "2632d447810xxxxxxxxxxx"
#define TUYA_DEVICE_SECRET              "qxZS1LhriSxxxxxx"

// MQTT服务器信息
#define TUYA_HOST                       "m1.tuyacn.com"
#define TUYA_PORT                       "8883"
#define TUYA_CLIENT_ID                  "tuyalink_"TUYA_DEVICE_ID

// MQTT主题
#define TUYA_TOPIC_PUBLISH              "tylink/"TUYA_DEVICE_ID"/thing/property/report"

mqtt_client_t *client = NULL;                     
pthread_t mqtt_publish_thread_obj;                

typedef struct sensor_mes {
    char name[20];
    char value[10];
} sensor_mes_t;

// CA 证书，用于 TLS 连接
static const char *ca_crt = {
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDxTCCAq2gAwIBAgIBADANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx\n"
  "EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT\n"
  "EUdvRGFkZHkuY29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRp\n"
  "ZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5MDkwMTAwMDAwMFoXDTM3MTIzMTIz\n"
  "NTk1OVowgYMxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6b25hMRMwEQYDVQQH\n"
  "EwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSwgSW5jLjExMC8GA1UE\n"
  "AxMoR28gRGFkZHkgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIw\n"
  "DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAL9xYgjx+lk09xvJGKP3gElY6SKD\n"
  "E6bFIEMBO4Tx5oVJnyfq9oQbTqC023CYxzIBsQU+B07u9PpPL1kwIuerGVZr4oAH\n"
  "/PMWdYA5UXvl+TW2dE6pjYIT5LY/qQOD+qK+ihVqf94Lw7YZFAXK6sOoBJQ7Rnwy\n"
  "DfMAZiLIjWltNowRGLfTshxgtDj6AozO091GB94KPutdfMh8+7ArU6SSYmlRJQVh\n"
  "GkSBjCypQ5Yj36w6gZoOKcUcqeldHraenjAKOc7xiID7S13MMuyFYkMlNAJWJwGR\n"
  "tDtwKj9useiciAF9n9T521NtYJ2/LOdYq7hfRvzOxBsDPAnrSTFcaUaz4EcCAwEA\n"
  "AaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYE\n"
  "FDqahQcQZyi27/a9BUFuIMGU2g/eMA0GCSqGSIb3DQEBCwUAA4IBAQCZ21151fmX\n"
  "WWcDYfF+OwYxdS2hII5PZYe096acvNjpL9DbWu7PdIxztDhC2gV7+AJ1uP2lsdeu\n"
  "9tfeE8tTEH6KRtGX+rcuKxGrkLAngPnon1rpN5+r5N9ss4UXnT3ZJE95kTXWXwTr\n"
  "gIOrmgIttRD02JDHBHNA7XIloKmf7J6raBKZV8aPEjoJpL1E/QYVN8Gb5DKj7Tjo\n"
  "2GTzLH4U/ALqn83/B2gX2yKQOC16jdFU8WnjXzPKej17CuPKf1855eJ1usV2GDPO\n"
  "LPAvTK33sefOT6jEm0pUBsV/fdUID+Ic/n4XuKxe9tQWskMJDE32p2u0mYRlynqI\n"
  "4uJEvlz36hz1\n"
  "-----END CERTIFICATE-----\n"
};

void generate_hmac_sha256(const char *key, const char *data, char *output) 
{
    unsigned char *digest;
    digest = HMAC(EVP_sha256(), key, strlen(key), (unsigned char *)data, strlen(data), NULL, NULL);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) 
    {
        sprintf(output + (i * 2), "%02x", digest[i]);
    }
}

static cJSON *create_tuya_json(sensor_mes_t *sensor) 
{   
    if(sensor == NULL)
        return NULL;

    if(sensor->name == NULL || sensor->value == NULL)
        return NULL;

    time_t t = time(NULL);
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%ld", t);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "msgID", timestamp);
    cJSON_AddStringToObject(root, "time", timestamp);

    cJSON *data = cJSON_CreateObject();

    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "value", sensor->value);
    cJSON_AddStringToObject(item, "time", timestamp);

    cJSON_AddItemToObject(data, sensor->name, item);
    cJSON_AddItemToObject(root, "data", data);

    return root;
}

static void sensor_mes_init(sensor_mes_t *sensor, const char *name, const char *value)
{
    if(sensor == NULL)
        return;
    
    if(name == NULL || value == NULL)
        return;

    strcpy(sensor->name, name);
    strcpy(sensor->value, value);
}

void *mqtt_publish_thread(void *arg)
{
    mqtt_client_t *client = (mqtt_client_t *)arg;

    int ret;
    mqtt_message_t msg;
    sensor_mes_t temp_mes_t;

    cJSON *tuya_json;
    char *tuya_json_str;

    memset(&msg, 0, sizeof(msg));
    msg.qos = 0;

    sleep(2);

    while(1) 
    {
        sensor_mes_init(&temp_mes_t, "temperature", "25.5");
        tuya_json = create_tuya_json(&temp_mes_t);

        tuya_json_str = cJSON_Print(tuya_json);
        msg.payload = (void *)tuya_json_str;

        mqtt_publish(client, TUYA_TOPIC_PUBLISH, &msg);
        cJSON_Delete(tuya_json);
        tuya_json = NULL;

        sleep(3);
    }
}

int mqtt_init()
{
    int rc = 0;
    char username[256];
    char data_for_signature[256];
    char password[65];

    // 获取当前时间戳
    time_t t = time(NULL);
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%ld", t);

    // 构建username和password
    snprintf(username, sizeof(username), "%s|signMethod=hmacSha256,timestamp=%s,secureMode=1,accessType=1", TUYA_DEVICE_ID, timestamp);
    snprintf(data_for_signature, sizeof(data_for_signature), "deviceId=%s,timestamp=%s,secureMode=1,accessType=1", TUYA_DEVICE_ID, timestamp);
    generate_hmac_sha256(TUYA_DEVICE_SECRET, data_for_signature, password);
    // printf("UserName: %s\n", username);
    // printf("Password: %s\n", password);

    // 初始化 MQTT 客户端
    client = mqtt_lease();
    if(client == NULL)
        return -1;

    mqtt_set_port(client, TUYA_PORT);               // 设置 MQTT 消息的端口
    mqtt_set_host(client, TUYA_HOST);               // 设置 MQTT 消息的主机
    mqtt_set_ca(client, (char*)ca_crt);             // 设置 CA 证书
    mqtt_set_user_name(client, username);           // 设置 MQTT 用户名
    mqtt_set_password(client, password);            // 设置 MQTT 密码
    mqtt_set_client_id(client, TUYA_CLIENT_ID);     // 设置 MQTT 客户端 ID
    mqtt_set_clean_session(client, 1);              // 设置 MQTT 会话为清除模式  

    // 连接到 MQTT 服务器
    mqtt_connect(client);

    // 创建 MQTT 发布线程
    rc = pthread_create(&mqtt_publish_thread_obj, NULL, mqtt_publish_thread, client);
    if(rc!= 0) 
    {
        MQTT_LOG_E("create mqtt publish thread fail");
        return -1;
    }

    return 0;
}

int main()
{
    int ret;
    
    mqtt_log_init();
    ret = mqtt_init();
    if(ret == -1)
    {
        MQTT_LOG_E("mqtt init fail\n");
        return -1;
    }

    while (1) 
    {
        sleep(1);
    }

    return 0;
}
