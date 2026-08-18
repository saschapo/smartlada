# RevBDisplayTest

Bring-up-тест платы **SmartLada Rev B**: проверка OLED-модуля и 4 кнопок
(разъём `J4` "OLED AND BUTTONS"). Пины взяты как **уже разведено на плате**
(сверено по нетлисту `custom_pcb/smartlada_revB`).

## Подключение

| Модуль | Сигнал | ESP32-C6 |
|--------|--------|----------|
| GND    | GND    | GND      |
| VCC    | +3V3   | 3V3      |
| SCL    | I2C SCL| GPIO18   |
| SDA    | I2C SDA| GPIO19   |
| K4 (*) | KEY4   | GPIO23   |
| K3 (#) | KEY3   | GPIO22   |
| K2 (v) | KEY2   | GPIO21   |
| K1 (^) | KEY1   | GPIO20   |

Кнопки замкнуты на GND -> `INPUT_PULLUP`, нажатие = LOW.
Дисплей: WEO012864V, контроллер SSD1315 (совместим с SSD1306), I2C `0x3C`.

## Что делает

- На OLED — по строке на кнопку: метка, символ и живой счётчик нажатий;
  удерживаемая кнопка подсвечивается (инверсия). В шапке — heartbeat 0..9
  (доказывает, что цикл обновления не завис).
- Каждое нажатие дублируется в Serial @115200.
- Если OLED не найден — тест продолжает опрашивать кнопки и пишет в Serial,
  чтобы можно было проверить разводку кнопок отдельно.

## Сборка/заливка

```
arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc sketches/RevBDisplayTest
arduino-cli upload  --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc -p <PORT> sketches/RevBDisplayTest
```

Библиотеки: `Adafruit SSD1306`, `Adafruit GFX`.

Важно: `CDCOnBoot=cdc` нужен, чтобы вывод `Serial` шёл в нативный USB-порт
(usbmodem). Без него по умолчанию `Serial` уходит в аппаратный UART0, и в
USB-мониторе вывода скетча не видно (виден только лог ROM-бутлоадера).

Проверено на живой плате (dev-board C6 + провода): OLED отвечает на `0x3C`,
все 4 кнопки (K1..K4) отрабатывают.
