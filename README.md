# espdrop
ESPDrop is a very basic and plain implementation of a minimalistic Dropbox. Upload, Drag and Drop Files (Images, Audio, Video, Texts) and Save Notes and access through any device connected to same local WiFi.

# Usage
This is a native ESP-IDF project for **ESP32-S3**. Implements on long `main.c`. Should be supported across the ESP32 Family (not a guaranteed drop-in on every board).
ESPDrop runs on the private local IP address assigned by your Wi-Fi router server, thus every device connected to the same WiFi is able to access it.
In `main.c` define your Wifi Name and Password accordingly!
```
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASS "YOUR_WIFI_PASS"
```
Set the flash size to **16 MB** (or as board supports) in _menuconfig_. Ensure custom partition table is enabled so `partitions.csv` is actually used.

```
idf.py build
idf.py -p COMx flash monitor
```
for windows COMx could likely be COM3 or COM5, verify for your os before using.

That's it! You'll see your private IP in the monitor logs `Open: 192.168.x.x`
## How to use
<img width="809" height="649" alt="{7626B736-A9B8-4992-83A0-517F42601092}" src="https://github.com/user-attachments/assets/a9df597a-3259-4b30-a54c-6e6c58074ca7" />
<img width="790" height="645" alt="image" src="https://github.com/user-attachments/assets/9988db06-042e-4859-924e-e59341785102" />

Since its a very minimalistic UI the features are straightforward. 
Upload Files from you device. Write Notes to save them in a scrollable card (very similar to google keep)
* The browser handles most UI work: drag/drop, upload progress, image preview, note editing, etc.
* Image uploads also create a small thumbnail capped at 30KB; the preview gallery loads thumbnails thus image gallery is loaded instantly, while opening an image loads the full original file.
* Upload/download handlers stream data in chunks instead of loading whole files into RAM.
* Uses FATFS + wear levelling on the storage partition for files and notes.
* Files are stored internally by generated IDs, while the original filename is kept separately in metadata.
* Large transfer buffers are allocated dynamically to avoid overflowing the HTTP server task stack.
