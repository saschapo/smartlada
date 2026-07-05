#pragma once
#include <stdint.h>

#include "../channels/channels.h"
#include "../config/config.h"

// Слой display: бортовой OLED SSD1306 128x64 платы HW-364A
// (I2C: SDA=GPIO14, SCL=GPIO12, адрес 0x3C) + кнопка FLASH (GPIO0)
// для переключения страниц. Кнопку не держать при ресете (вход в bootloader).
// [ВЫБРАСЫВАЕТСЯ] Привязан к плате стенда; на C6 не переносится.
//
// Страницы: 1) статус AP/heap/PWM/кап/loop-rate  2) каналы: % и duty
//           3) калибровка: gamma / min..max / soft_start.

namespace display {

// false — дисплей не найден (стенд продолжает работать без индикации).
bool begin(const config::Config* cfg, const channels::Channel* chans);

// Вызывать каждый loop(): кнопка, периодическая перерисовка, счётчик loop/s.
void tick(uint32_t now_ms);

}  // namespace display
