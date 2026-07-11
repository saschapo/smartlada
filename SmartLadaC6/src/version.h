#pragma once

// Версия прошивки SmartLada C6. Печатается в Serial при старте, отдаётся в /status
// и показывается в шапке web-UI. Правится вручную при значимых изменениях.
#define FW_VERSION "0.7.0-c6"
#define FW_BUILD (__DATE__ " " __TIME__)
