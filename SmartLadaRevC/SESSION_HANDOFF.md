# SmartLada Rev C — Session Handoff (2026-09-04)

Передача для новой сессии. Фокус: прошивка `SmartLadaRevC/` (слияние Zigbee в основную прошивку).
План целиком — в [`DEV_PLAN.md`](DEV_PLAN.md). Роадмап/уроки — в авто-памяти (`revc-firmware-roadmap`).

## Где мы (сделано за сессию)

- **Плата revC поднята полностью** в железе: питание (USB-C PD 12В через CH224K + buck TPS54202 → 3.3В), OLED, кнопки, I2C, 4 PWM-канала ламп (гоняли 35 Вт реальной нагрузки — стабильно). Пройдены баги: перевёрнутый U4 зажимал 3V3; EP U3 (единственная земля CH224K) паяли через низ; **LEDC на C6 тактируется от 40 МГц → RES_BITS=10** (иначе 20кГц > потолка 19.5кГц при 11 битах, каналы молчат).
- **Ревью+чистка кода** прошивки: исправлены ложные комментарии, гарды буферов (`MAX_FX_PARAMS`, `MODE_MAX`), `cur[]`→`screenCursor[]`. Мёртвого кода нет (кроме намеренно оставленного `display::setInverse` под будущий «No 12V»).
- **Zigbee сведён в основную прошивку** (Фаза 1): собирается **700КБ/20% flash, RAM 11%**, залито, **спарено со Станцией Midi** — 5 устройств, эффекты переключаются цветом Fara, локальный OLED работает параллельно.

## Сборка / прошивка / тест

```
arduino-cli compile --fqbn "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M" SmartLadaRevC
arduino-cli upload  --fqbn "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M" -p /dev/cu.usbmodem2101 SmartLadaRevC
```
- **Правило: компилировать до заливки.** Порт `/dev/cu.usbmodem*` иногда «busy» на ре-энумерации — повторить upload.
- **Serial:** логи Zigbee-стека идут на **UART0 (J8), НЕ на USB-CDC**. На USB видны только `Serial.printf` (`ZB Fara/lamp ...`). Захват (arduino-cli monitor без TTY сразу выходит): `stdbuf -oL cat /dev/cu.usbmodem2101 > log &` — держать порт свободным при заливке (kill перед upload).
- Zigbee-бондинг в разделе `zb_storage` переживает перепрошивку → **заново парить не надо**. `MAGIC` в config уже 0x57; при следующем bump'е настройки сбросятся раз.

## Модель управления (что делает прошивка)

Единый источник состояния — **`config::s`**; пишут в него меню И Zigbee (last-writer-wins); луп: `fx::compute → channels::write`.

- **5 Zigbee-эндпоинтов:** EP10-13 = 4 лампы (`ZigbeeDimmableLight`, on/off+level), EP14 «Fara» (`ZigbeeColorDimmableLight`): level→`master`, hue→эффект (`hueToMode`), on/off→`faraOn`.
- **Компоновщик `fx::compute`:** Fara вкл + эффект → анимация×master (все каналы); Fara вкл + static → поканально `staticBri*master` (гейт `lampOn`); **Fara выкл → passthrough** (лампы по своим уровням, без master).
- **Эвристики в `src/net/zigbee.cpp`** (различают групповую операцию от одиночного касания, т.к. Яндекс-группа шлёт level всем 5 EP разом):
  - Fara меняет эффект **только на смену цвета** (hue/белизна), не на level.
  - Касание лампы → `mode=0` (static) **только если одиночное** (`millis()-s_lastFaraMs > GROUP_WINDOW_MS=300`).

## Открытые хвосты (следующие шаги, по приоритету)

1. **report-back board→Alice (Фаза 2) — ГЛАВНОЕ.** Сейчас нет обратной синхры: плитка Fara в апе не отражает переход в static, локальные правки яркости/режима Алисе не видны. Нужно репортить атрибуты эндпоинтов при изменении `config::s` из меню/эвристик (API `Zigbee*Light` сеттеры + report).
2. **UX группировки vs Fara-как-мастер.** Research-план советует НЕ делать Яндекс-группу (мастерить через Fara EP14). В static-режиме групповая яркость даёт **двойное затухание** (и `staticBri`, и `master` меняются группой). Решить: Fara-как-мастер (рекомендация) или переделка эндпоинтов под групповой мастер.
3. **PD-gating + debug-байпас (Фаза 4):** PG=IO10 (LOW=12В есть) гейтит каналы; пункт меню «Force outputs» — открывать транзисторы на 5В для визуального дебага без ламп. VBUS-sense=IO4 (ADC).
4. **Эффекты на движок `fx::` (Фаза 5)** — сейчас hue квантуется по существующим 4 эффектам.
5. **WiFi/веб (Фаза 6)** — веб в стиле cineink (QR-провижн, smartlada.local); WiFi+Zigbee coexistence на C6 (HW умеет, софт-риск) + переключатель в меню как fallback. **ВНИМАНИЕ:** та же ловушка case-insensitive FS — свой `wifi.h` vs `<WiFi.h>` → библиотеку угловыми скобками.

## Карта изменённых файлов

- `src/config/config.{h,cpp}` — +`lampOn`,`faraOn`; MAGIC 0x57; дефолты `0x0F`/`1`.
- `src/fx/effects.{h,cpp}` — новая сигнатура `compute(... faraOn, lampOn ...)` + компоновщик/passthrough; гард `MAX_FX_PARAMS`.
- `src/net/zigbee.{h,cpp}` — **НОВЫЙ** модуль: 5 EP, коллбэки→`config::s`, эвристики, `begin/update/connected/consumeDirty/factoryReset`.
- `src/ui/menu.{h,cpp}` — `notifyExternalChange()`, factory-reset→`zb::factoryReset()`, `screenCursor[]`, гард `MODE_MAX`. Мёртвые пункты меню `WiFi`/`WiFi QR` пока оставлены (под Фазу 6).
- `src/channels/channels.{h,cpp}` — RES_BITS=10, комментарии-факты (нет gate-пулдауна на revC).
- `SmartLadaRevC.ino` — `#include <Zigbee.h>` (детект либы) + `zb::begin/update`, новый `compute`, zigbee-FQBN в шапке.

## Уроки-ловушки (в памяти)
- macOS case-insensitive FS: `#include "Zigbee.h"` из `src/net/` матчит свой `zigbee.h` → библиотеку через `<...>`.
- LEDC C6 от 40МГц: `freq_max = 40e6/2^RES_BITS`.
- Переиспользуемый WROOM тащит старый NVS; центральный EPAD WROOM НЕ паяли (земля через pin1/pin28).
