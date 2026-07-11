#pragma once
#include <stdint.h>

#include "../channels/channels.h"

// Слой button: бортовая кнопка BOOT (GPIO9) как локальный пульт.
//   - короткое нажатие -> следующий режим anim по кругу, включая MANUAL (в MANUAL
//     все лампы включаются, чтобы диммировались вместе). Смену видно по цвету
//     NeoPixel в слое status.
//   - удержание        -> плавный диммер мастер-яркости (channels::setMasterPct),
//     действует во ВСЕХ режимах, включая анимации; скорость 0..100% за DIM_FULL_MS.
//     Направление чередуется от удержания к удержанию (у предела — внутрь диапазона).
//     Уровень визуализируется на NeoPixel через status (гамма-коррекция, 0=красный,
//     100=зелёный).
//
// Логика взята из стенда cineink (debounce + long-press «пока держим»), но без ISR:
// проект однопоточный, опрос из loop() достаточен и не требует volatile/IRAM.
// GPIO9 — strapping только в момент загрузки; в рантайме читается как обычный вход.

namespace button {

void begin(channels::Channel* chans);
void tick(uint32_t now_ms);

}  // namespace button
