# SmartLada Rev C — Session Handoff (2026-09-06)

Большая сессия на живой плате revC (USB `/dev/cu.usbmodem401101`, питание USB / 12В PD).
Прошивка `SmartLadaRevC/`. Сборка: `arduino-cli` FQBN
`esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=16M`.
Текущая версия **1.0.6-revC**, залита, работает (Zigbee + меню). Правило: компилировать до заливки.

## Сделано за сессию (всё протестировано на железе)

- **Багфиксы Zigbee-модели** (research/smartlada_revc_zigbee_alice_test.md):
  - B1: групповое диммирование больше не зажигает выключенные лампы (убран `lampOn|=` на Level).
  - B3: локальное меню авторитетно (пишет `mode`/`staticBri` напрямую).
  - **Модель v2 (финал):** `mode` один определяет слой. EP14 «Fara»: выкл/белый → static (белый =
    fan-out уровня в `staticBri`), цвет → эффект (hue), level → яркость эффекта. Static без множителя
    (нет двойного затухания). Мастер = uniform fan-out (пропорциональный отвергнут: при 1 включённой
    лампе группу не отличить). Убраны эвристики GROUP_WINDOW/lone, «касание→static».
- **B5 — ребут при холодном старте на 100%: РЕШЁН boot soft-start** (плавный разгон ламп 1.5с на
  сплэше). Причина — инраш холодных нитей на общий рельс +12V/VBUS (=вход бака ESP). Затворы притянуты
  HW (Rpd 100k) — README про «нет пулдауна» был неверен, исправлен.
- **Экспоненциальное сглаживание** яркости (`channels::write`), `softMs`=τ (дефолт 250мс). Эффекты
  идут с малым τ (FX_SMOOTH_MS=20), static — с τ пользователя.
- **Фаза 4 PD-gating:** нет ~12В (VBUS<7В, IO4 ADC) → каналы 0 + «NO 12V»; Settings→**Force Out**
  (toggle+confirm) байпас на 5В. Модуль `src/power/`. Force Out сбрасывается на ребуте.
- **Report-back board→Alice (Фаза 2, полностью):** `reportState()` в zigbee.cpp (дебаунс 400мс). Лампы
  EP10-13 via `setLight()`. **EP14** via `setLightColor` с guard `s_reporting` (сеттеры дёргают
  onFaraHsv). EP14 репортит ФАКТИЧЕСКИЙ hue Алисы (не центр — иначе цвет прыгал). Static → только
  яркость (белый sat=0 багал ап Алисы фантомным цветом).
- **Карта каналов по функциям:** ch0=turn(IO0), ch1=marker(IO1), ch2=reverse(IO2), ch3=stop(IO3);
  `PINS={0,1,2,3}`; EP10-13=Turn/Marker/Reverse/Stop. (По нетлисту IO0/IO1 → Fastons OUT1/OUT0.)
- **Эффекты (Фаза 5):** Breathe, **Turn** (только поворотник 1.5Гц), Chase, Fade, **Drive** (FSM
  вождения, порт из SmartLadaC6/src/anim). Цвет→эффект: белый=Static, красн=Breathe, зел=Turn,
  голуб=Chase, син=Fade, розов=Drive.
- **Меню (реорг):** Settings = Display · Zigbee · WiFi(стаб) · Force Out · **System**. System =
  Statistics(диагностика) · **Update Over BLE** · Factory Reset. Стрелки «›» у drill-down. Экран
  **Zigbee**: state/PAN/ch/addr + **LQI/RSSI** parent (`esp_zb_nwk_get_next_neighbor`) + Re-pair.
  Экран Brightness: каналы `1 Turn / 2 Marker / 3 Reverse / 4 Stop / Master`, бары параметризованы.

## BLE OTA — РАБОТАЕТ end-to-end (главный итог сессии)

Заливка прошивки по BLE с моего Mac (без USB), устройство на 12В.
- **Устройство:** `src/net/bleota.{h,cpp}` — NimBLE GATT-сервис (UUID `5ada0a70-...-0001`): CTRL
  (START токен+size / FINISH / ABORT), DATA (write-no-response, поток), STATUS (notify: state+recv,
  каждые 4КБ = flow-control ACK). Пишет в свободный OTA-слот (`esp_ota_*`), reboot. Отдельная
  **writer-task** (флеш вне BLE-колбэка). Токен `0x5A5AA5A5`.
- **Хост:** `tools/ble_ota.py` (bleak). **Оконный контроль** (write-no-response + окно 16КБ по
  notify-ACK) — БЕЗ этого линк рвался (write-with-response ронял на ~40%, флуд no-response терял
  пакеты). Запуск: `python tools/ble_ota.py <bin>`. ~14 КБ/с, ~80с на 1.1МБ.
- **Режим A (принят):** BLE поднимается ТОЛЬКО в OTA-режиме (Zigbee НЕ стартует → радио свободно →
  надёжно). Вход: **System → Update Over BLE → Yes** → ставит RTC-флаг `g_otaReq=OTA_REQ_MAGIC`,
  ребут; setup() видит флаг → `g_otaMode`, только `bleota::begin()`, экран «BLE OTA / Zigbee paused».
  Любая кнопка = выход в норму. После заливки — авто-reboot в нормальный режим.
- В нормальном режиме BLE НЕ рекламируется (чисто Zigbee, без coexistence).
- **Coexistence:** BLE+Zigbee компилируются вместе (sdkconfig C6: BT_NIMBLE + ZB + SW_COEXIST =y,
  32% flash) и работают, НО объёмная BLE-передача при живом Zigbee рвёт линк — поэтому Режим A.

### Хост-настройка (важно для след. сессии)
- BLE из shell требует **Bluetooth-разрешение для Claude.app** (System Settings → Privacy → Bluetooth)
  + **перезапуск Claude** (иначе SIGABRT). Проверено — работает.
- venv: `uv venv /tmp/blechk --python 3.12 && uv pip install --python /tmp/blechk/bin/python bleak`.

## Открытые хвосты (след. шаг)

1. **Вход в OTA-режим ЧЕРЕЗ МЕНЮ не сработал у Саши** (BLE не поднялся; проверяли через TEST FORCE в
   .ino — сам механизм OK). Отладить путь System→Update Over BLE→Yes: рендер `renderFwConfirm`,
   `SC_FW` handler (`g_otaReq=OTA_REQ_MAGIC; ESP.restart()`), RTC extern `g_otaReq`, проверку в setup().
   Возможно Саша просто не дошёл до пункта / не нажал Yes — уточнить + снять serial `BLE OTA mode`.
2. Проверить, влезает ли «Update Over BLE» в строку System (рядом со стрелкой).
3. **WiFi OTA для юзеров** (бэклог): SoftAP + web-uploader (`Update.h`) — партиции dual-OTA готовы.
   BLE-заливка юзером с телефона непрактична — веб удобнее.
4. Мелочи: «Reverse»/«100%» в Brightness — проверить, не поджато ли; report-back — обновляются ли
   плитки сразу или по переоткрытию (квирк Яндекса).

## Ключевые файлы
- `SmartLadaRevC.ino` — setup/loop, OTA-режим (`g_otaMode`, RTC `g_otaReq`), boot soft-start, PD-gate.
- `src/net/bleota.{h,cpp}` — BLE OTA. `tools/ble_ota.py` — хост-клиент.
- `src/net/zigbee.{h,cpp}` — 5 EP, report-back, status (PAN/LQI). `src/fx/effects.cpp` — эффекты+Drive.
- `src/ui/menu.cpp` — меню (System-подменю, Zigbee/Force/OTA-confirm экраны, showUpdating/showOtaIdle).
- `src/power/` — PD-gating/VBUS/temp. `research/smartlada_revc_zigbee_alice_test.md` — тест-журнал.
</content>
