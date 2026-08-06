/*
 * SideEye — checkpoint 7: slave real vision judgement (replaces the
 * checkpoint4_capture_stub's fixed vehicleFound=1).
 *
 * Listens for CMD_CAPTURE_REQUEST from the master over ESPNOW, captures one
 * frame with its own camera, classifies it with SideEYEVision_inferencing
 * (background/vehicle), and replies CMD_RESULT_REPLY with the real
 * vehicleFound bit -- echoing the request's sequence number back so the
 * master's fail-safe (checkpoint7 on firmware/master/) can match the reply
 * to the outstanding request and drop stale ones.
 *
 * Blinks the built-in LED once per request purely for local debugging;
 * per PDR_SideEye.md section 4 the slave has no alert-display role.
 *
 * MAC (re-measured 2026-08-06 after board/port reshuffle):
 *   slave (this board)  AC:27:6E:A8:42:08  COM15
 *   master (peer)        AC:27:6E:A8:47:80  COM14
 */
#include <WiFi.h>
#include <cstring>
#include "esp_now.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"
#include <SideEYEVision_inferencing.h>
#include "../../shared/protocol.h"

constexpr uint8_t ESPNOW_WIFI_CHANNEL = 0;
uint8_t masterMac[6] = {0xAC, 0x27, 0x6E, 0xA8, 0x47, 0x80};
uint32_t replySequenceCounter = 0;

// ---- camera pins (XIAO ESP32-S3 Sense) ----
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39
#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13
static constexpr size_t CAMERA_WIDTH = 240;
static constexpr size_t CAMERA_HEIGHT = 240;

// Tensor arena in PSRAM (VISION_MODEL_DEPLOY.md section 5).
void *ei_malloc(size_t size) {
  void *p = heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM);
  if (!p) p = heap_caps_aligned_alloc(16, size, MALLOC_CAP_DEFAULT);
  return p;
}
void *ei_calloc(size_t n, size_t s) {
  void *p = ei_malloc(n * s);
  if (p) memset(p, 0, n * s);
  return p;
}
void ei_free(void *ptr) { heap_caps_free(ptr); }

static uint8_t *visionRgb888 = nullptr;
static float *visionFeatures = nullptr;

static int visionGetSignalData(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, visionFeatures + offset, length * sizeof(float));
  return 0;
}

static bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = -1; config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_240X240;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 15;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 1;
  if (esp_camera_init(&config) != ESP_OK) return false;
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) sensor->set_hmirror(sensor, 1);
  for (int i = 0; i < 8; ++i) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(60);
  }
  return true;
}

// Returns true and sets *vehicleFound on a successful classification; false
// on capture/decode/inference failure. The caller treats a false return the
// same as "no vehicle" (see master checkpoint7 file header: fail-safe means
// fail toward "no false alarm").
static bool captureAndClassify(bool *vehicleFound) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;
  bool decoded = fb->format == PIXFORMAT_JPEG && fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, visionRgb888);
  esp_camera_fb_return(fb);
  if (!decoded) return false;

  for (size_t y = 0; y < EI_CLASSIFIER_INPUT_HEIGHT; ++y) {
    size_t source_x = y * CAMERA_WIDTH / EI_CLASSIFIER_INPUT_HEIGHT;
    for (size_t x = 0; x < EI_CLASSIFIER_INPUT_WIDTH; ++x) {
      size_t source_y = CAMERA_HEIGHT - 1 - (x * CAMERA_HEIGHT / EI_CLASSIFIER_INPUT_WIDTH);
      size_t source = (source_y * CAMERA_WIDTH + source_x) * 3;
      size_t target = y * EI_CLASSIFIER_INPUT_WIDTH + x;
      uint32_t r = visionRgb888[source], g = visionRgb888[source + 1], b = visionRgb888[source + 2];
      visionFeatures[target] = static_cast<float>((r << 16) | (g << 8) | b);
    }
  }

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data = visionGetSignalData;
  ei_impulse_result_t result = {};
  uint32_t start = millis();
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) return false;

  size_t best = 0;
  for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
    if (result.classification[i].value > result.classification[best].value) best = i;
  }
  *vehicleFound = strcmp(result.classification[best].label, "vehicle") == 0;
  Serial.printf("[vision] %s %.2f (%lums)\n", result.classification[best].label,
                result.classification[best].value, millis() - start);
  return true;
}

volatile bool requestPending = false;
uint8_t requestFrom[6] = {};
uint32_t requestSequence = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (length != static_cast<int>(sizeof(EspNowMessage))) return;
  const EspNowMessage *msg = reinterpret_cast<const EspNowMessage *>(data);
  if (msg->magic != SIDEEYE_MSG_MAGIC || msg->version != SIDEEYE_MSG_VERSION) return;
  if (msg->command != CMD_CAPTURE_REQUEST) return;
  memcpy(requestFrom, info->src_addr, sizeof(requestFrom));
  requestSequence = msg->sequence;
  requestPending = true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("=== SideEye checkpoint 7: slave vision inference ===");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // off (active low)

  visionFeatures = static_cast<float *>(heap_caps_malloc(
      EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(float), MALLOC_CAP_SPIRAM));
  visionRgb888 = static_cast<uint8_t *>(heap_caps_malloc(
      CAMERA_WIDTH * CAMERA_HEIGHT * 3, MALLOC_CAP_SPIRAM));
  if (!visionFeatures || !visionRgb888) {
    Serial.println("ERROR: PSRAM allocation failed (compile with PSRAM=opi)");
    while (true) delay(1000);
  }
  if (!initCamera()) {
    Serial.println("ERROR: camera init failed");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  delay(100);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMac, sizeof(peerInfo.peer_addr));
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_err_t peerResult = esp_now_add_peer(&peerInfo);
  Serial.printf("[espnow] add_peer result=%d (%s), WiFi channel now=%d\n",
                peerResult, esp_err_to_name(peerResult), WiFi.channel());

  Serial.println("Ready, waiting for capture requests");
}

void loop() {
  if (!requestPending) return;
  requestPending = false;

  digitalWrite(LED_BUILTIN, LOW);

  bool vehicleFound = false;
  bool ok = captureAndClassify(&vehicleFound);
  if (!ok) {
    Serial.println("WARN: capture/inference failed, replying vehicleFound=0");
    vehicleFound = false;
  }

  EspNowMessage reply = {};
  reply.magic = SIDEEYE_MSG_MAGIC;
  reply.version = SIDEEYE_MSG_VERSION;
  reply.command = CMD_RESULT_REPLY;
  reply.vehicleFound = vehicleFound ? 1 : 0;
  reply.sequence = requestSequence;  // echo back so master can match/drop stale replies

  esp_err_t result = esp_now_send(requestFrom, reinterpret_cast<const uint8_t *>(&reply), sizeof(reply));
  Serial.printf("[REPLY] seq=%lu vehicleFound=%u result=%s\n",
                reply.sequence, reply.vehicleFound, result == ESP_OK ? "queued" : "error");

  delay(80);
  digitalWrite(LED_BUILTIN, HIGH);
}
