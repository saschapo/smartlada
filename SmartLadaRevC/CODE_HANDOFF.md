# SmartLada Rev C — Firmware Code Handoff

Передача для новой сессии, фокус = **правки кода прошивки**. Всё ниже проверено.

## Что это
Прошивка контроллера ламп фонаря ВАЗ на **ESP32-C6-WROOM-1** (плата revC).
Тонкий `.ino` + слои в `src/*`. OLED-меню (4 кнопки через J4), 4 PWM-канала ламп (OUT0..3,
low-side MOSFET, gate active-HIGH), подключаемые эффекты. Форк от `../SmartLadaRevB`.

## Текущее состояние
- **Pin-remap под revC СДЕЛАН и СОБИРАЕТСЯ** (arduino-cli, esp32c6, 27% flash):
  каналы `PINS={1,0,2,3}` (OUT0=IO1,OUT1=IO0,OUT2=IO2,OUT3=IO3), кнопки `{23,22,21,20}` (KEY1..4),
  I2C 18/19 без изменений; `version.h` → revC.
- **PD-gating НЕ реализован** — это ГЛАВНАЯ задача (см. ниже).
- На железе ещё НЕ проверено (платы в производстве/доставке).

## Сборка / прошивка
```
arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc SmartLadaRevC
arduino-cli upload  --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc -p <PORT> SmartLadaRevC
```
- **CDCOnBoot=cdc обязателен** — Serial идёт по native-USB (иначе Serial молчит).
- Правило: **компилировать до заливки** ([[verify-build-before-upload]]).
- Прошивка через **USB-C (J1)**; для отладки в собранном фонаре — UART-хедер J8 (порт под
  крепёжным цилиндром, плату снимают) + USB-UART донгл.

## Архитектура (loop-поток)
`.ino` (тонкий) вызывает слои:
```
setup(): channels::begin(20000) [форсит выходы в 0 - safety] -> display -> config::load
         -> fx::loadParams -> setFreq/setCalib/setSoftMs -> buttons::begin -> menu::begin
loop():  buttons::poll -> menu::update
         fx::compute(mode, now, master, staticBri, out[4])   // считает уровни каналов 0..255
         channels::write(now, out)                            // slew(soft-start)->gamma->LEDC
         menu::render()
```
Модули: `src/channels` (LEDC PWM + soft-start slew + gamma), `src/input/buttons`
(дебаунс 25мс + accel-repeat), `src/display` (SSD1315 I2C 0x3C, 18/19), `src/fx/effects`
(эффекты), `src/ui/menu`, `src/config` (NVS-настройки, struct `config::s`).

## Проверенный pin-map U1 (ground truth, из нетлиста PCB)
```
Каналы ламп:  OUT0=IO1  OUT1=IO0  OUT2=IO2  OUT3=IO3   (gate active-HIGH, low-side)
PD power-good: IO10  (open-drain, active-LOW: LOW=12В есть; R9=10k pull-up на 3V3)
VBUS-sense:    IO4   (ADC; делитель R7=100k/R8=33k -> Vadc=Vin*33/133; 12В=2.98В)
Native USB:    D-=IO12  D+=IO13   (hardware, CDCOnBoot=cdc)
I2C (OLED J4): SCL=IO18  SDA=IO19
Кнопки (J4):   KEY1=IO23  KEY2=IO22  KEY3=IO21  KEY4=IO20   (active-LOW, INPUT_PULLUP)
UART debug J8: TX=IO16  RX=IO17
Strapping:     BOOT=IO9(SW2)  RESET=EN(SW1)
Индик. LED:    D2..D5 на OUT0..3 (пассивные), D1 на +3V3, D6 на GND — GPIO НЕ нужны
```

## ГЛАВНАЯ ЗАДАЧА: PD-gating (защита от питания «из ноутбука»)
**Смысл:** включать каналы ламп ТОЛЬКО при питании от правильного 12В-PD-источника, а НЕ
когда воткнули в компьютер (там ~5В/30Вт из порта — лампы сожгут бюджет/порт). Дефолт — каналы OFF.

**Логика (реализовать):** каналы разрешены только когда
`PG == LOW` (есть 12В)  **И**  `VBUS ~ 12В` (по ADC)  **И**  нет USB-data-хоста.
Иначе — форсить все 4 канала в 0.

**Куда цеплять (точка интеграции):** в `loop()` между `fx::compute(...out)` и
`channels::write(now, out)` — если питание небезопасно, обнулить `out[]` (soft-start в
`channels::write` сам плавно погасит). Пример:
```cpp
fx::compute(config::s.mode, now, config::s.master, config::s.staticBri, out);
if (!power::safe()) { out[0]=out[1]=out[2]=out[3]=0; }   // PD-gate
channels::write(now, out);
```

**Предлагаемый модуль `src/power/power.{h,cpp}`:**
- `power::begin()` — `pinMode(10, INPUT)` (PG уже подтянут внешним 10k); ADC на IO4
  (`analogSetPinAttenuation(4, ADC_11db)` — 12В=2.98В у верха диапазона).
- `power::poll(now)` — читать PG + VBUS с дебаунсом/гистерезисом.
- `power::safe()` → bool.

**Дизайн-решения для следующей сессии (обдумать):**
1. **USB-host detect** — как отличить «зарядка (только питание)» от «компьютер (enum)».
   На native-USB: устройство *смонтировано/энумерировано* хостом => это компьютер => лампы OFF.
   Проверять через TinyUSB (`tud_mounted()`) или Arduino-USB события / `Serial` (bool = CDC-порт
   открыт хостом). Уточнить, что доступно на C6- core.
2. **VBUS-порог** — 12В=2.98В у нелинейного верха ADC; брать как «presence» (напр. >2.5В =
   есть 12В) с гистерезисом, не как точное измерение. Усреднять несколько отсчётов.
3. **Дебаунс PG** — учесть возможный дребезг/инраш; PG open-drain active-LOW.
4. **Достаточность** — по сути `PG==LOW` уже почти гарантирует 12В; VBUS-ADC и USB-host —
   «пояс+подтяжки». Решить финальную комбинацию (напр. PG обязателен, host-check опционален).
5. **UI/индикация** — показать состояние питания на OLED (напр. иконка «12V OK / no PD»);
   опционально мигать при небезопасном питании.
6. **Гистерезис/лог** — при переходе safe<->unsafe писать в Serial + плавно гасить.

## Bring-up (когда приедут платы) — порядок проверки
1. Питание: 12В-PD -> мерить **TP_3V3 = 3.3В** (buck), **TP_PG** (LOW при 12В), TP_12V, TP_GND.
2. Прошить по USB-C (J1). Serial по CDC (`CDCOnBoot=cdc`).
3. OLED+кнопки через кабель на J4 (I2C 18/19 @0x3C; KEY1-4).
4. Каналы: подать лампы на OUT0-3 (Faston), проверить что GPIO двигают яркость (карта выше).
5. PD-gating: проверить поведение «зарядка (лампы ON)» vs «компьютер (лампы OFF)».
> База revB уже поднималась на C6 (OLED/кнопки/каналы) — см. [[revB-display-buttons-bringup]],
> [[smartlada-revb-ui]].

## Указатели
- PCB-контекст/железо: `custom_pcb/smartlada_revC/SESSION_HANDOFF.md`,
  `smartlada_revC_verification.md` (раздел 6 = pin-map).
- Спека прошивки revB (модульная структура, эффекты, меню): `smartlada-revb-ui` память.
- Память проекта: `smartlada-revC-pcb`, [[modular-firmware-structure]], [[language-usage]],
  [[ascii-only-in-code-output]] (в Serial/коде только ASCII).
