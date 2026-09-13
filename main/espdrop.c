#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_vfs_fat.h"

#include "wear_levelling.h"
#include "nvs_flash.h"


#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASS "YOUR_WIFI_PASS"

#define STORAGE       "/fat"
#define FILES_DIR     "/fat/files"
#define NOTES_DIR     "/fat/notes"
#define THUMBS_DIR    "/fat/thumbs"
#define META_FILE     "/fat/files.meta"
#define META_TEMP     "/fat/files.tmp"

#define WIFI_CONNECTED BIT0

static const char *TAG = "ESPDrop";

static EventGroupHandle_t wifi_events;
static wl_handle_t wl_handle = WL_INVALID_HANDLE;

static uint32_t next_file_id = 1;
static uint32_t next_note_id = 1;


static const char INDEX_HTML[] =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESPDrop</title>"
"<style>"
"*{box-sizing:border-box}"
"body{margin:0;background:#111;color:#eee;font-family:system-ui,sans-serif}"
".container{max-width:900px;margin:28px auto;padding:18px}"
"h1{margin:0;font-size:28px}"
".subtitle{color:#888;margin:4px 0 22px}"
".card{background:#181818;border:1px solid #2a2a2a;border-radius:12px;overflow:hidden}"
".topbar{display:flex;align-items:center;justify-content:space-between;padding:12px 14px;border-bottom:1px solid #292929}"
".tabs{display:flex;gap:5px}"
".tab{background:transparent;border:0;color:#888;padding:7px 11px;border-radius:6px;cursor:pointer;font-size:14px}"
".tab:hover{background:#222;color:#ddd}"
".tab.active{background:#292929;color:#fff}"
".storage{color:#888;font-size:12px}"
".content{padding:16px}"
".page{display:none}"
".page.active{display:block}"
"button{border:1px solid #353535;background:#252525;color:#e5e5e5;padding:7px 10px;border-radius:6px;cursor:pointer;font-size:13px}"
"button:hover{background:#303030}"
".dropzone{border:1px dashed #444;border-radius:9px;padding:15px 10px;text-align:center;cursor:pointer;color:#999;margin-bottom:12px}"
".dropzone:hover,.dropzone.drag{background:#202020;border-color:#666;color:#ddd}"
"input[type=file]{display:none}"
".progress{display:none;margin-bottom:15px}"
".progress-info{display:flex;justify-content:space-between;color:#888;font-size:12px;margin-bottom:5px}"
".track{height:5px;background:#292929;border-radius:20px;overflow:hidden}"
".bar{width:0;height:100%;background:#888}"
".gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:9px;margin-bottom:15px}"
".image-card{background:#151515;border:1px solid #292929;border-radius:8px;overflow:hidden}"
".image-card img{width:100%;aspect-ratio:1/1;object-fit:cover;display:block;background:#0c0c0c;cursor:pointer}"
".image-actions{display:flex;gap:5px;padding:6px}"
".image-actions button{flex:1;padding:5px;font-size:11px}"
".file{display:flex;gap:10px;align-items:center;padding:9px 0;border-bottom:1px solid #292929}"
".ext{width:42px;height:36px;flex-shrink:0;border-radius:7px;border:1px solid #333;background:#202020;display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:700;color:#888;text-transform:uppercase}"
".file-info{flex:1;min-width:0}"
".filename{font-size:14px;overflow-wrap:anywhere}"
".filesize{color:#777;font-size:11px;margin-top:2px}"
".file-actions{display:flex;gap:5px}"
"textarea{width:100%;min-height:120px;background:#111;color:#eee;border:1px solid #333;border-radius:8px;padding:11px;font:inherit;resize:none;outline:none;overflow:hidden}"
"textarea:focus{border-color:#555}"
".save-row{display:flex;justify-content:flex-end;margin-top:8px}"
".notes{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:10px;margin-top:16px}"
".note{height:220px;background:#131313;border:1px solid #292929;border-radius:9px;padding:12px;display:flex;flex-direction:column;overflow:hidden;cursor:pointer}"
".note:hover{border-color:#444;background:#161616}"
".note-text{flex:1;overflow-y:auto;overflow-x:hidden;white-space:pre-wrap;overflow-wrap:anywhere;padding-right:7px;font-size:14px;line-height:1.45;scrollbar-width:thin;scrollbar-color:#383838 transparent}"
".note-text::-webkit-scrollbar{width:5px}"
".note-text::-webkit-scrollbar-track{background:transparent}"
".note-text::-webkit-scrollbar-thumb{background:#383838;border-radius:10px}"
".note-text::-webkit-scrollbar-thumb:hover{background:#505050}"
".note-actions{display:flex;gap:6px;margin-top:10px;padding-top:9px;border-top:1px solid #252525;flex-shrink:0}"
".overlay{position:fixed;inset:0;background:rgba(0,0,0,.82);display:none;align-items:center;justify-content:center;padding:20px;z-index:9999}"
".overlay.active{display:flex}"
".editor{width:min(850px,100%);height:min(720px,90vh);background:#181818;border:1px solid #333;border-radius:12px;padding:14px;display:flex;flex-direction:column}"
".editor textarea{flex:1;min-height:0;resize:none;overflow:auto}"
".editor-actions{display:flex;justify-content:flex-end;gap:7px;margin-top:10px}"
".image-view{max-width:95vw;max-height:90vh;object-fit:contain}"
"</style>"
"</head>"
"<body>"
"<div class='container'>"
"<h1>ESPDrop</h1>"
"<div class='subtitle'>Local files and notes</div>"
"<div class='card'>"
"<div class='topbar'>"
"<div class='tabs'>"
"<button class='tab active' data-page='filesPage'>Files</button>"
"<button class='tab' data-page='notesPage'>Notes</button>"
"</div>"
"<div id='storage' class='storage'>Loading...</div>"
"</div>"
"<div class='content'>"

"<div id='filesPage' class='page active'>"
"<div id='dropzone' class='dropzone'>Drop files here or click to choose</div>"
"<input id='fileInput' type='file' multiple>"
"<div id='progress' class='progress'>"
"<div class='progress-info'><span id='progressName'></span><span id='progressPct'>0%</span></div>"
"<div class='track'><div id='progressBar' class='bar'></div></div>"
"</div>"
"<div id='gallery' class='gallery'></div>"
"<div id='fileList'></div>"
"</div>"

"<div id='notesPage' class='page'>"
"<textarea id='noteInput' placeholder='Write or paste anything...'></textarea>"
"<div class='save-row'><button id='saveNote'>Save</button></div>"
"<div id='notes' class='notes'></div>"
"</div>"

"</div>"
"</div>"
"</div>"

"<div id='editorOverlay' class='overlay'>"
"<div class='editor'>"
"<textarea id='editorText'></textarea>"
"<div class='editor-actions'>"
"<button id='editorCancel'>Cancel</button>"
"<button id='editorSave'>Save</button>"
"</div>"
"</div>"
"</div>"

"<div id='imageOverlay' class='overlay'>"
"<img id='imageView' class='image-view'>"
"</div>"

"<script>"

"const $=id=>document.getElementById(id);"
"function grow(t){t.style.height='auto';t.style.height=t.scrollHeight+'px';}"
"$('noteInput').addEventListener('input',()=>grow($('noteInput')));"

"document.querySelectorAll('.tab').forEach(tab=>{"
"tab.onclick=()=>{"
"document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));"
"document.querySelectorAll('.page').forEach(x=>x.classList.remove('active'));"
"tab.classList.add('active');"
"$(tab.dataset.page).classList.add('active');"
"};"
"});"

"const dropzone=$('dropzone');"
"const fileInput=$('fileInput');"

"dropzone.onclick=()=>fileInput.click();"
"dropzone.ondragover=e=>{e.preventDefault();dropzone.classList.add('drag')};"
"dropzone.ondragleave=()=>dropzone.classList.remove('drag');"
"dropzone.ondrop=e=>{"
"e.preventDefault();"
"dropzone.classList.remove('drag');"
"uploadFiles([...e.dataTransfer.files]);"
"};"

"fileInput.onchange=()=>{"
"uploadFiles([...fileInput.files]);"
"fileInput.value='';"
"};"

"function fmt(n){"
"if(n<1024)return n+' B';"
"if(n<1048576)return(n/1024).toFixed(1)+' KB';"
"return(n/1048576).toFixed(2)+' MB';"
"}"

"function ext(name){"
"const i=name.lastIndexOf('.');"
"return i<0?'':name.slice(i+1).toLowerCase();"
"}"

"function imageName(name){"
"return['png','jpg','jpeg','webp'].includes(ext(name));"
"}"

"async function uploadFiles(files){"
"for(const file of files){"
"const id=await uploadOne(file);"
"if(imageName(file.name)){"
"try{"
"const thumb=await makeThumbnail(file);"
"if(thumb)await uploadThumbnail(id,thumb);"
"}catch(e){console.warn('Thumbnail failed',e);}"
"}"
"}"
"await loadAll();"
"}"

"function uploadOne(file){"
"return new Promise((resolve,reject)=>{"
"const xhr=new XMLHttpRequest();"
"xhr.open('POST','/api/upload?name='+encodeURIComponent(file.name));"
"xhr.timeout=180000;"
"$('progress').style.display='block';"
"$('progressName').textContent=file.name;"
"$('progressPct').textContent='0%';"
"$('progressBar').style.width='0%';"
"const done=()=>{$('progress').style.display='none';$('progressBar').style.width='0%';};"
"xhr.upload.onprogress=e=>{"
"if(e.lengthComputable){"
"const p=Math.round(e.loaded*100/e.total);"
"$('progressPct').textContent=p+'%';"
"$('progressBar').style.width=p+'%';"
"}"
"};"
"xhr.onload=()=>{"
"done();"
"if(xhr.status>=200&&xhr.status<300){"
"try{resolve(JSON.parse(xhr.responseText).id);}"
"catch(e){reject(new Error('Invalid upload response'));}"
"}else reject(new Error(xhr.responseText||'Upload failed'));"
"};"
"xhr.onerror=()=>{done();reject(new Error('Upload failed'));};"
"xhr.ontimeout=()=>{done();reject(new Error('Upload timed out'));};"
"xhr.send(file);"
"});"
"}"

"function uploadThumbnail(id,blob){"
"return new Promise((resolve,reject)=>{"
"const xhr=new XMLHttpRequest();"
"xhr.open('POST','/api/thumb?id='+id);"
"xhr.timeout=30000;"
"xhr.onload=()=>xhr.status>=200&&xhr.status<300?resolve():reject(new Error('Thumbnail upload failed'));"
"xhr.onerror=()=>reject(new Error('Thumbnail upload failed'));"
"xhr.ontimeout=()=>reject(new Error('Thumbnail upload timed out'));"
"xhr.send(blob);"
"});"
"}"

"function canvasBlob(canvas,q){"
"return new Promise(resolve=>canvas.toBlob(resolve,'image/jpeg',q));"
"}"

"async function makeThumbnail(file){"
"const url=URL.createObjectURL(file);"
"try{"
"const img=new Image();"
"await new Promise((resolve,reject)=>{img.onload=resolve;img.onerror=reject;img.src=url;});"
"let w=img.naturalWidth,h=img.naturalHeight;"
"const maxDim=320;"
"const scale=Math.min(1,maxDim/Math.max(w,h));"
"w=Math.max(1,Math.round(w*scale));"
"h=Math.max(1,Math.round(h*scale));"
"const canvas=document.createElement('canvas');"
"const ctx=canvas.getContext('2d',{alpha:false});"
"for(let pass=0;pass<8;pass++){"
"canvas.width=w;canvas.height=h;"
"ctx.fillStyle='#111';ctx.fillRect(0,0,w,h);"
"ctx.drawImage(img,0,0,w,h);"
"for(let q=.82;q>=.30;q-=.08){"
"const blob=await canvasBlob(canvas,q);"
"if(blob&&blob.size<=30720)return blob;"
"}"
"w=Math.max(80,Math.round(w*.82));"
"h=Math.max(80,Math.round(h*.82));"
"}"
"return await canvasBlob(canvas,.25);"
"}finally{URL.revokeObjectURL(url);}"
"}"

"async function api(url,options){"
"const r=await fetch(url,{cache:'no-store',...(options||{})});"
"if(!r.ok)throw new Error(await r.text());"
"return r;"
"}"

"async function loadAll(){"
"const data=await(await api('/api/list')).json();"
"$('storage').textContent=fmt(data.used)+' / '+fmt(data.total)+' used';"
"renderFiles(data.files);"
"renderNotes(data.notes);"
"}"

"function renderFiles(files){"
"files=[...files].sort((a,b)=>b.id-a.id);"
"const gallery=$('gallery');"
"const list=$('fileList');"
"gallery.replaceChildren();"
"list.replaceChildren();"
"for(const f of files){"
"if(imageName(f.name))renderImage(f);"
"else renderFile(f);"
"}"
"}"

"function renderImage(f){"
"const card=document.createElement('div');"
"card.className='image-card';"

"const img=document.createElement('img');"
"img.loading='lazy';"
"img.src='/api/thumb?id='+f.id;"
"img.alt=f.name;"
"img.onclick=()=>{"
"$('imageView').src='/api/view?id='+f.id;"
"$('imageOverlay').classList.add('active');"
"};"

"const actions=document.createElement('div');"
"actions.className='image-actions';"

"const download=document.createElement('button');"
"download.textContent='Download';"
"download.onclick=()=>location.href='/api/download?id='+f.id;"

"const del=document.createElement('button');"
"del.textContent='Delete';"
"del.onclick=async()=>{"
"await api('/api/file/delete?id='+f.id,{method:'POST'});"
"await loadAll();"
"};"

"actions.append(download,del);"
"card.append(img,actions);"
"$('gallery').append(card);"
"}"

"function renderFile(f){"
"const row=document.createElement('div');"
"row.className='file';"

"const badge=document.createElement('div');"
"badge.className='ext';"
"let e=ext(f.name);"
"badge.textContent=e&&e.length<=5?e:'file';"

"const info=document.createElement('div');"
"info.className='file-info';"

"const name=document.createElement('div');"
"name.className='filename';"
"name.textContent=f.name;"

"const size=document.createElement('div');"
"size.className='filesize';"
"size.textContent=fmt(f.size);"

"info.append(name,size);"

"const actions=document.createElement('div');"
"actions.className='file-actions';"

"const download=document.createElement('button');"
"download.textContent='Download';"
"download.onclick=()=>location.href='/api/download?id='+f.id;"

"const del=document.createElement('button');"
"del.textContent='Delete';"
"del.onclick=async()=>{"
"await api('/api/file/delete?id='+f.id,{method:'POST'});"
"await loadAll();"
"};"

"actions.append(download,del);"
"row.append(badge,info,actions);"
"$('fileList').append(row);"
"}"

"let currentNote=null;"

"function renderNotes(notes){"
"notes=[...notes].sort((a,b)=>b.id-a.id);"
"const box=$('notes');"
"box.replaceChildren();"

"for(const n of notes){"
"const card=document.createElement('div');"
"card.className='note';"

"const text=document.createElement('div');"
"text.className='note-text';"
"text.textContent=n.text;"

"const actions=document.createElement('div');"
"actions.className='note-actions';"

"const copy=document.createElement('button');"
"copy.textContent='Copy';"
"copy.onclick=async e=>{"
"e.stopPropagation();"
"await navigator.clipboard.writeText(n.text);"
"copy.textContent='Copied';"
"setTimeout(()=>copy.textContent='Copy',800);"
"};"

"const del=document.createElement('button');"
"del.textContent='Delete';"
"del.onclick=async e=>{"
"e.stopPropagation();"
"await api('/api/note/delete?id='+n.id,{method:'POST'});"
"await loadAll();"
"};"

"card.onclick=e=>{"
"if(e.target.closest('button'))return;"
"currentNote=n.id;"
"$('editorText').value=n.text;"
"$('editorOverlay').classList.add('active');"
"$('editorText').focus();"
"};"

"actions.append(copy,del);"
"card.append(text,actions);"
"box.append(card);"
"}"
"}"

"$('saveNote').onclick=async()=>{"
"const text=$('noteInput').value;"
"if(!text.trim())return;"
"await api('/api/note',{method:'POST',body:text});"
"$('noteInput').value='';"
"$('noteInput').style.height='auto';"
"await loadAll();"
"};"

"$('editorSave').onclick=async()=>{"
"if(currentNote===null)return;"
"await api('/api/note?id='+currentNote,{method:'POST',body:$('editorText').value});"
"$('editorOverlay').classList.remove('active');"
"currentNote=null;"
"await loadAll();"
"};"

"$('editorCancel').onclick=()=>{"
"$('editorOverlay').classList.remove('active');"
"currentNote=null;"
"};"

"$('editorOverlay').onclick=e=>{"
"if(e.target===$('editorOverlay'))$('editorCancel').click();"
"};"

"$('imageOverlay').onclick=()=>{"
"$('imageOverlay').classList.remove('active');"
"$('imageView').src='';"
"};"

"document.addEventListener('keydown',e=>{"
"if(e.key==='Escape'){"
"$('editorOverlay').classList.remove('active');"
"$('imageOverlay').classList.remove('active');"
"}"
"});"

"loadAll();"

"</script>"
"</body>"
"</html>";


static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}


static bool url_decode(char *dst, size_t cap, const char *src)
{
    size_t out = 0;

    while (*src) {

        if (out + 1 >= cap)
            return false;

        if (*src == '%' && src[1] && src[2]) {

            int a = hex_value(src[1]);
            int b = hex_value(src[2]);

            if (a < 0 || b < 0)
                return false;

            dst[out++] = (char)((a << 4) | b);

            src += 3;
        }
        else if (*src == '+') {

            dst[out++] = ' ';
            src++;
        }
        else {

            dst[out++] = *src++;
        }
    }

    dst[out] = 0;

    return true;
}


static bool get_query_value(
    httpd_req_t *req,
    const char *key,
    char *out,
    size_t out_size)
{
    char query[1024];
    char value[800];

    if (httpd_req_get_url_query_str(
            req,
            query,
            sizeof(query)) != ESP_OK)
        return false;

    if (httpd_query_key_value(
            query,
            key,
            value,
            sizeof(value)) != ESP_OK)
        return false;

    return url_decode(
        out,
        out_size,
        value
    );
}


static bool get_query_u32(
    httpd_req_t *req,
    const char *key,
    uint32_t *value)
{
    char text[32];

    if (!get_query_value(
            req,
            key,
            text,
            sizeof(text)))
        return false;

    char *end = NULL;

    unsigned long v =
        strtoul(text, &end, 10);

    if (!end || *end != 0)
        return false;

    *value = (uint32_t)v;

    return true;
}


static void hex_encode(
    const char *src,
    char *dst,
    size_t cap)
{
    static const char h[] =
        "0123456789ABCDEF";

    size_t j = 0;

    while (*src && j + 2 < cap) {

        unsigned char c =
            (unsigned char)*src++;

        dst[j++] = h[c >> 4];
        dst[j++] = h[c & 15];
    }

    dst[j] = 0;
}


static bool hex_decode(
    const char *src,
    char *dst,
    size_t cap)
{
    size_t j = 0;

    while (src[0] && src[1]) {

        if (j + 1 >= cap)
            return false;

        int a = hex_value(src[0]);
        int b = hex_value(src[1]);

        if (a < 0 || b < 0)
            return false;

        dst[j++] =
            (char)((a << 4) | b);

        src += 2;
    }

    dst[j] = 0;

    return true;
}


static void file_path(
    uint32_t id,
    char *path,
    size_t size)
{
    snprintf(
        path,
        size,
        FILES_DIR "/f%u.bin",
        (unsigned)id
    );
}


static void thumb_path(
    uint32_t id,
    char *path,
    size_t size)
{
    snprintf(
        path,
        size,
        THUMBS_DIR "/t%u.jpg",
        (unsigned)id
    );
}


static void note_path(
    uint32_t id,
    char *path,
    size_t size)
{
    snprintf(
        path,
        size,
        NOTES_DIR "/n%u.txt",
        (unsigned)id
    );
}


static bool append_meta(
    uint32_t id,
    uint64_t size,
    const char *name)
{
    char encoded[600];

    hex_encode(
        name,
        encoded,
        sizeof(encoded)
    );

    FILE *f =
        fopen(META_FILE, "a");

    if (!f)
        return false;

    int ok =
        fprintf(
            f,
            "%u %llu %s\n",
            (unsigned)id,
            (unsigned long long)size,
            encoded
        );

    fclose(f);

    return ok > 0;
}


static bool find_meta(
    uint32_t wanted,
    char *name,
    size_t name_size,
    uint64_t *size)
{
    FILE *f =
        fopen(META_FILE, "r");

    if (!f)
        return false;

    char line[800];

    while (fgets(
        line,
        sizeof(line),
        f)) {

        unsigned id;
        unsigned long long sz;
        char encoded[600];

        if (sscanf(
                line,
                "%u %llu %599s",
                &id,
                &sz,
                encoded) != 3)
            continue;

        if (id != wanted)
            continue;

        fclose(f);

        if (!hex_decode(
                encoded,
                name,
                name_size))
            return false;

        if (size)
            *size = (uint64_t)sz;

        return true;
    }

    fclose(f);

    return false;
}


static bool delete_meta(
    uint32_t wanted)
{
    FILE *src =
        fopen(META_FILE, "r");

    if (!src)
        return false;

    FILE *dst =
        fopen(META_TEMP, "w");

    if (!dst) {
        fclose(src);
        return false;
    }

    char line[800];

    while (fgets(
        line,
        sizeof(line),
        src)) {

        unsigned id = 0;

        if (sscanf(
                line,
                "%u",
                &id) == 1 &&
            id == wanted)
            continue;

        fputs(line, dst);
    }

    fclose(src);
    fclose(dst);

    remove(META_FILE);

    return rename(
        META_TEMP,
        META_FILE
    ) == 0;
}


static esp_err_t send_json_string(
    httpd_req_t *req,
    const char *text)
{
    char out[512];
    size_t pos = 0;

    #define FLUSH() \
        do { \
            if (pos) { \
                if (httpd_resp_send_chunk(req,out,pos)!=ESP_OK) \
                    return ESP_FAIL; \
                pos=0; \
            } \
        } while (0)

    out[pos++] = '"';

    while (*text) {

        unsigned char c =
            (unsigned char)*text++;

        char esc[7];
        const char *p = NULL;

        if (c == '"')
            p = "\\\"";
        else if (c == '\\')
            p = "\\\\";
        else if (c == '\n')
            p = "\\n";
        else if (c == '\r')
            p = "\\r";
        else if (c == '\t')
            p = "\\t";
        else if (c < 32) {

            snprintf(
                esc,
                sizeof(esc),
                "\\u%04x",
                c
            );

            p = esc;
        }

        if (p) {

            size_t n =
                strlen(p);

            if (pos + n >= sizeof(out))
                FLUSH();

            memcpy(
                out + pos,
                p,
                n
            );

            pos += n;
        }
        else {

            if (pos + 1 >= sizeof(out))
                FLUSH();

            out[pos++] = (char)c;
        }
    }

    if (pos + 1 >= sizeof(out))
        FLUSH();

    out[pos++] = '"';

    FLUSH();

    #undef FLUSH

    return ESP_OK;
}


static esp_err_t send_json_file(
    httpd_req_t *req,
    FILE *f)
{
    char out[512];
    size_t pos = 0;

    #define FLUSH_FILE() \
        do { \
            if (pos) { \
                if (httpd_resp_send_chunk(req,out,pos)!=ESP_OK) \
                    return ESP_FAIL; \
                pos=0; \
            } \
        } while (0)

    out[pos++] = '"';

    int ch;

    while ((ch = fgetc(f)) != EOF) {

        unsigned char c =
            (unsigned char)ch;

        char esc[7];
        const char *p = NULL;

        if (c == '"')
            p = "\\\"";
        else if (c == '\\')
            p = "\\\\";
        else if (c == '\n')
            p = "\\n";
        else if (c == '\r')
            p = "\\r";
        else if (c == '\t')
            p = "\\t";
        else if (c < 32) {

            snprintf(
                esc,
                sizeof(esc),
                "\\u%04x",
                c
            );

            p = esc;
        }

        if (p) {

            size_t n =
                strlen(p);

            if (pos + n >= sizeof(out))
                FLUSH_FILE();

            memcpy(
                out + pos,
                p,
                n
            );

            pos += n;
        }
        else {

            if (pos + 1 >= sizeof(out))
                FLUSH_FILE();

            out[pos++] = (char)c;
        }
    }

    if (pos + 1 >= sizeof(out))
        FLUSH_FILE();

    out[pos++] = '"';

    FLUSH_FILE();

    #undef FLUSH_FILE

    return ESP_OK;
}


static const char *mime_type(
    const char *name)
{
    const char *dot =
        strrchr(name, '.');

    if (!dot)
        return "application/octet-stream";

    dot++;

    if (!strcasecmp(dot, "png"))
        return "image/png";

    if (!strcasecmp(dot, "jpg") ||
        !strcasecmp(dot, "jpeg"))
        return "image/jpeg";

    if (!strcasecmp(dot, "webp"))
        return "image/webp";

    if (!strcasecmp(dot, "txt"))
        return "text/plain";

    if (!strcasecmp(dot, "pdf"))
        return "application/pdf";

    if (!strcasecmp(dot, "mp3"))
        return "audio/mpeg";

    if (!strcasecmp(dot, "mp4"))
        return "video/mp4";

    return "application/octet-stream";
}


static esp_err_t root_handler(
    httpd_req_t *req)
{
    httpd_resp_set_type(
        req,
        "text/html"
    );

    return httpd_resp_send(
        req,
        INDEX_HTML,
        HTTPD_RESP_USE_STRLEN
    );
}


static esp_err_t list_handler(
    httpd_req_t *req)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;

    esp_vfs_fat_info(
        STORAGE,
        &total,
        &free_bytes
    );

    uint64_t used =
        total - free_bytes;

    httpd_resp_set_type(
        req,
        "application/json"
    );

    char head[128];

    snprintf(
        head,
        sizeof(head),
        "{\"used\":%llu,\"total\":%llu,\"files\":[",
        (unsigned long long)used,
        (unsigned long long)total
    );

    httpd_resp_send_chunk(
        req,
        head,
        HTTPD_RESP_USE_STRLEN
    );

    FILE *meta =
        fopen(META_FILE, "r");

    bool first = true;

    if (meta) {

        char line[800];

        while (fgets(
            line,
            sizeof(line),
            meta)) {

            unsigned id;
            unsigned long long size;
            char encoded[600];
            char name[300];

            if (sscanf(
                    line,
                    "%u %llu %599s",
                    &id,
                    &size,
                    encoded) != 3)
                continue;

            if (!hex_decode(
                    encoded,
                    name,
                    sizeof(name)))
                continue;

            if (!first)
                httpd_resp_send_chunk(
                    req,
                    ",",
                    1
                );

            first = false;

            char start[96];

            snprintf(
                start,
                sizeof(start),
                "{\"id\":%u,\"name\":",
                id
            );

            httpd_resp_send_chunk(
                req,
                start,
                HTTPD_RESP_USE_STRLEN
            );

            send_json_string(
                req,
                name
            );

            char end[96];

            snprintf(
                end,
                sizeof(end),
                ",\"size\":%llu}",
                size
            );

            httpd_resp_send_chunk(
                req,
                end,
                HTTPD_RESP_USE_STRLEN
            );
        }

        fclose(meta);
    }

    httpd_resp_send_chunk(
        req,
        "],\"notes\":[",
        HTTPD_RESP_USE_STRLEN
    );

    DIR *dir =
        opendir(NOTES_DIR);

    first = true;

    if (dir) {

        struct dirent *entry;

        while ((entry =
                readdir(dir)) != NULL) {

            unsigned id;

            if (sscanf(
                    entry->d_name,
                    "n%u.txt",
                    &id) != 1)
                continue;

            char path[128];

            note_path(
                id,
                path,
                sizeof(path)
            );

            FILE *f =
                fopen(path, "r");

            if (!f)
                continue;

            if (!first)
                httpd_resp_send_chunk(
                    req,
                    ",",
                    1
                );

            first = false;

            char start[64];

            snprintf(
                start,
                sizeof(start),
                "{\"id\":%u,\"text\":",
                id
            );

            httpd_resp_send_chunk(
                req,
                start,
                HTTPD_RESP_USE_STRLEN
            );

            send_json_file(
                req,
                f
            );

            fclose(f);

            httpd_resp_send_chunk(
                req,
                "}",
                1
            );
        }

        closedir(dir);
    }

    httpd_resp_send_chunk(
        req,
        "]}",
        2
    );

    httpd_resp_send_chunk(
        req,
        NULL,
        0
    );

    return ESP_OK;
}


static esp_err_t serve_file(httpd_req_t *req, bool download);


static esp_err_t upload_handler(
    httpd_req_t *req)
{
    char filename[300];

    if (!get_query_value(
            req,
            "name",
            filename,
            sizeof(filename))) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Missing filename"
        );

        return ESP_FAIL;
    }

    uint32_t id = next_file_id++;

    char path[128];

    file_path(
        id,
        path,
        sizeof(path)
    );

    FILE *f = fopen(path, "wb");

    if (!f) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Cannot create file"
        );

        return ESP_FAIL;
    }

    const size_t buffer_size = 32768;
    char *buffer = malloc(buffer_size);

    if (!buffer) {
        fclose(f);
        remove(path);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Out of memory"
        );

        return ESP_FAIL;
    }

    size_t remaining = req->content_len;
    size_t total = 0;
    size_t buffered = 0;
    int timeout_count = 0;

    while (remaining > 0) {
        size_t room = buffer_size - buffered;
        size_t wanted = remaining < room ? remaining : room;

        int got = httpd_req_recv(
            req,
            buffer + buffered,
            wanted
        );

        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            timeout_count++;

            if (timeout_count >= 12) {
                free(buffer);
                fclose(f);
                remove(path);

                httpd_resp_send_err(
                    req,
                    HTTPD_408_REQ_TIMEOUT,
                    "Upload timed out"
                );

                return ESP_FAIL;
            }

            continue;
        }

        if (got <= 0) {
            free(buffer);
            fclose(f);
            remove(path);
            return ESP_FAIL;
        }

        timeout_count = 0;
        buffered += (size_t)got;
        remaining -= (size_t)got;

        if (buffered == buffer_size || remaining == 0) {
            if (fwrite(buffer, 1, buffered, f) != buffered) {
                free(buffer);
                fclose(f);
                remove(path);

                httpd_resp_send_err(
                    req,
                    HTTPD_500_INTERNAL_SERVER_ERROR,
                    "Storage write failed"
                );

                return ESP_FAIL;
            }

            total += buffered;
            buffered = 0;
        }
    }

    fflush(f);
    fclose(f);
    free(buffer);

    if (!append_meta(
            id,
            total,
            filename)) {

        remove(path);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Metadata write failed"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Uploaded %s (%u bytes)",
        filename,
        (unsigned)total
    );

    char response[64];
    snprintf(response, sizeof(response), "{\"id\":%u}", (unsigned)id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);

    return ESP_OK;
}


static esp_err_t thumbnail_upload_handler(httpd_req_t *req)
{
    uint32_t id;
    if (!get_query_u32(req, "id", &id)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id");
        return ESP_FAIL;
    }

    if (req->content_len > 30720) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Thumbnail too large");
        return ESP_FAIL;
    }

    char path[128];
    thumb_path(id, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot save thumbnail");
        return ESP_FAIL;
    }

    char buffer[2048];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        int got = httpd_req_recv(req, buffer, wanted);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0 || fwrite(buffer, 1, (size_t)got, f) != (size_t)got) {
            fclose(f);
            remove(path);
            return ESP_FAIL;
        }
        remaining -= (size_t)got;
    }

    fclose(f);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}


static esp_err_t thumbnail_handler(httpd_req_t *req)
{
    uint32_t id;
    if (!get_query_u32(req, "id", &id)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id");
        return ESP_FAIL;
    }

    char path[128];
    thumb_path(id, path, sizeof(path));
    FILE *f = fopen(path, "rb");

    if (!f)
        return serve_file(req, false);

    httpd_resp_set_type(req, "image/jpeg");

    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t serve_file(
    httpd_req_t *req,
    bool download)
{
    uint32_t id;

    if (!get_query_u32(
            req,
            "id",
            &id)) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Missing id"
        );

        return ESP_FAIL;
    }

    char name[300];
    uint64_t size;

    if (!find_meta(
            id,
            name,
            sizeof(name),
            &size)) {

        httpd_resp_send_err(
            req,
            HTTPD_404_NOT_FOUND,
            "File not found"
        );

        return ESP_FAIL;
    }

    char path[128];

    file_path(
        id,
        path,
        sizeof(path)
    );

    FILE *f =
        fopen(path, "rb");

    if (!f) {

        httpd_resp_send_err(
            req,
            HTTPD_404_NOT_FOUND,
            "File not found"
        );

        return ESP_FAIL;
    }

    httpd_resp_set_type(
        req,
        mime_type(name)
    );

    if (download) {

        char header[400];

        snprintf(
            header,
            sizeof(header),
            "attachment; filename=\"download\""
        );

        httpd_resp_set_hdr(
            req,
            "Content-Disposition",
            header
        );
    }

    const size_t buffer_size = 32768;
    char *buffer = malloc(buffer_size);

    if (!buffer) {
        fclose(f);
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Out of memory"
        );
        return ESP_FAIL;
    }

    size_t n;

    while ((n = fread(
                buffer,
                1,
                buffer_size,
                f)) > 0) {

        if (httpd_resp_send_chunk(
                req,
                buffer,
                n) != ESP_OK) {

            free(buffer);
            fclose(f);
            return ESP_FAIL;
        }
    }

    free(buffer);
    fclose(f);

    httpd_resp_send_chunk(
        req,
        NULL,
        0
    );

    return ESP_OK;
}


static esp_err_t view_handler(
    httpd_req_t *req)
{
    return serve_file(
        req,
        false
    );
}


static esp_err_t download_handler(
    httpd_req_t *req)
{
    return serve_file(
        req,
        true
    );
}


static esp_err_t delete_file_handler(
    httpd_req_t *req)
{
    uint32_t id;

    if (!get_query_u32(
            req,
            "id",
            &id)) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Missing id"
        );

        return ESP_FAIL;
    }

    char path[128];

    file_path(
        id,
        path,
        sizeof(path)
    );

    remove(path);

    char thumb[128];
    thumb_path(id, thumb, sizeof(thumb));
    remove(thumb);

    delete_meta(id);

    httpd_resp_sendstr(
        req,
        "OK"
    );

    return ESP_OK;
}


static esp_err_t note_handler(
    httpd_req_t *req)
{
    uint32_t id;

    if (!get_query_u32(
            req,
            "id",
            &id)) {

        id =
            next_note_id++;
    }

    char path[128];

    note_path(
        id,
        path,
        sizeof(path)
    );

    FILE *f =
        fopen(path, "wb");

    if (!f) {

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Cannot save note"
        );

        return ESP_FAIL;
    }

    char buffer[4096];

    size_t remaining =
        req->content_len;

    while (remaining > 0) {

        size_t wanted =
            remaining > sizeof(buffer)
            ? sizeof(buffer)
            : remaining;

        int got =
            httpd_req_recv(
                req,
                buffer,
                wanted
            );

        if (got ==
            HTTPD_SOCK_ERR_TIMEOUT)
            continue;

        if (got <= 0) {

            fclose(f);
            return ESP_FAIL;
        }

        if (fwrite(
                buffer,
                1,
                got,
                f) != (size_t)got) {

            fclose(f);

            httpd_resp_send_err(
                req,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                "Storage full"
            );

            return ESP_FAIL;
        }

        remaining -= got;
    }

    fclose(f);

    char result[64];

    snprintf(
        result,
        sizeof(result),
        "{\"id\":%u}",
        (unsigned)id
    );

    httpd_resp_set_type(
        req,
        "application/json"
    );

    httpd_resp_sendstr(
        req,
        result
    );

    return ESP_OK;
}


static esp_err_t delete_note_handler(
    httpd_req_t *req)
{
    uint32_t id;

    if (!get_query_u32(
            req,
            "id",
            &id)) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Missing id"
        );

        return ESP_FAIL;
    }

    char path[128];

    note_path(
        id,
        path,
        sizeof(path)
    );

    remove(path);

    httpd_resp_sendstr(
        req,
        "OK"
    );

    return ESP_OK;
}


static void start_server(void)
{
    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.stack_size = 12288;
    config.max_uri_handlers = 12;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;

    httpd_handle_t server =
        NULL;

    ESP_ERROR_CHECK(
        httpd_start(
            &server,
            &config
        )
    );

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler
    };

    httpd_uri_t list = {
        .uri = "/api/list",
        .method = HTTP_GET,
        .handler = list_handler
    };

    httpd_uri_t upload = {
        .uri = "/api/upload",
        .method = HTTP_POST,
        .handler = upload_handler
    };

    httpd_uri_t thumb_upload = {
        .uri = "/api/thumb",
        .method = HTTP_POST,
        .handler = thumbnail_upload_handler
    };

    httpd_uri_t thumb_view = {
        .uri = "/api/thumb",
        .method = HTTP_GET,
        .handler = thumbnail_handler
    };

    httpd_uri_t view = {
        .uri = "/api/view",
        .method = HTTP_GET,
        .handler = view_handler
    };

    httpd_uri_t download = {
        .uri = "/api/download",
        .method = HTTP_GET,
        .handler = download_handler
    };

    httpd_uri_t delete_file = {
        .uri = "/api/file/delete",
        .method = HTTP_POST,
        .handler = delete_file_handler
    };

    httpd_uri_t note = {
        .uri = "/api/note",
        .method = HTTP_POST,
        .handler = note_handler
    };

    httpd_uri_t delete_note = {
        .uri = "/api/note/delete",
        .method = HTTP_POST,
        .handler = delete_note_handler
    };

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &root
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &list
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &upload
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &thumb_upload
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &thumb_view
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &view
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &download
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &delete_file
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &note
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &delete_note
        )
    );
}


static void find_next_ids(void)
{
    FILE *f =
        fopen(META_FILE, "r");

    if (f) {

        char line[800];

        while (fgets(
            line,
            sizeof(line),
            f)) {

            unsigned id;

            if (sscanf(
                    line,
                    "%u",
                    &id) == 1 &&
                id >= next_file_id) {

                next_file_id =
                    id + 1;
            }
        }

        fclose(f);
    }

    DIR *dir =
        opendir(NOTES_DIR);

    if (dir) {

        struct dirent *entry;

        while ((entry =
                readdir(dir)) != NULL) {

            unsigned id;

            if (sscanf(
                    entry->d_name,
                    "n%u.txt",
                    &id) == 1 &&
                id >= next_note_id) {

                next_note_id =
                    id + 1;
            }
        }

        closedir(dir);
    }
}


static void storage_init(void)
{
    esp_vfs_fat_mount_config_t config = {
        .format_if_mount_failed = true,
        .max_files = 20,
        .allocation_unit_size = 4096
    };

    ESP_ERROR_CHECK(
        esp_vfs_fat_spiflash_mount_rw_wl(
            STORAGE,
            "storage",
            &config,
            &wl_handle
        )
    );

    mkdir(
        FILES_DIR,
        0777
    );

    mkdir(
        NOTES_DIR,
        0777
    );

    mkdir(
        THUMBS_DIR,
        0777
    );

    find_next_ids();

    uint64_t total = 0;
    uint64_t free_bytes = 0;

    ESP_ERROR_CHECK(
        esp_vfs_fat_info(
            STORAGE,
            &total,
            &free_bytes
        )
    );

    ESP_LOGI(
        TAG,
        "Storage: %llu used / %llu total",
        (unsigned long long)(total - free_bytes),
        (unsigned long long)total
    );
}


static void wifi_event(
    void *arg,
    esp_event_base_t base,
    int32_t id,
    void *data)
{
    if (base == WIFI_EVENT &&
        id == WIFI_EVENT_STA_START) {

        esp_wifi_connect();
    }

    else if (
        base == WIFI_EVENT &&
        id == WIFI_EVENT_STA_DISCONNECTED) {

        xEventGroupClearBits(
            wifi_events,
            WIFI_CONNECTED
        );

        esp_wifi_connect();
    }

    else if (
        base == IP_EVENT &&
        id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)data;

        ESP_LOGI(
            TAG,
            "Open: http://" IPSTR,
            IP2STR(
                &event->ip_info.ip
            )
        );

        xEventGroupSetBits(
            wifi_events,
            WIFI_CONNECTED
        );
    }
}


static void wifi_init(void)
{
    wifi_events =
        xEventGroupCreate();

    ESP_ERROR_CHECK(
        esp_netif_init()
    );

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(
            &init
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event,
            NULL
        )
    );

    wifi_config_t config = {0};

    strncpy(
        (char *)config.sta.ssid,
        WIFI_SSID,
        sizeof(config.sta.ssid) - 1
    );

    strncpy(
        (char *)config.sta.password,
        WIFI_PASS,
        sizeof(config.sta.password) - 1
    );

    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(
            WIFI_MODE_STA
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_ps(WIFI_PS_NONE)
    );

    xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );
}


void app_main(void)
{
    esp_err_t err =
        nvs_flash_init();

    if (err ==
            ESP_ERR_NVS_NO_FREE_PAGES ||
        err ==
            ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        err =
            nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    storage_init();

    wifi_init();

    start_server();

    ESP_LOGI(
        TAG,
        "ESPDrop running"
    );
}
