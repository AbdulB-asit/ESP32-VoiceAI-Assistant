#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <cstring>

// ====================================================
// PIN CONFIGURATION
// ====================================================

#define BUTTON_PIN 8

#define LED_GREEN  9
#define LED_RED    10
#define LED_BLUE   11


// ====================================================
// WIFI CONFIGURATION
// ====================================================

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";


// ====================================================
// BACKEND URLS
// ====================================================

const char* BACKEND_HEALTH_URL =
    "http://host.wokwi.internal:8000/health";

const char* BACKEND_TEST_AUDIO_URL =
    "http://host.wokwi.internal:8000/test-audio";

const char* BACKEND_STT_URL =
    "http://host.wokwi.internal:8000/stt";

const char* BACKEND_CHAT_URL =
    "http://host.wokwi.internal:8000/chat";

const char* BACKEND_TTS_URL =
    "http://host.wokwi.internal:8000/tts";


// ====================================================
// AUDIO LIMITS
// ====================================================

// Maximum WAV file allowed in PSRAM
const size_t MAX_WAV_BYTES =
    2 * 1024 * 1024;


// ====================================================
// SYSTEM STATES
// ====================================================

enum SystemState {

  STATE_CONNECTING_WIFI,

  STATE_IDLE,

  STATE_RECORDING,

  STATE_PROCESSING,

  STATE_PLAYING,

  STATE_ERROR

};

SystemState currentState =
    STATE_CONNECTING_WIFI;


// ====================================================
// GLOBAL DATA
// ====================================================

// Stores latest AI response
String lastAIResponse = "";

// Prevents pipeline from running repeatedly
bool aiRequestStarted = false;


// ====================================================
// BUTTON DEBOUNCE
// ====================================================

bool lastRawButtonState = HIGH;
bool stableButtonState = HIGH;

unsigned long lastDebounceTime = 0;

const unsigned long DEBOUNCE_DELAY = 50;


// ====================================================
// STATE TIMING
// ====================================================

unsigned long stateStartTime = 0;


// ====================================================
// WAV INFORMATION STRUCTURE
// ====================================================

struct WavInfo {

  bool valid = false;

  uint16_t audioFormat = 0;

  uint16_t channels = 0;

  uint16_t bitsPerSample = 0;

  uint32_t sampleRate = 0;

  uint32_t byteRate = 0;

  size_t dataOffset = 0;

  uint32_t declaredDataSize = 0;

  size_t actualDataSize = 0;
};


// ====================================================
// LITTLE-ENDIAN HELPERS
// ====================================================

uint16_t readLE16(
  const uint8_t* p
) {

  return
      ((uint16_t)p[0]) |
      ((uint16_t)p[1] << 8);
}


uint32_t readLE32(
  const uint8_t* p
) {

  return
      ((uint32_t)p[0]) |
      ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) |
      ((uint32_t)p[3] << 24);
}


// ====================================================
// LED CONTROL
// ====================================================

void turnOffAllLEDs() {

  digitalWrite(
    LED_GREEN,
    LOW
  );

  digitalWrite(
    LED_RED,
    LOW
  );

  digitalWrite(
    LED_BLUE,
    LOW
  );
}


void updateLEDs() {

  turnOffAllLEDs();

  switch (currentState) {

    // ------------------------------------------------
    // CONNECTING TO WIFI
    // ------------------------------------------------

    case STATE_CONNECTING_WIFI:

      if (
        (millis() / 300) % 2 == 0
      ) {

        digitalWrite(
          LED_BLUE,
          HIGH
        );
      }

      break;


    // ------------------------------------------------
    // READY
    // ------------------------------------------------

    case STATE_IDLE:

      digitalWrite(
        LED_GREEN,
        HIGH
      );

      break;


    // ------------------------------------------------
    // RECORDING
    // ------------------------------------------------

    case STATE_RECORDING:

      digitalWrite(
        LED_RED,
        HIGH
      );

      break;


    // ------------------------------------------------
    // PROCESSING
    // ------------------------------------------------

    case STATE_PROCESSING:

      digitalWrite(
        LED_BLUE,
        HIGH
      );

      break;


    // ------------------------------------------------
    // PLAYING
    // ------------------------------------------------

    case STATE_PLAYING:

      if (
        (millis() / 300) % 2 == 0
      ) {

        digitalWrite(
          LED_GREEN,
          HIGH
        );
      }

      break;


    // ------------------------------------------------
    // ERROR
    // ------------------------------------------------

    case STATE_ERROR:

      if (
        (millis() / 200) % 2 == 0
      ) {

        digitalWrite(
          LED_RED,
          HIGH
        );
      }

      break;
  }
}


// ====================================================
// STATE CHANGE
// ====================================================

void changeState(
  SystemState newState
) {

  currentState =
      newState;

  stateStartTime =
      millis();

  Serial.print(
    "[STATE] "
  );

  switch (newState) {

    case STATE_CONNECTING_WIFI:

      Serial.println(
        "CONNECTING_WIFI"
      );

      break;


    case STATE_IDLE:

      Serial.println(
        "IDLE"
      );

      break;


    case STATE_RECORDING:

      Serial.println(
        "RECORDING"
      );

      break;


    case STATE_PROCESSING:

      Serial.println(
        "PROCESSING"
      );

      break;


    case STATE_PLAYING:

      Serial.println(
        "PLAYING"
      );

      break;


    case STATE_ERROR:

      Serial.println(
        "ERROR"
      );

      break;
  }
}


// ====================================================
// MEMORY INFORMATION
// ====================================================

void printMemoryInfo() {

  Serial.println();

  Serial.println(
    "------ MEMORY INFORMATION ------"
  );

  Serial.printf(
    "Heap total : %u bytes\n",
    ESP.getHeapSize()
  );

  Serial.printf(
    "Heap free  : %u bytes\n",
    ESP.getFreeHeap()
  );

  Serial.printf(
    "PSRAM total: %u bytes\n",
    ESP.getPsramSize()
  );

  Serial.printf(
    "PSRAM free : %u bytes\n",
    ESP.getFreePsram()
  );

  Serial.println(
    "--------------------------------"
  );

  if (
    ESP.getPsramSize() > 0
  ) {

    Serial.println(
      "[PSRAM] Detected successfully"
    );

  } else {

    Serial.println(
      "[PSRAM] NOT detected"
    );
  }
}


// ====================================================
// WIFI CONNECTION
// ====================================================

bool connectWiFi() {

  changeState(
    STATE_CONNECTING_WIFI
  );

  Serial.println();

  Serial.println(
    "[WIFI] Connecting to Wokwi-GUEST..."
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD,
    6
  );

  unsigned long startAttempt =
      millis();

  const unsigned long timeout =
      15000;

  while (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    updateLEDs();

    Serial.print(
      "."
    );

    delay(
      250
    );

    if (
      millis() -
      startAttempt >=
      timeout
    ) {

      Serial.println();

      Serial.println(
        "[WIFI] Connection timeout"
      );

      changeState(
        STATE_ERROR
      );

      return false;
    }
  }

  Serial.println();

  Serial.println(
    "[WIFI] Connected successfully"
  );

  Serial.print(
    "[WIFI] IP address: "
  );

  Serial.println(
    WiFi.localIP()
  );

  Serial.print(
    "[WIFI] Signal strength: "
  );

  Serial.print(
    WiFi.RSSI()
  );

  Serial.println(
    " dBm"
  );

  return true;
}


// ====================================================
// BACKEND HEALTH CHECK
// ====================================================

bool testBackendConnection() {

  Serial.println();

  Serial.println(
    "[BACKEND] Testing connection..."
  );

  HTTPClient http;

  http.begin(
    BACKEND_HEALTH_URL
  );

  http.setTimeout(
    5000
  );

  int httpCode =
      http.GET();

  Serial.print(
    "[BACKEND] HTTP status: "
  );

  Serial.println(
    httpCode
  );

  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    String response =
        http.getString();

    Serial.println(
      "[BACKEND] Response:"
    );

    Serial.println(
      response
    );

    http.end();

    Serial.println(
      "[BACKEND] Connection successful"
    );

    return true;
  }

  if (
    httpCode < 0
  ) {

    Serial.print(
      "[BACKEND] Error: "
    );

    Serial.println(
      http.errorToString(
        httpCode
      )
    );
  }

  http.end();

  Serial.println(
    "[BACKEND] Connection failed"
  );

  return false;
}


// ====================================================
// GROQ LLM REQUEST
// ====================================================

bool askAI(
  const String& userMessage
) {

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    Serial.println(
      "[AI] WiFi not connected"
    );

    return false;
  }

  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    "[AI] Sending message to backend"
  );

  Serial.print(
    "[USER] "
  );

  Serial.println(
    userMessage
  );


  // --------------------------------------------------
  // BUILD JSON REQUEST
  // --------------------------------------------------

  JsonDocument requestDoc;

  requestDoc["message"] =
      userMessage;

  String requestBody;

  serializeJson(
    requestDoc,
    requestBody
  );


  // --------------------------------------------------
  // SEND REQUEST
  // --------------------------------------------------

  HTTPClient http;

  http.begin(
    BACKEND_CHAT_URL
  );

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  http.setTimeout(
    30000
  );

  int httpCode =
      http.POST(
        requestBody
      );

  Serial.print(
    "[CHAT HTTP] Status: "
  );

  Serial.println(
    httpCode
  );


  // --------------------------------------------------
  // ERROR
  // --------------------------------------------------

  if (
    httpCode !=
    HTTP_CODE_OK
  ) {

    if (
      httpCode > 0
    ) {

      Serial.println(
        "[CHAT] Error response:"
      );

      Serial.println(
        http.getString()
      );
    }

    http.end();

    return false;
  }


  // --------------------------------------------------
  // RECEIVE RESPONSE
  // --------------------------------------------------

  String responseBody =
      http.getString();

  http.end();


  // --------------------------------------------------
  // PARSE JSON
  // --------------------------------------------------

  JsonDocument responseDoc;

  DeserializationError error =
      deserializeJson(
        responseDoc,
        responseBody
      );

  if (
    error
  ) {

    Serial.print(
      "[JSON] Parse error: "
    );

    Serial.println(
      error.c_str()
    );

    return false;
  }


  if (
    !responseDoc["response"]
      .is<const char*>()
  ) {

    Serial.println(
      "[AI] Missing response field"
    );

    return false;
  }


  lastAIResponse =
      responseDoc["response"]
      .as<String>();


  Serial.println();

  Serial.println(
    "========== AI RESPONSE =========="
  );

  Serial.println(
    lastAIResponse
  );

  Serial.println(
    "================================="
  );

  Serial.print(
    "[AI] Character count: "
  );

  Serial.println(
    lastAIResponse.length()
  );

  return true;
}


// ====================================================
// WAV PARSER
// ====================================================

bool parseWav(
  const uint8_t* buffer,
  size_t length,
  WavInfo& info
) {

  // Minimum RIFF header
  if (
    length < 12
  ) {

    Serial.println(
      "[WAV] File too small"
    );

    return false;
  }


  // RIFF
  if (
    memcmp(
      buffer,
      "RIFF",
      4
    ) != 0
  ) {

    Serial.println(
      "[WAV] RIFF header missing"
    );

    return false;
  }


  // WAVE
  if (
    memcmp(
      buffer + 8,
      "WAVE",
      4
    ) != 0
  ) {

    Serial.println(
      "[WAV] WAVE header missing"
    );

    return false;
  }


  bool foundFmt =
      false;

  bool foundData =
      false;

  size_t offset =
      12;


  // --------------------------------------------------
  // SEARCH WAV CHUNKS
  // --------------------------------------------------

  while (
    offset + 8 <=
    length
  ) {

    const uint8_t* chunk =
        buffer + offset;

    uint32_t chunkSize =
        readLE32(
          chunk + 4
        );

    size_t chunkDataOffset =
        offset + 8;


    // ------------------------------------------------
    // FMT CHUNK
    // ------------------------------------------------

    if (
      memcmp(
        chunk,
        "fmt ",
        4
      ) == 0
    ) {

      if (
        chunkDataOffset + 16 >
        length
      ) {

        return false;
      }


      info.audioFormat =
          readLE16(
            buffer +
            chunkDataOffset
          );


      info.channels =
          readLE16(
            buffer +
            chunkDataOffset +
            2
          );


      info.sampleRate =
          readLE32(
            buffer +
            chunkDataOffset +
            4
          );


      info.byteRate =
          readLE32(
            buffer +
            chunkDataOffset +
            8
          );


      info.bitsPerSample =
          readLE16(
            buffer +
            chunkDataOffset +
            14
          );


      foundFmt =
          true;
    }


    // ------------------------------------------------
    // DATA CHUNK
    // ------------------------------------------------

    else if (
      memcmp(
        chunk,
        "data",
        4
      ) == 0
    ) {

      info.dataOffset =
          chunkDataOffset;

      info.declaredDataSize =
          chunkSize;


      size_t bytesRemaining =
          length -
          chunkDataOffset;


      // Some streaming WAV files report 0xFFFFFFFF
      // as the data size.
      //
      // Therefore we never trust a declared size
      // larger than the actual downloaded file.

      if (
        chunkSize >
        bytesRemaining
      ) {

        info.actualDataSize =
            bytesRemaining;

      } else {

        info.actualDataSize =
            chunkSize;
      }


      foundData =
          true;

      break;
    }


    // WAV chunks are word aligned
    size_t paddedChunkSize =
        (size_t)chunkSize +
        (chunkSize & 1);


    if (
      chunkDataOffset +
      paddedChunkSize >
      length
    ) {

      break;
    }


    offset =
        chunkDataOffset +
        paddedChunkSize;
  }


  info.valid =
      foundFmt &&
      foundData;

  return info.valid;
}


// ====================================================
// PRINT WAV INFORMATION
// ====================================================

void printWavInfo(
  const WavInfo& info,
  size_t downloadedBytes
) {

  Serial.println();

  Serial.println(
    "========== WAV INFORMATION =========="
  );


  Serial.print(
    "Downloaded bytes : "
  );

  Serial.println(
    downloadedBytes
  );


  Serial.print(
    "Audio format     : "
  );

  Serial.println(
    info.audioFormat
  );


  Serial.print(
    "Channels         : "
  );

  Serial.println(
    info.channels
  );


  Serial.print(
    "Sample rate      : "
  );

  Serial.print(
    info.sampleRate
  );

  Serial.println(
    " Hz"
  );


  Serial.print(
    "Bits per sample  : "
  );

  Serial.println(
    info.bitsPerSample
  );


  Serial.print(
    "Byte rate        : "
  );

  Serial.println(
    info.byteRate
  );


  Serial.print(
    "Data offset      : "
  );

  Serial.println(
    info.dataOffset
  );


  Serial.print(
    "Declared data    : "
  );

  Serial.println(
    info.declaredDataSize
  );


  Serial.print(
    "Actual audio data: "
  );

  Serial.println(
    info.actualDataSize
  );


  if (
    info.byteRate > 0
  ) {

    float duration =
        (float)info.actualDataSize /
        (float)info.byteRate;


    Serial.print(
      "Actual duration  : "
    );

    Serial.print(
      duration,
      2
    );

    Serial.println(
      " seconds"
    );
  }


  Serial.println(
    "====================================="
  );
}


// ====================================================
// DOWNLOAD SIMULATED MICROPHONE WAV
// ====================================================

bool downloadTestAudio(
  uint8_t*& audioBuffer,
  size_t& audioSize
) {

  audioBuffer =
      nullptr;

  audioSize =
      0;


  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    "[MIC] Downloading simulated microphone WAV"
  );


  HTTPClient http;

  http.begin(
    BACKEND_TEST_AUDIO_URL
  );

  http.setTimeout(
    15000
  );


  int httpCode =
      http.GET();


  Serial.print(
    "[MIC HTTP] Status: "
  );

  Serial.println(
    httpCode
  );


  if (
    httpCode !=
    HTTP_CODE_OK
  ) {

    Serial.println(
      "[MIC] Failed to download test audio"
    );

    http.end();

    return false;
  }


  int contentLength =
      http.getSize();


  Serial.print(
    "[MIC] WAV size: "
  );

  Serial.print(
    contentLength
  );

  Serial.println(
    " bytes"
  );


  if (
    contentLength <= 0
  ) {

    Serial.println(
      "[MIC] Invalid content length"
    );

    http.end();

    return false;
  }


  if (
    (size_t)contentLength >
    MAX_WAV_BYTES
  ) {

    Serial.println(
      "[MIC] Audio exceeds safety limit"
    );

    http.end();

    return false;
  }


  // --------------------------------------------------
  // ALLOCATE PSRAM
  // --------------------------------------------------

  Serial.print(
    "[MIC] PSRAM free before allocation: "
  );

  Serial.println(
    ESP.getFreePsram()
  );


  audioBuffer =
      (uint8_t*)
      heap_caps_malloc(
        contentLength,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      );


  if (
    audioBuffer ==
    nullptr
  ) {

    Serial.println(
      "[MIC] PSRAM allocation failed"
    );

    http.end();

    return false;
  }


  Serial.println(
    "[MIC] PSRAM buffer allocated"
  );


  // --------------------------------------------------
  // DOWNLOAD
  // --------------------------------------------------

  WiFiClient* stream =
      http.getStreamPtr();


  size_t received =
      0;


  unsigned long lastDataTime =
      millis();


  while (
    received <
    (size_t)contentLength
  ) {

    size_t available =
        stream->available();


    if (
      available > 0
    ) {

      size_t remaining =
          (size_t)contentLength -
          received;


      size_t toRead =
          available <
          remaining
          ?
          available
          :
          remaining;


      size_t bytesRead =
          stream->readBytes(
            audioBuffer +
            received,
            toRead
          );


      if (
        bytesRead > 0
      ) {

        received +=
            bytesRead;

        lastDataTime =
            millis();
      }

    } else {

      if (
        millis() -
        lastDataTime >
        10000
      ) {

        Serial.println(
          "[MIC] Download timeout"
        );

        break;
      }

      delay(
        1
      );
    }
  }


  http.end();


  Serial.print(
    "[MIC] Bytes downloaded: "
  );

  Serial.println(
    received
  );


  if (
    received !=
    (size_t)contentLength
  ) {

    Serial.println(
      "[MIC] Incomplete WAV download"
    );

    free(
      audioBuffer
    );

    audioBuffer =
        nullptr;

    return false;
  }


  audioSize =
      received;


  // --------------------------------------------------
  // VALIDATE WAV
  // --------------------------------------------------

  WavInfo wavInfo;


  if (
    !parseWav(
      audioBuffer,
      audioSize,
      wavInfo
    )
  ) {

    Serial.println(
      "[MIC] Invalid WAV"
    );

    free(
      audioBuffer
    );

    audioBuffer =
        nullptr;

    audioSize =
        0;

    return false;
  }


  Serial.println(
    "[MIC] Valid simulated microphone WAV"
  );


  printWavInfo(
    wavInfo,
    audioSize
  );


  return true;
}


// ====================================================
// SPEECH TO TEXT
// ====================================================

bool transcribeAudio(
  uint8_t* audioBuffer,
  size_t audioSize,
  String& transcript
) {

  transcript =
      "";


  if (
    audioBuffer == nullptr ||
    audioSize == 0
  ) {

    Serial.println(
      "[STT] No audio supplied"
    );

    return false;
  }


  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    "[STT] Sending WAV to Whisper"
  );


  Serial.print(
    "[STT] Upload size: "
  );

  Serial.print(
    audioSize
  );

  Serial.println(
    " bytes"
  );


  HTTPClient http;

  http.begin(
    BACKEND_STT_URL
  );


  http.addHeader(
    "Content-Type",
    "audio/wav"
  );


  http.setTimeout(
    30000
  );


  // --------------------------------------------------
  // SEND RAW WAV
  // --------------------------------------------------

  int httpCode =
      http.POST(
        audioBuffer,
        audioSize
      );


  Serial.print(
    "[STT HTTP] Status: "
  );

  Serial.println(
    httpCode
  );


  if (
    httpCode !=
    HTTP_CODE_OK
  ) {

    if (
      httpCode > 0
    ) {

      Serial.println(
        "[STT] Error response:"
      );

      Serial.println(
        http.getString()
      );
    }


    http.end();

    return false;
  }


  // --------------------------------------------------
  // RECEIVE TRANSCRIPT
  // --------------------------------------------------

  String responseBody =
      http.getString();


  http.end();


  Serial.println(
    "[STT] Backend response:"
  );

  Serial.println(
    responseBody
  );


  JsonDocument responseDoc;


  DeserializationError error =
      deserializeJson(
        responseDoc,
        responseBody
      );


  if (
    error
  ) {

    Serial.print(
      "[STT] JSON parsing failed: "
    );

    Serial.println(
      error.c_str()
    );

    return false;
  }


  if (
    !responseDoc["text"]
      .is<const char*>()
  ) {

    Serial.println(
      "[STT] Missing text field"
    );

    return false;
  }


  transcript =
      responseDoc["text"]
      .as<String>();


  transcript.trim();


  if (
    transcript.length() == 0
  ) {

    Serial.println(
      "[STT] Empty transcription"
    );

    return false;
  }


  Serial.println();

  Serial.println(
    "========== TRANSCRIPTION =========="
  );

  Serial.println(
    transcript
  );

  Serial.println(
    "==================================="
  );


  return true;
}


// ====================================================
// TEXT TO SPEECH + DOWNLOAD WAV
// ====================================================

bool downloadTTS(
  const String& text
) {

  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    "[TTS] Requesting speech audio"
  );


  Serial.print(
    "[TTS] Text: "
  );

  Serial.println(
    text
  );


  Serial.print(
    "[TTS] Text length: "
  );

  Serial.println(
    text.length()
  );


  if (
    text.length() == 0
  ) {

    Serial.println(
      "[TTS] Empty text"
    );

    return false;
  }


  if (
    text.length() > 200
  ) {

    Serial.println(
      "[TTS] Text exceeds 200 characters"
    );

    return false;
  }


  // --------------------------------------------------
  // BUILD JSON
  // --------------------------------------------------

  JsonDocument requestDoc;

  requestDoc["text"] =
      text;


  String requestBody;


  serializeJson(
    requestDoc,
    requestBody
  );


  // --------------------------------------------------
  // HTTP
  // --------------------------------------------------

  HTTPClient http;

  http.begin(
    BACKEND_TTS_URL
  );


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  http.setTimeout(
    30000
  );


  int httpCode =
      http.POST(
        requestBody
      );


  Serial.print(
    "[TTS HTTP] Status: "
  );

  Serial.println(
    httpCode
  );


  if (
    httpCode !=
    HTTP_CODE_OK
  ) {

    if (
      httpCode > 0
    ) {

      String body =
          http.getString();


      Serial.println(
        "[TTS] Error response:"
      );


      Serial.println(
        body
      );
    }


    http.end();

    return false;
  }


  // --------------------------------------------------
  // GET CONTENT SIZE
  // --------------------------------------------------

  int contentLength =
      http.getSize();


  Serial.print(
    "[TTS] HTTP content length: "
  );


  Serial.println(
    contentLength
  );


  if (
    contentLength <= 0
  ) {

    Serial.println(
      "[TTS] Invalid content length"
    );

    http.end();

    return false;
  }


  if (
    (size_t)contentLength >
    MAX_WAV_BYTES
  ) {

    Serial.println(
      "[TTS] WAV exceeds safety limit"
    );

    http.end();

    return false;
  }


  // --------------------------------------------------
  // PSRAM
  // --------------------------------------------------

  Serial.print(
    "[PSRAM] Free before TTS allocation: "
  );


  Serial.println(
    ESP.getFreePsram()
  );


  uint8_t* wavBuffer =
      (uint8_t*)
      heap_caps_malloc(
        contentLength,
        MALLOC_CAP_SPIRAM |
        MALLOC_CAP_8BIT
      );


  if (
    wavBuffer ==
    nullptr
  ) {

    Serial.println(
      "[PSRAM] TTS WAV allocation failed"
    );

    http.end();

    return false;
  }


  Serial.println(
    "[PSRAM] TTS WAV buffer allocated"
  );


  // --------------------------------------------------
  // DOWNLOAD WAV
  // --------------------------------------------------

  WiFiClient* stream =
      http.getStreamPtr();


  size_t received =
      0;


  unsigned long lastDataTime =
      millis();


  while (
    received <
    (size_t)contentLength
  ) {

    size_t available =
        stream->available();


    if (
      available > 0
    ) {

      size_t remaining =
          (size_t)contentLength -
          received;


      size_t toRead =
          available <
          remaining
          ?
          available
          :
          remaining;


      size_t bytesRead =
          stream->readBytes(
            wavBuffer +
            received,
            toRead
          );


      if (
        bytesRead > 0
      ) {

        received +=
            bytesRead;

        lastDataTime =
            millis();
      }

    } else {

      if (
        millis() -
        lastDataTime >
        10000
      ) {

        Serial.println(
          "[TTS] Download timeout"
        );

        break;
      }


      delay(
        1
      );
    }
  }


  http.end();


  Serial.print(
    "[TTS] Bytes received: "
  );


  Serial.println(
    received
  );


  if (
    received == 0
  ) {

    Serial.println(
      "[TTS] No WAV data received"
    );

    free(
      wavBuffer
    );

    return false;
  }


  if (
    received !=
    (size_t)contentLength
  ) {

    Serial.println(
      "[TTS] WARNING: Partial download"
    );
  }


  // --------------------------------------------------
  // CHECK HEADER
  // --------------------------------------------------

  Serial.print(
    "[WAV] First four bytes: "
  );


  if (
    received >= 4
  ) {

    Serial.write(
      wavBuffer,
      4
    );

    Serial.println();

  } else {

    Serial.println(
      "N/A"
    );
  }


  // --------------------------------------------------
  // PARSE WAV
  // --------------------------------------------------

  WavInfo wavInfo;


  bool valid =
      parseWav(
        wavBuffer,
        received,
        wavInfo
      );


  if (
    !valid
  ) {

    Serial.println(
      "[WAV] Invalid TTS WAV"
    );

    free(
      wavBuffer
    );

    return false;
  }


  Serial.println(
    "[WAV] Valid RIFF/WAVE file detected"
  );


  printWavInfo(
    wavInfo,
    received
  );


  Serial.println();

  Serial.println(
    "[AUDIO] Response WAV available in PSRAM"
  );

  Serial.println(
    "[AUDIO] Physical speaker playback comes next"
  );


  // For simulation we only validate it
  free(
    wavBuffer
  );


  Serial.println(
    "[PSRAM] TTS WAV buffer released"
  );


  Serial.print(
    "[PSRAM] Free after release: "
  );


  Serial.println(
    ESP.getFreePsram()
  );


  return true;
}


// ====================================================
// BUTTON HANDLING
// ====================================================

void handleButton() {

  bool rawButtonState =
      digitalRead(
        BUTTON_PIN
      );


  if (
    rawButtonState !=
    lastRawButtonState
  ) {

    lastDebounceTime =
        millis();

    lastRawButtonState =
        rawButtonState;
  }


  if (
    millis() -
    lastDebounceTime >
    DEBOUNCE_DELAY
  ) {

    if (
      rawButtonState !=
      stableButtonState
    ) {

      stableButtonState =
          rawButtonState;


      // ----------------------------------------------
      // BUTTON PRESSED
      // ----------------------------------------------

      if (
        stableButtonState ==
        LOW
      ) {

        if (
          currentState ==
          STATE_IDLE
        ) {

          Serial.println(
            "[BUTTON] Pressed"
          );

          Serial.println(
            "[AUDIO] Recording started"
          );

          changeState(
            STATE_RECORDING
          );
        }
      }


      // ----------------------------------------------
      // BUTTON RELEASED
      // ----------------------------------------------

      else {

        if (
          currentState ==
          STATE_RECORDING
        ) {

          Serial.println(
            "[BUTTON] Released"
          );

          Serial.println(
            "[AUDIO] Recording stopped"
          );

          changeState(
            STATE_PROCESSING
          );
        }
      }
    }
  }
}


// ====================================================
// FULL VOICE PROCESSING PIPELINE
// ====================================================

void handleProcessing() {

  if (
    currentState ==
    STATE_PROCESSING &&
    !aiRequestStarted
  ) {

    aiRequestStarted =
        true;


    uint8_t* microphoneWav =
        nullptr;


    size_t microphoneWavSize =
        0;


    String transcription =
        "";


    // =================================================
    // STEP 1
    // GET SIMULATED MICROPHONE AUDIO
    // =================================================

    Serial.println();

    Serial.println(
      "[PIPELINE] STEP 1 - Simulated microphone"
    );


    bool audioSuccess =
        downloadTestAudio(
          microphoneWav,
          microphoneWavSize
        );


    if (
      !audioSuccess
    ) {

      Serial.println(
        "[PIPELINE] Microphone WAV failed"
      );

      aiRequestStarted =
          false;

      changeState(
        STATE_ERROR
      );

      return;
    }


    // =================================================
    // STEP 2
    // SPEECH TO TEXT
    // =================================================

    Serial.println();

    Serial.println(
      "[PIPELINE] STEP 2 - Speech to text"
    );


    bool sttSuccess =
        transcribeAudio(
          microphoneWav,
          microphoneWavSize,
          transcription
        );


    // Input audio no longer needed
    free(
      microphoneWav
    );


    microphoneWav =
        nullptr;


    Serial.println(
      "[MIC] Input WAV released from PSRAM"
    );


    if (
      !sttSuccess
    ) {

      Serial.println(
        "[PIPELINE] STT failed"
      );

      aiRequestStarted =
          false;

      changeState(
        STATE_ERROR
      );

      return;
    }


    // =================================================
    // STEP 3
    // LLM
    // =================================================

    Serial.println();

    Serial.println(
      "[PIPELINE] STEP 3 - Groq LLM"
    );


    bool chatSuccess =
        askAI(
          transcription
        );


    if (
      !chatSuccess
    ) {

      Serial.println(
        "[PIPELINE] LLM failed"
      );

      aiRequestStarted =
          false;

      changeState(
        STATE_ERROR
      );

      return;
    }


    // =================================================
    // STEP 4
    // TEXT TO SPEECH
    // =================================================

    Serial.println();

    Serial.println(
      "[PIPELINE] STEP 4 - Text to speech"
    );


    bool ttsSuccess =
        downloadTTS(
          lastAIResponse
        );


    if (
      !ttsSuccess
    ) {

      Serial.println(
        "[PIPELINE] TTS failed"
      );

      aiRequestStarted =
          false;

      changeState(
        STATE_ERROR
      );

      return;
    }


    // =================================================
    // FULL PIPELINE SUCCESS
    // =================================================

    Serial.println();

    Serial.println(
      "======================================"
    );

    Serial.println(
      "[PIPELINE] FULL VOICE PIPELINE SUCCESS"
    );

    Serial.println(
      "STT -> LLM -> TTS completed"
    );

    Serial.println(
      "======================================"
    );


    changeState(
      STATE_PLAYING
    );
  }


  // ==================================================
  // PLAYBACK STILL SIMULATED
  // ==================================================

  if (
    currentState ==
    STATE_PLAYING
  ) {

    if (
      millis() -
      stateStartTime >=
      2000
    ) {

      Serial.println(
        "[AUDIO] Playback stage completed"
      );


      aiRequestStarted =
          false;


      lastAIResponse =
          "";


      changeState(
        STATE_IDLE
      );
    }
  }
}


// ====================================================
// WIFI MONITOR
// ====================================================

void checkWiFi() {

  static unsigned long lastCheck =
      0;


  if (
    millis() -
    lastCheck <
    5000
  ) {

    return;
  }


  lastCheck =
      millis();


  if (
    WiFi.status() !=
    WL_CONNECTED &&
    currentState ==
    STATE_IDLE
  ) {

    Serial.println(
      "[WIFI] Connection lost"
    );


    if (
      connectWiFi()
    ) {

      changeState(
        STATE_IDLE
      );
    }
  }
}


// ====================================================
// SETUP
// ====================================================

void setup() {

  Serial.begin(
    115200
  );


  delay(
    1000
  );


  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    " ESP32-S3 AI Voice Assistant"
  );

  Serial.println(
    " Stage 6 - Full STT + LLM + TTS"
  );

  Serial.println(
    "======================================"
  );


  // --------------------------------------------------
  // GPIO
  // --------------------------------------------------

  pinMode(
    BUTTON_PIN,
    INPUT_PULLUP
  );


  pinMode(
    LED_GREEN,
    OUTPUT
  );


  pinMode(
    LED_RED,
    OUTPUT
  );


  pinMode(
    LED_BLUE,
    OUTPUT
  );


  turnOffAllLEDs();


  // --------------------------------------------------
  // MEMORY
  // --------------------------------------------------

  printMemoryInfo();


  // --------------------------------------------------
  // WIFI
  // --------------------------------------------------

  if (
    !connectWiFi()
  ) {

    Serial.println(
      "[BOOT] WiFi failed"
    );

    return;
  }


  // --------------------------------------------------
  // BACKEND
  // --------------------------------------------------

  if (
    !testBackendConnection()
  ) {

    Serial.println(
      "[BOOT] Backend unavailable"
    );

    changeState(
      STATE_ERROR
    );

    return;
  }


  Serial.println();

  Serial.println(
    "[BOOT] Backend online"
  );

  Serial.println(
    "[BOOT] System ready"
  );


  changeState(
    STATE_IDLE
  );
}


// ====================================================
// MAIN LOOP
// ====================================================

void loop() {

  handleButton();

  handleProcessing();

  checkWiFi();

  updateLEDs();

  delay(
    1
  );
}