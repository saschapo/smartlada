# RevAZigbeeTest — Zigbee bring-up на собранной плате RevA (ESP32-C6)

Цель этого теста — проверить, что модуль **присоединяется к Zigbee-координатору**
(Яндекс Станция Midi) как стандартная диммируемая лампа и что вкл/выкл + яркость из
приложения Дом с Алисой рулят четырьмя каналами ламп.

Wi-Fi/веб здесь намеренно отключены: у ESP32-C6 один 2.4 ГГц тракт на Wi-Fi и
802.15.4, поэтому первый заход — чистый Zigbee, лог по USB. Совмещение веб-интерфейса
и Zigbee (coexistence) — отдельный следующий шаг.

## Железо (из рабочей прошивки SmartLadaC6)

| Что | Пин | Примечание |
|-----|-----|-----------|
| Каналы ламп | GPIO0..3 | stop/reverse/turn/marker, low-side D4184, active-high PWM (больше duty = ярче). В тесте — один общий свет. |
| NeoPixel-статус | GPIO8 | onboard WS2812 |
| Кнопка BOOT | GPIO9 | active-low; удержание 3 c = factory reset + rejoin |
| USB | — | нативный USB-Serial-JTAG, лог 115200 |

Индикация NeoPixel: **синий мигает** — ищет сеть; **зелёный** — присоединился;
**белый мигает** — identify (Алиса «ищет устройство»); **красный** — идёт factory reset.

> [CRITICAL] С подключёнными лампами и 12 В: общая земля C6 ↔ БП, предохранитель на
> месте. Каналы принудительно OUTPUT+LOW до любой инициализации.

## Сборка и заливка

Модуль — ESP32-C6-WROOM-1-**N8** (8 МБ флеш), поэтому партишн `zigbee_8MB`.

Компиляция:

```bash
arduino-cli compile -b "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=8M" sketches/RevAZigbeeTest
```

Заливка (подставь свой порт из `arduino-cli board list`, обычно `/dev/cu.usbmodemXXXX`):

```bash
arduino-cli upload -b "esp32:esp32:esp32c6:ZigbeeMode=ed,PartitionScheme=zigbee_8MB,CDCOnBoot=cdc,FlashSize=8M" -p /dev/cu.usbmodemXXXX sketches/RevAZigbeeTest
```

Лог:

```bash
arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200
```

В Arduino IDE эквивалент: Tools → Zigbee mode = **Zigbee ED (end device)**,
Partition Scheme = **Zigbee 8MB with spiffs**, USB CDC On Boot = **Enabled**.

## Порядок спаривания

1. Залить скетч, открыть монитор порта — увидеть `Joining network...`.
2. В приложении «Дом с Алисой»: добавить устройство → Умный дом → Зигби-устройство
   (открыть режим сопряжения на Midi).
3. NeoPixel сменит синее мигание на зелёный, в логе — `Joined Zigbee network`.
4. В приложении появится лампа: проверить вкл/выкл и ползунок яркости — каналы должны
   реагировать. Голос: «Алиса, включи <имя лампы>».

## Честные оговорки

- Zigbee-хаб Яндекса исторически придирчив к нестандартным устройствам. Стандартный
  профиль «диммируемая лампа» (On/Off + Level) имеет хорошие шансы, но результат
  эмпирический: может спариться как есть, может — только вкл/выкл, может не принять.
- Если диммер не принимается — откат на `ZigbeeLight` (чистый On/Off) тривиален.
- Если не присоединяется вовсе — снять лог с `DebugLevel=verbose` и режимом
  `ZigbeeMode=ed_debug` для диагностики стека.
