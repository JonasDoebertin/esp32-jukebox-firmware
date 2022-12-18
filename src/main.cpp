#include "Arduino.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"
#include "JC_Button_ESP.h"

#include "Wire.h"
#include "nfc.h"

// Debugging

#define DEBUG

// Pin configuration

const int8_t SPI_MOSI_PIN = 15;
const int8_t SPI_MISO_PIN = 2;
const int8_t SPI_SCK_PIN = 14;
const uint8_t SD_CS_PIN = 13;
const uint8_t SD_DETECT_PIN = 34;

const uint8_t I2S_DOUT_PIN = 26;
const uint8_t I2S_BCLK_PIN = 5;
const uint8_t I2S_LRC_PIN = 25;

const gpio_num_t AMP_POWER_PIN = GPIO_NUM_21;
const gpio_num_t AMP_GAIN_PIN  = GPIO_NUM_23;

const uint8_t BTN_PLAY_PAUSE_PIN = 4;
const uint8_t BTN_VOLUME_DOWN_PIN = 19;
const uint8_t BTN_VOLUME_UP_PIN = 18;

const uint32_t I2C_FREQUENCY = 400000;
const int I2C_SDA_PIN = 32;
const int I2C_SCL_PIN = 12;

// Misc configuration

const int MIN_VOLUME = 1;
const int MAX_VOLUME = 21;
const int DEFAULT_VOLUME = 12;

const unsigned long NFC_SCAN_INTERVAL = 2000;

// Globals

Button buttonPlayPause(BTN_PLAY_PAUSE_PIN);
Button buttonVolumeDown(BTN_VOLUME_DOWN_PIN);
Button buttonVolumeUp(BTN_VOLUME_UP_PIN);

Audio audio;

enum States {STATE_IDLING, STATE_PAUSING, STATE_PLAYING};
States state = STATE_IDLING;

NFC_Module nfc;

uint currentTrack = 0;
uint numberOfTracks = 0;
std::string currentTagId = "";

void onPlayPauseButtonPressed();
void onVolumeDownButtonPressed();
void onVolumeUpButtonPressed();
void startPlayback();
void pausePlayback();
void stopPlayback();
bool hasCurrentTrack();
bool hasNextTrack();
void playCurrentTrack();
void playNextTrack();
void onTrackFinished();
uint getCurrentFileCount();
std::string getCurrentDirectoryPath();
std::string getCurrentFilePath();
void recoverI2CBus();
std::string asHexStr(uint8_t *buffer, uint32_t length);

// Setup

void setup() {
    Serial.begin(9600);

#ifdef DEBUG
    delay(5000);
#endif

    Serial.println("[Setup] Starting");

    // enable buttons

    buttonPlayPause.begin();
    buttonVolumeDown.begin();
    buttonVolumeUp.begin();
    Serial.println("[Setup] Buttons initialized");

    // enable amp power

    gpio_reset_pin(AMP_POWER_PIN);
    gpio_set_direction(AMP_POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(AMP_POWER_PIN, 1);
    gpio_set_pull_mode(AMP_GAIN_PIN, GPIO_PULLDOWN_ONLY);
    Serial.println("[Setup] Amp power initialized");

    // enable sd card

    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
    delay(500);
    //pinMode(SD_CS_PIN, OUTPUT);
    //digitalWrite(SD_CS_PIN, HIGH);

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[Setup] SD card initialization failed");
        while (true);
    }

    Serial.println("[Setup] SD card initialized");
    
    // enable audio output

    audio.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    audio.setVolume(DEFAULT_VOLUME);
    Serial.println("[Setup] Audio initialized");

    // enable nfc module

    Wire.setPins(I2C_SDA_PIN, I2C_SCL_PIN);
    nfc.begin();
    
    uint32_t versiondata = nfc.get_version();
    if (!versiondata) {
        Serial.println("[Setup] Tag reader initialization failed");
        while (true);
    }
    
#ifdef DEBUG
    Serial.print("[Setup] Found chip PN5");
    Serial.print((versiondata>>24) & 0xFF, HEX); 
    Serial.print(" with firmware v");
    Serial.print((versiondata>>16) & 0xFF, DEC); 
    Serial.print('.');
    Serial.println((versiondata>>8) & 0xFF, DEC);
#endif

    nfc.SAMConfiguration();
    Serial.println("[Setup] Tag reader initialized");
}

// Loop

void loop() {

    if (state == STATE_IDLING or state == STATE_PAUSING) {
        loopTagReader();
    }
    
    if (state == STATE_PAUSING or state == STATE_PLAYING) {
        audio.loop();
    }

    buttonPlayPause.read();
    if (buttonPlayPause.wasPressed()) {
        onPlayPauseButtonPressed();
    }

    buttonVolumeDown.read();
    if (buttonVolumeDown.wasPressed()) {
        onVolumeDownButtonPressed();
    }

    buttonVolumeUp.read();
    if (buttonVolumeUp.wasPressed()) {
        onVolumeUpButtonPressed();
    }
}

void loopTagReader() {
    static unsigned long previousMillis = 0;
	unsigned long currentMillis = millis();
	
	if (currentMillis - previousMillis >= NFC_SCAN_INTERVAL) {
		previousMillis = currentMillis;
        readTagReader();
	}
}

void readTagReader() {
    uint8_t buffer[32];
    uint8_t status;
    
    Serial.println("[NFC] Check for tag");
    status = nfc.InListPassiveTarget(buffer);
  
    if (status == 1 && buffer[0] == 4) {
        Serial.println("[NFC] Found tag");

#ifdef DEBUG       
        Serial.print("[NFC]   UUID length: ");
        Serial.print(buffer[0], DEC);
        Serial.println();
        Serial.print("[NFC]   UUID: ");
        nfc.puthex(buffer+1, buffer[0]);
        Serial.println();
#endif

        currentTagId = asHexStr(buffer+1, buffer[0]);
        startPlayback();
    }
}

// Event handlers

void onPlayPauseButtonPressed() {
    Serial.println("[GPIO] Play/pause button pressed");
    pausePlayback();
}

void onVolumeDownButtonPressed() {
    Serial.println("[GPIO] Volume down button pressed");
    audio.setVolume(max(audio.getVolume() - 1, MIN_VOLUME));
}

void onVolumeUpButtonPressed() {
    Serial.println("[GPIO] Volume up button pressed");
    audio.setVolume(min(audio.getVolume() + 1, MAX_VOLUME));
}

void audio_eof_mp3(const char *info) {
    Serial.println("[Audio] Track finished");
    Serial.print("[Audio]   ");
    Serial.println(info);

    onTrackFinished();
}

// Helpers

void startPlayback() {
    Serial.println("[Audio] Starting playback");

    currentTrack = 0;
    numberOfTracks = getCurrentFileCount();

    if (hasCurrentTrack()) {
        state = STATE_PLAYING;
        playCurrentTrack();
    }
}

void pausePlayback() {
    if (state == STATE_PLAYING) {
        Serial.println("[Audio] Pausing playback");
        state = STATE_PAUSING;
        audio.pauseResume();
    } else if (state == STATE_PAUSING) {
        Serial.println("[Audio] Unpausing playback");
        state = STATE_PLAYING;
        audio.pauseResume();
    }
}

void stopPlayback() {
    Serial.println("[Audio] Stopping playback");

    audio.stopSong();
    state = STATE_IDLING;
}

bool hasCurrentTrack() {
    return currentTrack < numberOfTracks;
}

bool hasNextTrack() {
    return (currentTrack + 1) < numberOfTracks;
}

void playNextTrack() {
    currentTrack++;
    playCurrentTrack();
}

void playCurrentTrack() {
    Serial.println("[Audio] Playing track");

    std::string filePath = getCurrentFilePath();

#ifdef DEBUG       
    Serial.print("[Audio]   Playing track: ");
    Serial.print(filePath.c_str());
    Serial.println();
    Serial.print("[Audio]   Number of tracks: ");
    Serial.print(numberOfTracks);
    Serial.println();
#endif

    audio.connecttoFS(SD, filePath.c_str());
}

void onTrackFinished() {
    if (hasNextTrack()) {
        playNextTrack();
    } else {
        stopPlayback();
    }
}

uint getCurrentFileCount() {
    uint count = 0;
    
    File dir = SD.open(getCurrentDirectoryPath().c_str());

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }

        count++;

        entry.close();
    }

    return count;
}

std::string getCurrentDirectoryPath() {
    return std::string("/")
        + currentTagId;
}

std::string getCurrentFilePath() {
    return getCurrentDirectoryPath()
        + "/"
        + std::to_string(currentTrack)
        + ".mp3";
}

void recoverI2CBus() {
  Serial.println("[I2C] Performing bus recovery");

  // For the upcoming operations, target for a 100kHz toggle frequency.
  // This is the maximum frequency for I2C running in standard-mode.
  // The actual frequency will be lower, because of the additional
  // function calls that are done, but that is no problem.
  const auto half_period_usec = 1000000 / 100000 / 2;

  // Activate input and pull up resistor for the SCL pin.
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);  // NOLINT

  // This should make the signal on the line HIGH. If SCL is pulled low
  // on the I2C bus however, then some device is interfering with the SCL
  // line. In that case, the I2C bus cannot be recovered.
  delayMicroseconds(half_period_usec);
  if (digitalRead(I2C_SCL_PIN) == LOW) {  // NOLINT
    Serial.println("[I2C]   Recovery failed: SCL is held LOW on the I2C bus");
    return;
  }

  // From the specification:
  // "If the data line (SDA) is stuck LOW, send nine clock pulses. The
  //  device that held the bus LOW should release it sometime within
  //  those nine clocks."
  // We don't really have to detect if SDA is stuck low. We'll simply send
  // nine clock pulses here, just in case SDA is stuck. Actual checks on
  // the SDA line status will be done after the clock pulses.

  // Make sure that switching to output mode will make SCL low, just in
  // case other code has setup the pin for a HIGH signal.
  digitalWrite(I2C_SCL_PIN, LOW);  // NOLINT

  delayMicroseconds(half_period_usec);
  for (auto i = 0; i < 9; i++) {
    // Release pull up resistor and switch to output to make the signal LOW.
    pinMode(I2C_SCL_PIN, INPUT);   // NOLINT
    pinMode(I2C_SCL_PIN, OUTPUT);  // NOLINT
    delayMicroseconds(half_period_usec);

    // Release output and activate pull up resistor to make the signal HIGH.
    pinMode(I2C_SCL_PIN, INPUT);         // NOLINT
    pinMode(I2C_SCL_PIN, INPUT_PULLUP);  // NOLINT
    delayMicroseconds(half_period_usec);

    // When SCL is kept LOW at this point, we might be looking at a device
    // that applies clock stretching. Wait for the release of the SCL line,
    // but not forever. There is no specification for the maximum allowed
    // time. We'll stick to 500ms here.
    auto wait = 20;
    while (wait-- && digitalRead(I2C_SCL_PIN) == LOW) {  // NOLINT
      delay(25);
    }
    if (digitalRead(I2C_SCL_PIN) == LOW) {  // NOLINT
      Serial.println("[I2C]   Recovery failed: SCL is held LOW during clock pulse cycle");
      return;
    }
  }

  // Activate input and pull resistor for the SDA pin, so we can verify
  // that SDA is pulled HIGH in the following step.
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);  // NOLINT
  digitalWrite(I2C_SDA_PIN, LOW);      // NOLINT

  // By now, any stuck device ought to have sent all remaining bits of its
  // transaction, meaning that it should have freed up the SDA line, resulting
  // in SDA being pulled up.
  if (digitalRead(I2C_SDA_PIN) == LOW) {  // NOLINT
    Serial.println("[I2C]   Recovery failed: SDA is held LOW after clock pulse cycle");
    return;
  }

  // From the specification:
  // "I2C-bus compatible devices must reset their bus logic on receipt of
  //  a START or repeated START condition such that they all anticipate
  //  the sending of a target address, even if these START conditions are
  //  not positioned according to the proper format."
  // While the 9 clock pulses from above might have drained all bits of a
  // single byte within a transaction, a device might have more bytes to
  // transmit. So here we'll generate a START condition to snap the device
  // out of this state.
  // SCL and SDA are already high at this point, so we can generate a START
  // condition by making the SDA signal LOW.
  delayMicroseconds(half_period_usec);
  pinMode(I2C_SDA_PIN, INPUT);   // NOLINT
  pinMode(I2C_SDA_PIN, OUTPUT);  // NOLINT

  // From the specification:
  // "A START condition immediately followed by a STOP condition (void
  //  message) is an illegal format. Many devices however are designed to
  //  operate properly under this condition."
  // Finally, we'll bring the I2C bus into a starting state by generating
  // a STOP condition.
  delayMicroseconds(half_period_usec);
  pinMode(I2C_SDA_PIN, INPUT);         // NOLINT
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);  // NOLINT

  Serial.println("[I2C]   Bus recovered");
}

std::string asHexStr(uint8_t *buffer, uint32_t length) {
    static const char* digits = "0123456789ABCDEF";
    std::string result;

    for (uint32_t i = 0; i < length; i++) {
        result += digits[(buffer[i]>>4) & 0x0F];
        result += digits[buffer[i] & 0x0F];
    }

    return result;
}