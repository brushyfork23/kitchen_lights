# Undercabinet Lights

Motion-sensing undercabinet kitchen lights.  Fades in bright during the day and cool during the night.

# WiFi

On boot, an attempt will be made to connect to a WiFi network stored in memory.  
If that fails, the module will broadcast its own WiFi network to allow another device to connect to it and select a network to join.  
When compiling, replace WIFI_PASSWORD with a new password.
SSID: *ESP<chip Id>*  
password default: *password*  


# OTA Programming

Generate public/private keypair in the sketch root directory (the same directory as the .ino):

```
openssl genrsa -out private.key 2048
openssl rsa -in private.key -outform PEM -pubout -out public.key
```

The default OTA password is *otapassword*.  
Generate a new OTA password with MD5 and and replace OTA_PASSWORD_HASH.
```
MD5 ("otapassword") = 36c3c743a4ba3814668066866f2f6089
```
