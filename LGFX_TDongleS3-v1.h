#define LGFX_USE_V1
#include <LovyanGFX.hpp>

//#define BOOT_PIN       0
//#define LED_DI_PIN     40
//#define LED_CI_PIN     39
#define SD_MMC_D0_PIN               14
#define SD_MMC_D1_PIN               17
#define SD_MMC_D2_PIN               21
#define SD_MMC_D3_PIN               18
#define SD_MMC_CLK_PIN              12
#define SD_MMC_CMD_PIN              16

// T-Dongle-S3専用のディスプレイ設定クラスを定義
class LGFX_TDongleS3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance; // V1仕様のバックライトインスタンス

public:
  LGFX_TDongleS3() {
    // SPIバスの設定
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;     // ESP32-S3のFSPIを使用
      cfg.spi_mode = 0;             // SPIモード0
      cfg.freq_write = 27000000;    // 転送クロック 27MHz
      cfg.freq_read  = 16000000;
      cfg.pin_sclk = 5;             // SCLKピン
      cfg.pin_mosi = 3;             // MOSIピン
      cfg.pin_miso = -1;            // MISOは使わないので-1
      cfg.pin_dc   = 2;             // DCピン
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // パネル（液晶）の設定
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 4;     // CSピン
      cfg.pin_rst          = 1;     // RSTピン
      cfg.panel_width      = 80;    // 画面幅
      cfg.panel_height     = 160;   // 画面高さ
      cfg.offset_x         = 24;    // T-Dongle-S3に必要なXオフセット
      cfg.offset_y         = 0;     // T-Dongle-S3に必要なYオフセット
      cfg.invert           = false;  // 色反転を有効にする
      
      _panel_instance.config(cfg);
    }

    // バックライトの設定（LovyanGFX V1の正しい書き方）
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 38;              // バックライトピン番号
      cfg.invert = false;           // HIGHで点灯させる場合はfalse
      cfg.freq   = 44100;           // PWM周波数
      cfg.pwm_channel = 7;          // 使用するPWMチャンネル
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance); // パネルにバックライトを紐付け
    }

    setPanel(&_panel_instance);
  }
};
