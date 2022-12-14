#include "Arduino.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"
#include "JC_Button_ESP.h"
#include "Wire.h"
#include "Adafruit_PN532.h"

// Debugging

#define DEBUG

// Pin configuration

const int8_t SPI_MOSI_PIN = 15;
const int8_t SPI_MISO_PIN = 2;
const int8_t SPI_SCK_PIN = 14;

const uint8_t I2S_DOUT_PIN = 26;
const uint8_t I2S_BCLK_PIN = 5;
const uint8_t I2S_LRC_PIN = 25;
// #define I2S_DIN       GPIO_NUM_35

const gpio_num_t AMP_POWER_PIN = GPIO_NUM_21;
const gpio_num_t AMP_GAIN_PIN  = GPIO_NUM_23;
const uint8_t SD_CS_PIN = 13;
const uint8_t SD_DETECT_PIN = 34;

const uint8_t SDA_PIN = (uint8_t) GPIO_NUM_32;
const uint8_t SDC_PIN = (uint8_t) GPIO_NUM_12;

const uint8_t BTN_PLAY_PAUSE_PIN = 4;
const uint8_t BTN_VOLUME_DOWN_PIN = 19;
const uint8_t BTN_VOLUME_UP_PIN = 18;

// Misc configuration

const int MIN_VOLUME = 1;
const int MAX_VOLUME = 21;
const int DEFAULT_VOLUME = 12;

// Globals

Button buttonPlayPause(BTN_PLAY_PAUSE_PIN);
Button buttonVolumeDown(BTN_VOLUME_DOWN_PIN);
Button buttonVolumeUp(BTN_VOLUME_UP_PIN);

Audio audio;

enum States {STATE_IDLING, STATE_PLAYING};
States state = STATE_IDLING;

uint currentTrack = 0;
uint numberOfTracks = 0;
std::string currentTagId = "c2696yqf";

void onPlayPauseButtonPressed();
void onVolumeDownButtonPressed();
void onVolumeUpButtonPressed();
void startPlayback();
void stopPlayback();
bool hasCurrentTrack();
bool hasNextTrack();
void playCurrentTrack();
void playNextTrack();
void onTrackFinished();
uint getCurrentFileCount();
std::string getCurrentDirectoryPath();
std::string getCurrentFilePath();

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
    
    // // enable audio output
    audio.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    audio.setVolume(DEFAULT_VOLUME);
    Serial.println("[Setup] Audio initialized");

    // // start playback
    startPlayback();
}

/*

- Check if tag is scanned
    - if different tag then before
        - set state to playback
        - set directory to tag id
        - set current track to 0
        - set track count to number of files in directory
        - start playing current track

- on starting playing current track
    - play current track from directory

- on track finishes
    - if current track < track count
        - increment current track
        - start playing current track
    - else
        - set state to idle



*/

// Loop

void loop() {

    if (state == STATE_PLAYING) {
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

// Event handlers

void onPlayPauseButtonPressed() {
    Serial.println("[GPIO] Play/pause button pressed");
    if (state == STATE_PLAYING) {
        audio.pauseResume();
    }
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
    Serial.print("[Audio] Track finished: ");
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

void stopPlayback() {
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
    audio.connecttoFS(SD, getCurrentFilePath().c_str());
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
