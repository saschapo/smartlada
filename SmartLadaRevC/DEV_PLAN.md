# SmartLada Rev C — план разработки прошивки

Дата: 2026-09-04. Железо revC поднято и провалидировано (питание PD 12В + buck 3.3В, USB,
OLED, кнопки, I2C, 4 канала — гоняли 35 Вт реальной нагрузки, стабильно, ничего не греется).
LEDC зафиксирован на 10 бит (см. [[esp32c6-ledc-freq-ceiling]]).

Цель: свести в основную прошивку `SmartLadaRevC` сетевое управление (Zigbee + опц. WiFi/веб)
поверх существующего локального (OLED-меню + кнопки), с единой моделью состояния.

## Целевая архитектура

**Единый источник состояния — `config::s`** (mode, master, staticBri[4] + калибровка).
Все «писатели» правят его, луп не меняется:

```
loop: buttons -> menu -> [zigbee/wifi callbacks уже записали config::s]
      fx::compute(mode, now, master, staticBri, out[4])   // компоновщик
      channels::write(now, out)                            // slew -> gamma -> LEDC
      menu::render()
```

Писатели состояния: **меню (кнопки), Zigbee, WiFi/веб.** Приоритет — **last-writer-wins**.
Двусторонняя синхра: изменил меню → репорт в Zigbee-атрибуты (и в веб); пришло из Zigbee/веб →
дёрнуть `menu::render()`. `fx::compute` уже делает компоновку (Static→поканально×master,
анимация→кадр×master), поэтому компоновщик из `RevCZigbeeFx` схлопывается в существующий `fx`.

## Zigbee-модель (архитектура подтверждена, см. research/smartlada_zigbee_effects_plan.md)

Один узел = 5 эндпоинтов (EP = логическое под-устройство; Станция видит их как 5 устройств):

| EP | Тип | Роль |
|----|-----|------|
| 10..13 | `ZigbeeDimmableLight` | 4 лампы (Turn/Brake/Marker/Reverse): on/off + level |
| 14 | `ZigbeeColorDimmableLight` «Fara» | on/off=питание фары; level=**master**; hue=**селектор эффекта** |

Правило приоритета (гвоздями):
```
master = Fara.on ? Fara.level/254 : 1.0
Fara.on && насыщенный цвет  -> out[i] = кадр_эффекта[i] * master   (hue -> эффект)
иначе (Fara off/белый)      -> out[i] = лампа[i] * master           (PASSTHROUGH: EP10..13 индивидуально)
```
**Passthrough** = когда Fara выключена, 4 лампы управляются независимо своими EP10-13 (без мастера/эффекта). Оставляем.

Готовая реализация всей этой логики (плоская, стендовая) — `sketches/RevCZigbeeFx/RevCZigbeeFx.ino`;
переносим в модуль. Build: `ZigbeeMode=ed, PartitionScheme=zigbee_8MB, FlashSize=16M, CDCOnBoot=cdc`.

## WiFi / веб-интерфейс (оставляем, стретч-цель)

Видение: управление и с телефона (веб), и Zigbee, и кнопками/OLED. Веб — в стиле **cineink**
(`/Users/saschapo/Documents/Documents/arduino/260518_cineink/cineink/`): подключение к WiFi по QR,
`smartlada.local` (mDNS) тоже по QR, тёмный `web_view/` (device.css/index.html/app.js).

**WiFi+Zigbee одновременно на C6:** железо умеет (один 2.4ГГц радиотракт, RF-coexistence /
тайм-слайсинг Wi-Fi6 ↔ 802.15.4). Риск — софт (Arduino: Zigbee-стек + WiFi + веб-сервер + LittleFS
+ OLED одновременно, менее обкатано; возможны потери/латентность). **Стратегия:** WiFi и Zigbee —
независимые модули, которые МОГУТ работать вместе, но с **переключателем в меню (WiFi ↔ Zigbee)** как
гарантированным fallback. Coexistence проверяем эмпирически.

## Решённые вопросы

1. Приоритет при одновременном управлении — **last-writer-wins** по `config::s`.
2. Скорость/яркость эффекта — **параметр меню** (Fara.level = master яркость; скорость — таймингами эффекта в меню).
3. Персист состояния в **NVS** — делаем. NVS в ESP-IDF с wear-leveling, на практике (cineink) не изнашивается;
   писать **с дебаунсом** (на «оседании» значения, не каждый кадр). Глянуть NVS/LittleFS-паттерн cineink.
4. Passthrough (Fara off → индивидуальные лампы) — **оставляем**.

## Железные дельты стенд→продукт (учесть в портировании)

| Стенд (nanoC6) | Продукт revC | Действие |
|---|---|---|
| статус NeoPixel **GPIO8** | **IO8 не разведён** | статус сети на **OLED** |
| factory-reset BOOT(GPIO9) | SW2=IO9 + пункт меню | оба |
| — | **PG=IO10** (12В есть, LOW), **VBUS=IO4** (ADC) | PD-gating (Фаза 4) |

## Фазы

- **Фаза 1 — билд/скелет.** Zigbee в билд `SmartLadaRevC` (FQBN выше). Проверить, что `config` NVS
  живёт под zigbee-партицией. Вынести Zigbee в `src/net/zigbee.{h,cpp}` (тонкий слой). RF-риск от
  пропущенного EPAD WROOM — проверяем бесплатно при первом паринге (LQI/дальность).
- **Фаза 2 — интеграция состояния.** Коллбэки Zigbee → `config::s`; изменения меню → report в Zigbee.
  `fx::compute` как компоновщик. Карта hue→индекс эффекта (общая для hue-коллбэка и пункта «Mode»).
- **Фаза 3 — меню под сеть.** Пункты `WiFi`/`WiFi QR` **оставить/доразвить** (провижн по QR, smartlada.local).
  Добавить «Zigbee» (статус joined/PAN/LQI, Pair/Re-pair) и переключатель **WiFi ↔ Zigbee**.
  Статус сети на OLED (вместо NeoPixel). Factory-reset расширить на `Zigbee.factoryReset()`.
- **Фаза 4 — PD-gating + debug-байпас.** Читать PG(IO10): нет 12В → каналы в 0 + «No 12V» на экране.
  **Пункт меню «Force outputs (debug)»** — открывать транзисторы даже на 5В для визуального дебага по
  индикаторным LED без ламп. VBUS(IO4 ADC) — опц. на экран.
- **Фаза 5 — эффекты.** 2-3 стартовых (blink/chase/fade уже есть) на движок `fx::`; hue→эффект.
- **Фаза 6 — WiFi/веб + персист.** Веб-UI из cineink (dark view, QR-провижн, mDNS). NVS-персист
  состояния ламп с дебаунсом. Имена/rejoin (T-пункты чеклиста research/).

## Риски

- **RF с пропущенным EPAD WROOM** — держим в голове, проверяем при первом паринге.
- **WiFi+Zigbee coexistence** на C6 под Arduino — главный софт-риск; fallback = переключатель в меню.
- Гетерогенный Zigbee-узел (4×0x0101 + 1×0x0102) — приёмка Станцией (чеклист research/, T3).
- `config` NVS под zigbee-партицией; рост прошивки (16М флеш — запас есть).

## Референсы

- Архитектура Zigbee: `research/smartlada_zigbee_effects_plan.md`, `.../multi_endpoint_summary.md`,
  результаты стенда `research/zigbee_alice_test_results.md`.
- Готовый Zigbee-код: `sketches/RevCZigbeeFx/RevCZigbeeFx.ino` (+ `RevCZigbee4EP` — 4 лампы, вариант A).
- Веб/NVS/лог: cineink `/Users/saschapo/Documents/Documents/arduino/260518_cineink/cineink/`
  (`web_view/`, `firmware/cineink/{web,log,rtc}.cpp`).
- Pin-map ground truth: `README.md`.
