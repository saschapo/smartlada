#pragma once
#include <stdint.h>

#include "../channels/channels.h"

// Слой status: индикация на бортовом NeoPixel (WS2812, GPIO8). Заменяет OLED
// стенда ESP8266. Логика:
//   - канал(ы) активны  -> янтарный, яркость по макс. проценту (видно диммирование)
//   - есть клиент AP     -> ровный зелёный
//   - AP без клиентов    -> медленное голубое «дыхание» (признак жизни)

namespace status {

void begin(channels::Channel* chans);
void tick(uint32_t now_ms);

// Показать уровень яркости 0..100% на NeoPixel в течение короткого окна (для
// диммирования кнопкой). Имеет приоритет над штатной индикацией, пока активно.
void brightnessOverlay(uint8_t pct, uint32_t now_ms);

}  // namespace status
