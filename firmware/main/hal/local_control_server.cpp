/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "local_control_server.h"
#include "hal.h"
#include <stackchan/stackchan.h>
#include <stackchan/avatar/avatar/elements/emotion.h>
#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <cstring>
#include <string>
#include <algorithm>

using stackchan::avatar::Emotion;

namespace {

constexpr const char* TAG = "LocalControl";
constexpr int kServerPort = 80;
httpd_handle_t g_server = nullptr;

constexpr const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StackChan Local</title>
<style>
:root{color-scheme:dark;--bg:#101314;--panel:#191f21;--line:#2a3437;--text:#edf4f2;--muted:#9fb0ad;--accent:#64d6a7;--danger:#ff7a7a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
main{width:min(760px,100%);margin:0 auto;padding:18px}.top{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px}
h1{font-size:22px;margin:0}.status{color:var(--muted);font-size:13px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
section{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}h2{font-size:15px;margin:0 0 12px}
label{display:grid;gap:6px;margin:10px 0;color:var(--muted)}input,select,button{font:inherit}
input[type=range]{width:100%}input[type=text],input[type=number],select{width:100%;background:#0d1112;color:var(--text);border:1px solid var(--line);border-radius:6px;padding:9px}
.row{display:grid;grid-template-columns:1fr 74px;gap:10px;align-items:center}.buttons{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
button{border:0;border-radius:6px;background:var(--accent);color:#062018;font-weight:700;padding:9px 12px;cursor:pointer}button.secondary{background:#303a3d;color:var(--text)}
button.danger{background:var(--danger);color:#2a0808}.chip{display:inline-flex;padding:4px 8px;border:1px solid var(--line);border-radius:999px;color:var(--muted)}
@media(max-width:640px){.grid{grid-template-columns:1fr}.top{align-items:flex-start;flex-direction:column}}
</style>
</head>
<body>
<main>
<div class="top"><h1>StackChan Local</h1><div class="status"><span id="ws" class="chip">local</span></div></div>
<div class="grid">
<section>
<h2>Head</h2>
<label>Yaw <div class="row"><input id="yaw" type="range" min="-128" max="128" value="0"><input id="yawNum" type="number" min="-128" max="128" value="0"></div></label>
<label>Pitch <div class="row"><input id="pitch" type="range" min="0" max="90" value="0"><input id="pitchNum" type="number" min="0" max="90" value="0"></div></label>
<label>Speed <div class="row"><input id="speed" type="range" min="100" max="1000" value="300"><input id="speedNum" type="number" min="100" max="1000" value="300"></div></label>
<div class="buttons"><button onclick="sendHead()">Move</button><button class="secondary" onclick="home()">Home</button></div>
</section>
<section>
<h2>Light</h2>
<label>Color <input id="color" type="color" value="#40d090"></label>
<div class="buttons"><button onclick="sendColor()">Set Light</button><button class="secondary" onclick="off()">Off</button></div>
</section>
<section>
<h2>Face</h2>
<label>Emotion <select id="emotion"><option>neutral</option><option>happy</option><option>angry</option><option>sad</option><option>doubt</option><option>sleepy</option></select></label>
<label>Speech <input id="speech" type="text" maxlength="80" placeholder="Hello"></label>
<div class="buttons"><button onclick="sendFace()">Send</button><button class="secondary" onclick="clearSpeech()">Clear</button></div>
</section>
<section>
<h2>System</h2>
<p id="info" class="status">Loading...</p>
<div class="buttons"><button class="secondary" onclick="refresh()">Refresh</button><button class="danger" onclick="reboot()">Reboot</button></div>
</section>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
function bindRange(a,b){$(a).addEventListener('input',()=>$(b).value=$(a).value);$(b).addEventListener('input',()=>$(a).value=$(b).value)}
bindRange('yaw','yawNum');bindRange('pitch','pitchNum');bindRange('speed','speedNum');
function send(o){return fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).then(r=>r.json()).then(update)}
function sendHead(){send({yaw:+$('yaw').value,pitch:+$('pitch').value,speed:+$('speed').value})}
function home(){send({home:true,speed:+$('speed').value});$('yaw').value=$('yawNum').value=0;$('pitch').value=$('pitchNum').value=0}
function sendColor(){const c=$('color').value;send({r:parseInt(c.slice(1,3),16),g:parseInt(c.slice(3,5),16),b:parseInt(c.slice(5,7),16)})}
function off(){send({r:0,g:0,b:0})}
function sendFace(){send({emotion:$('emotion').value,speech:$('speech').value})}
function clearSpeech(){send({speech:''});$('speech').value=''}
function reboot(){if(confirm('Reboot StackChan?'))send({reboot:true})}
function refresh(){fetch('/api/status').then(r=>r.json()).then(update)}
function update(s){if(s.yaw!==undefined){$('yaw').value=$('yawNum').value=s.yaw;$('pitch').value=$('pitchNum').value=s.pitch}$('info').textContent=`IP: ${s.ip||location.hostname}  Battery: ${s.battery}%  Charging: ${s.charging?'yes':'no'}`}
refresh();
</script>
</body>
</html>)HTML";

int get_json_int(cJSON* root, const char* key, int fallback)
{
    cJSON* item = cJSON_GetObjectItem(root, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool get_json_bool(cJSON* root, const char* key, bool fallback)
{
    cJSON* item = cJSON_GetObjectItem(root, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

std::string get_ip_address()
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif == nullptr || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        return "";
    }
    char buffer[16];
    snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&ip_info.ip));
    return buffer;
}

std::string json_to_string(cJSON* json)
{
    char* printed = cJSON_PrintUnformatted(json);
    if (printed == nullptr) {
        return "{}";
    }
    std::string result = printed;
    cJSON_free(printed);
    return result;
}

const char* emotion_to_string(Emotion emotion)
{
    switch (emotion) {
        case Emotion::Happy:
            return "happy";
        case Emotion::Angry:
            return "angry";
        case Emotion::Sad:
            return "sad";
        case Emotion::Doubt:
            return "doubt";
        case Emotion::Sleepy:
            return "sleepy";
        case Emotion::Neutral:
        default:
            return "neutral";
    }
}

Emotion parse_emotion(const char* value)
{
    if (value == nullptr) {
        return Emotion::Neutral;
    }
    if (strcmp(value, "happy") == 0) {
        return Emotion::Happy;
    }
    if (strcmp(value, "angry") == 0) {
        return Emotion::Angry;
    }
    if (strcmp(value, "sad") == 0) {
        return Emotion::Sad;
    }
    if (strcmp(value, "doubt") == 0) {
        return Emotion::Doubt;
    }
    if (strcmp(value, "sleepy") == 0) {
        return Emotion::Sleepy;
    }
    return Emotion::Neutral;
}

std::string make_status_json()
{
    LvglLockGuard lock;
    auto& stackchan = GetStackChan();
    auto& motion = stackchan.motion();
    int yaw = motion.getCurrentYawAngle() / 10;
    int pitch = motion.getCurrentPitchAngle() / 10;
    const char* emotion = stackchan.hasAvatar() ? emotion_to_string(stackchan.avatar().getEmotion()) : "none";

    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddStringToObject(json, "ip", get_ip_address().c_str());
    cJSON_AddStringToObject(json, "url", local_control::get_url().c_str());
    cJSON_AddNumberToObject(json, "yaw", yaw);
    cJSON_AddNumberToObject(json, "pitch", pitch);
    cJSON_AddNumberToObject(json, "battery", GetHAL().getBatteryLevel());
    cJSON_AddBoolToObject(json, "charging", GetHAL().isBatteryCharging());
    cJSON_AddStringToObject(json, "emotion", emotion);
    std::string status = json_to_string(json);
    cJSON_Delete(json);
    return status;
}

bool apply_control_json(cJSON* root, std::string& error)
{
    if (root == nullptr || !cJSON_IsObject(root)) {
        error = "invalid json";
        return false;
    }

    bool should_reboot = get_json_bool(root, "reboot", false);

    {
        LvglLockGuard lock;
        auto& stackchan = GetStackChan();
        auto& motion = stackchan.motion();

        int speed = std::clamp(get_json_int(root, "speed", 300), 100, 1000);
        if (get_json_bool(root, "home", false)) {
            motion.goHome(speed);
        } else {
            cJSON* yaw_item = cJSON_GetObjectItem(root, "yaw");
            cJSON* pitch_item = cJSON_GetObjectItem(root, "pitch");
            if (cJSON_IsNumber(yaw_item)) {
                int yaw = std::clamp(yaw_item->valueint, -128, 128);
                motion.yawServo().moveWithSpeed(yaw * 10, speed);
            }
            if (cJSON_IsNumber(pitch_item)) {
                int pitch = std::clamp(pitch_item->valueint, 0, 90);
                motion.pitchServo().moveWithSpeed(pitch * 10, speed);
            }
        }

        cJSON* torque_item = cJSON_GetObjectItem(root, "torque");
        if (cJSON_IsBool(torque_item)) {
            motion.setTorqueEnabled(cJSON_IsTrue(torque_item));
        }

        cJSON* r_item = cJSON_GetObjectItem(root, "r");
        cJSON* g_item = cJSON_GetObjectItem(root, "g");
        cJSON* b_item = cJSON_GetObjectItem(root, "b");
        if (cJSON_IsNumber(r_item) && cJSON_IsNumber(g_item) && cJSON_IsNumber(b_item)) {
            uint8_t r = static_cast<uint8_t>(std::clamp(r_item->valueint, 0, 168));
            uint8_t g = static_cast<uint8_t>(std::clamp(g_item->valueint, 0, 168));
            uint8_t b = static_cast<uint8_t>(std::clamp(b_item->valueint, 0, 168));
            stackchan.leftNeonLight().setColor(r, g, b);
            stackchan.rightNeonLight().setColor(r, g, b);
        }

        if (stackchan.hasAvatar()) {
            cJSON* emotion_item = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion_item)) {
                stackchan.avatar().setEmotion(parse_emotion(emotion_item->valuestring));
            }

            cJSON* speech_item = cJSON_GetObjectItem(root, "speech");
            if (cJSON_IsString(speech_item)) {
                stackchan.avatar().setSpeech(speech_item->valuestring);
            }
        }
    }

    if (should_reboot) {
        GetHAL().delay(100);
        GetHAL().reboot();
    }

    return true;
}

esp_err_t send_text(httpd_req_t* req, const char* type, const char* body)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t index_handler(httpd_req_t* req)
{
    return send_text(req, "text/html", kIndexHtml);
}

esp_err_t status_handler(httpd_req_t* req)
{
    auto status = make_status_json();
    return send_text(req, "application/json", status.c_str());
}

esp_err_t control_handler(httpd_req_t* req)
{
    if (req->content_len <= 0 || req->content_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
        return ESP_FAIL;
    }

    std::string body;
    body.resize(req->content_len);
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read body");
            return ESP_FAIL;
        }
        received += ret;
    }

    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    std::string error;
    bool ok = apply_control_json(root, error);
    cJSON_Delete(root);

    if (!ok) {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddBoolToObject(json, "ok", false);
        cJSON_AddStringToObject(json, "error", error.c_str());
        std::string response = json_to_string(json);
        cJSON_Delete(json);
        return send_text(req, "application/json", response.c_str());
    }

    auto status = make_status_json();
    return send_text(req, "application/json", status.c_str());
}

}  // namespace

namespace local_control {

bool start()
{
    if (g_server != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kServerPort;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    if (httpd_start(&g_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start local control server");
        g_server = nullptr;
        return false;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t control_uri = {
        .uri = "/api/control",
        .method = HTTP_POST,
        .handler = control_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &index_uri);
    httpd_register_uri_handler(g_server, &status_uri);
    httpd_register_uri_handler(g_server, &control_uri);

    ESP_LOGI(TAG, "started at %s", get_url().c_str());
    return true;
}

void stop()
{
    if (g_server != nullptr) {
        httpd_stop(g_server);
        g_server = nullptr;
    }
}

bool is_running()
{
    return g_server != nullptr;
}

std::string get_url()
{
    auto ip = get_ip_address();
    if (ip.empty()) {
        return "";
    }
    return std::string("http://") + ip + "/";
}

}  // namespace local_control
