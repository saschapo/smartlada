# SmartLada — умный задний фонарь ВАЗ-2106

Прошивки и документация для проекта «умный светильник» из штатного заднего фонаря
ВАЗ-2106: PWM-диммирование 4 ламп (стоп / задний / поворот / габарит) с калибровкой,
web-управлением и динамическими режимами.

## Структура репозитория

| Папка / файл | Что это |
|--------------|---------|
| [`SmartLadaC6/`](SmartLadaC6/) | **Актуальная прошивка** на ESP32-C6 (MuseLab nanoESP32-C6). Аппаратный PWM (LEDC), Wi-Fi AP + web-UI, кнопка BOOT, 5 тест-режимов + DRIVE, NeoPixel-индикация. Далее — Zigbee. |
| [`SmartLada/`](SmartLada/) | Первый стенд bring-up на ESP8266 (завершён 2026-07-06). PWM `analogWrite`, OLED, калибровка ламп. База, портированная на C6. |
| [`PROJECT_2106_SMART_LIGHT.md`](PROJECT_2106_SMART_LIGHT.md) | Контекст проекта: решения, железо, архитектура, план (handoff). |
| [`VAZ2106_Smart_Lamp_Context.md`](VAZ2106_Smart_Lamp_Context.md) | Ранний контекст-документ (факты/выводы/гипотезы/решения). |
| [`TASK_smartlada_bench_esp8266.md`](TASK_smartlada_bench_esp8266.md) | ТЗ стенда ESP8266 (выполнено). |
| [`schematic_2106_ESP32.md`](schematic_2106_ESP32.md) | Схема подключения (лампы накаливания + ESP32-C6). |

## Железо (текущий стенд C6)

- **Контроллер:** ESP32-C6 (nanoESP32-C6), каналы на `GPIO0..3` — см. [board doc](SmartLadaC6/docs/board_nanoESP32-C6.md).
- **Силовой ключ:** 4× модуль D4184 (N-MOSFET, low-side) — см. [схему ключа](SmartLadaC6/docs/mosfet_switch_D4184.md).
- **Лампы:** R10W 12V BA15S ×4 (40 Вт / 3.33 А). **БП:** Ecola B2L080ESB (DC 12 В, 80 Вт).

## Сборка и прошивка (C6)

```sh
FQBN="esp32:esp32:esp32c6:FlashSize=16M,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile -b "$FQBN" SmartLadaC6
arduino-cli upload  -b "$FQBN" -p /dev/cu.usbmodemXXXX SmartLadaC6
```

Подробности, карта каналов, режимы и предупреждения по силовому железу — в
[`SmartLadaC6/README.md`](SmartLadaC6/README.md).
