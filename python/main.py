import paho.mqtt.client as mqtt
import time
import hmac
import json
from hashlib import sha256
import uuid

# 设备信息
DeviceID = '2632d4478100fxxxxxxxxx'         # 替换成自己的DeviceID
DeviceSecret = 'qxZS1Lhrixxxxxxx'           # 替换成自己的DeviceSecret

# MQTT服务器信息
Address = 'm1.tuyacn.com'
Port = 8883
ClientID = 'tuyalink_' + DeviceID  

# 认证信息
T = int(time.time())
UserName = f'{DeviceID}|signMethod=hmacSha256,timestamp={T},secureMode=1,accessType=1'
data_for_signature = f'deviceId={DeviceID},timestamp={T},secureMode=1,accessType=1'.encode('utf-8')
appsecret = DeviceSecret.encode('utf-8')
Password = hmac.new(appsecret, data_for_signature, digestmod=sha256).hexdigest()

# MQTT回调函数（可选）
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT broker")
    else:
        print(f"Failed to connect to MQTT broker with code {rc}")

# 创建MQTT客户端
client = mqtt.Client(ClientID)
client.username_pw_set(UserName, Password)
client.tls_set()  				# 必须启用TLS
client.on_connect = on_connect  # 设置连接回调函数（可选）

# 连接到MQTT服务器
client.connect(Address, Port, 60)

# 等待连接建立
client.loop_start()
time.sleep(2)  

# 准备上报的数据
current_time = int(time.time() * 1000)
payload = json.dumps({
    "msgId":str(uuid.uuid4()),
    "time":current_time,
    "data":{
        "temperature":{
            "value": "26.5",
            "time": current_time  
        }
    }
})

# 上报数据到云平台
topic = f'tylink/{DeviceID}/thing/property/report'
client.publish(topic, payload)
print(f"Published payload: {payload} to topic: {topic}")

while True:
    time.sleep(1)

client.loop_stop()
client.disconnect()