# SmartLada Rev B — подготовка к производству (pselectro, 4 слоя)

Аналог [revA FABRICATION_ORDER.md](FABRICATION_ORDER.md), адаптирован под **4 слоя**.
Состояние: плата разведена, DRC 0/0 (кроме 2 принятых: тонкий текст легенды, рамка OLED у M3).
Стек и правила — [спека §3/§13](smartlada_rev_b_spec.md), справочник завода
[pselectro_tech_limits.md](pselectro_tech_limits.md), BOM — [smartlada_rev_b_bom.md](smartlada_rev_b_bom.md).

## 1. Сводка платы для завода

| Параметр | Значение |
|---|---|
| Габарит | 92 × 80 мм, прямоугольник |
| Слои | **4** (F.Cu / In1.Cu=GND / In2.Cu=+3V3 / B.Cu) |
| Стек-ап | `Типовой/4/1,5/35/FR/вн 0.71 (35/35)` — **1.57 мм**, медь 35µm все слои |
| Маска | **чёрная**, обе стороны |
| Шелк | белый, **обе стороны** (низ — легенда GPIO, зеркальна) |
| Финиш | HASL (ПОС-63; плата 1.5 мм > 1 мм — ок) |
| Min провод/зазор | 0.20 / 0.20 (класс 4) |
| Min отверстие | 0.30 мм |
| Поясок | 0.20 (стандарт 35µm) |
| Контур | **скрайбирование**, медь-край 0.5 мм |
| NPTH | 4× M3 (3.2 мм) + фиксатор F1 (2.7 мм) — **немета** |
| Электроконтроль | да |
| Кол-во | 4 (фаб-минимум ~5, уточнить) |

## 2. Gerber-экспорт (KiCad 10: File → Fabrication Outputs → Gerbers)

**Слои в набор (4-слойка!):**
- **F.Cu, In1.Cu, In2.Cu, B.Cu** — вся медь (2 внешних + 2 плоскости)
- **F.Mask, B.Mask** — маска
- **F.Silkscreen, B.Silkscreen** — шелк (низ несёт легенду!)
- **Edge.Cuts** — контур

НЕ включать: F/B.Fab, F/B.Courtyard, F/B.Paste (ручная пайка, трафарет не нужен), User.*.

**Опции (Plot):**
- Format **Gerber**, Units **mm**, Precision **4.6**
- **Снять** «Use extended X2 format» и «Include netlist attributes» (pselectro просит RS-274X)
- **Снять** «Plot border and title block»
- **Поставить** «Subtract soldermask from silkscreen»
- Mirror **off**, Drill marks **none**
- **«Use drill/place file origin»** (aux origin) — общий ноль для gerber и сверловки

## 3. Drill (Fabrication Outputs → Drill Files)

- Format **Excellon**, Units **mm**, десятичный
- **Снять** «Merge PTH and NPTH» — раздельно (M3 и F1-фиксатор = немета)
- Origin — тот же aux origin
- Сгенерировать **drill map** + **report** (положить в набор)

## 4. Чек-лист во внешнем вьювере (обязательно до отправки)

- [ ] **Edge.Cuts** — один замкнутый прямоугольник 92×80, без разрывов
- [ ] **In1.Cu (GND) и In2.Cu (+3V3)** присутствуют и залиты
- [ ] **⚠ Вырез плоскостей под антенной U1 виден на In1 И In2** (это чинили — критично для RF)
- [ ] **EPAD U1** — массив via + SMD-пады на GND
- [ ] Плоскости — сплошные, не порваны на острова
- [ ] **B.Silkscreen** — легенда GPIO присутствует и **зеркальна** (читается с обратной стороны)
- [ ] **NPTH drill**: 4× M3 (3.2) + F1-фиксатор (2.7) — в NPTH-файле, НЕ в PTH
- [ ] Маска раскрыта на всех падах; шелк не на голой меди
- [ ] Нет лишних слоёв (Paste/Fab отсутствуют)
- [ ] Drill report: диаметры сходятся, PTH ≥ 0.30, NPTH 2.7/3.2

## 5. Пакет и заявка

Zip: gerbers + PTH.drl + NPTH.drl + drill map + report → `smartlada_revB_gerbers_v1.zip`.

**Поля заявки pselectro:** 4 слоя / FR4 **1.57 мм** / стек `Типовой/4/1,5/35/вн 0.71` /
медь 35µm все слои / маска **чёрная 2 стороны** / шелк белый 2 стороны / HASL / класс 4 /
min hole 0.3 / контур **скрайб** / электроконтроль да / вход RS-274X + Excellon /
контур-слой Edge.Cuts / 4× M3 (3.2) и F1 (2.7) — **немета** / сборка нет.

## 6. Заказы (параллельно)

- **Модуль U1 ESP32-C6-WROOM-1-N16** — AliExpress, СЕЙЧАС (long-lead), +2 запас.
- **Platan-корзина** — по [BOM](smartlada_rev_b_bom.md); хвосты: TPS MIN 1 (5), Q1-4 MIN 13 (16),
  J1 DG127 MIN 10, 0R (R5/R6) — перемычкой/Ali.
