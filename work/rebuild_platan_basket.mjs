import fs from "node:fs/promises";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const dir = "/Users/saschapo/Documents/Claude/Projects/VAZ smart light/custom_pcb/smartlada_revA/BOM_work/revA";
const source = `${dir}/260721_platan_basket.xls`;
const outputPath = `${dir}/260721_platan_basket_кириллица.xlsx`;

const raw = await fs.readFile(source);
const text = new TextDecoder("windows-1251").decode(raw).replace(/\r/g, "");
const parsed = text.trim().split("\n").map(line => line.split("\t"));
const data = parsed.slice(1).map((r, i) => [
  i + 1,
  String(r[0]),
  r[1],
  Number(r[2]),
  Number(r[3]),
  Number(r[4]),
]);

const wb = Workbook.create();
const basket = wb.worksheets.add("Корзина Платан");
const check = wb.worksheets.add("Проверка корзины");

basket.getRange("A1:F1").merge();
basket.getRange("A1").values = [["Корзина Platan.ru — восстановленная кириллица"]];
basket.getRange("A2:F2").merge();
basket.getRange("A2").values = [["Источник: 260721_platan_basket.xls; исходный файл является TSV в кодировке Windows-1251, а не бинарным XLS."]];
basket.getRange("A4:F4").values = [["№", "Артикул Platan", "Наименование", "Количество", "Цена, ₽/шт.", "Сумма, ₽"]];
basket.getRange(`A5:F${4 + data.length}`).values = data;
basket.tables.add(`A4:F${4 + data.length}`, true, "PlatanBasket");
basket.getRange(`F5:F${4 + data.length}`).formulas = data.map((_, i) => [`=D${i + 5}*E${i + 5}`]);
basket.getRange("E23").values = [["Итого:"]];
basket.getRange("F23").formulas = [[`=SUM(F5:F${4 + data.length})`]];

basket.getRange("A1:F1").format = {fill:"#17324D",font:{bold:true,color:"#FFFFFF",size:16}};
basket.getRange("A1:F1").format.rowHeight = 30;
basket.getRange("A2:F2").format = {fill:"#DCEAF7",font:{italic:true,color:"#17324D"},wrapText:true};
basket.getRange("A4:F4").format = {fill:"#2F6B8A",font:{bold:true,color:"#FFFFFF"},wrapText:true};
basket.getRange(`A5:F${4 + data.length}`).format.verticalAlignment = "top";
basket.getRange(`C5:C${4 + data.length}`).format.wrapText = true;
basket.getRange(`D5:D${4 + data.length}`).format.numberFormat = "#,##0";
basket.getRange(`E5:F23`).format.numberFormat = "#,##0.00";
basket.getRange("E23:F23").format = {fill:"#DCEAF7",font:{bold:true,color:"#17324D"}};
[6,18,82,13,16,16].forEach((w,i)=>basket.getRangeByIndexes(0,i,23,1).format.columnWidth=w);
basket.getRange("A1:F23").format.autofitRows();
basket.freezePanes.freezeRows(4);
basket.showGridLines = false;

const checks = [
  ["Статус", "RefDes / позиция", "Что в корзине", "Требование Rev A", "Действие"],
  ["ПРИНЯТО", "C2", "GRM21BR71E104KA01L: 100 нФ, 25 В, X7R, 0805", "C2 стоит на защищённой шине +12 V; 50 В было консервативным запасом", "Одобрено для регулируемого комнатного БП 12 В. Не использовать это решение для автомобильной сети."],
  ["J2 — ИЗМЕНЕНИЕ ПРИНЯТО", "J2", "DG308-2.54-05P: 5 контактов, шаг 2,54 мм, 6 А UL / 8 А IEC", "J2.1 несёт 3,33 А; J2.2…J2.5 примерно по 0,83 А", "Новый клеммник электрически подходит. Footprint и разводка PCB должны быть переделаны под шаг 2,54 мм и повторно проверены DRC."],
  ["OK", "C1", "EEUFR1E101: 100 µF, 25 В, Ø6,3×11,2 мм, шаг 2,50 мм", "100 µF, 25 В, THT radial D6.3, шаг 2,50 мм", "Точно соответствует электрическим требованиям и посадочному месту."],
  ["ПРИНЯТО С РИСКОМ", "F1 держатель", "S1052-1: 5x20, 4 А, 250 В; корпус 24×10×11 мм; выводы 22,4 мм / 1,2 мм", "Текущий footprint: центры 22,5 мм, отверстия 1,3 мм", "Оставляем footprint. Разница шага 0,1 мм несущественна; возможна плотная посадка из-за малого зазора в отверстиях. При необходимости аккуратно расширить отверстия вручную."],
  ["OK", "C5", "GRM188R72A104KA35D: 100 нФ, 100 В, X7R, 0603", "100 нФ, 0603; рекомендовано ≥16 В", "Подходит по ключевым параметрам."],
  ["OK", "C3, C4", "HLC1210X7R106K500N: 10 µF, 50 В, X7R, 1210", "10 µF, 50 В, 1210", "Подходит; учесть снижение ёмкости под DC bias."],
  ["OK", "C6, C7", "GRT21BR61E226ME13L, 0805, 22 µF", "22 µF, ≥16 В, 0805", "По маркировке подходит; перед заказом подтвердить напряжение в карточке товара."],
  ["OK", "D1", "SS14-E3/61T: 1 А, 40 В, SMA / DO-214AC", "SS14, SMA", "Точная корпусная и электрическая замена."],
  ["OK", "U1", "TPS54202DDCR, TSOT-23-6", "TPS54202DDCR, SOT-23-6", "Подходит."],
  ["OK", "Q1…Q4 / Q5", "AOD4184A и AOD4185", "Те же модели, TO-252/DPAK", "Модели совпадают; количество с большим запасом."],
  ["OK", "R3 / Rg / 100 kΩ", "13,7 kΩ; 100 Ω; 100 kΩ, все 0805", "Соответствующие номиналы 0805", "Подходят."],
  ["ИТОГ", `${data.length} строк корзины`, "Кириллица восстановлена; суммы строк и итог пересчитываются формулами", "Критических несовпадений в выбранных компонентах не осталось", "Перед заказом завершить/проверить изменение J2 в KiCad и выполнить DRC. F1 проверить физической примеркой на первой плате."],
];
check.getRange(`A1:E${checks.length}`).values = checks;
check.tables.add(`A1:E${checks.length}`, true, "BasketCheck");
check.getRange("A1:E1").format = {fill:"#17324D",font:{bold:true,color:"#FFFFFF"}};
check.getRange(`A2:E${checks.length}`).format.wrapText = true;
[17,22,50,42,54].forEach((w,i)=>check.getRangeByIndexes(0,i,checks.length,1).format.columnWidth=w);
check.getRange(`A1:E${checks.length}`).format.autofitRows();
check.getRange(`A2:A${checks.length}`).conditionalFormats.add("containsText",{text:"ОШИБКА",format:{fill:"#FECACA",font:{bold:true,color:"#991B1B"}}});
check.getRange(`A2:A${checks.length}`).conditionalFormats.add("containsText",{text:"ОТСУТСТВУЕТ",format:{fill:"#FED7AA",font:{bold:true,color:"#9A3412"}}});
check.getRange(`A2:A${checks.length}`).conditionalFormats.add("containsText",{text:"ПРОВЕРИТЬ",format:{fill:"#FEF3C7",font:{bold:true,color:"#92400E"}}});
check.getRange(`A2:A${checks.length}`).conditionalFormats.add("containsText",{text:"ПРИНЯТО",format:{fill:"#FEF3C7",font:{bold:true,color:"#92400E"}}});
check.getRange(`A2:A${checks.length}`).conditionalFormats.add("containsText",{text:"OK",format:{fill:"#DCFCE7",font:{bold:true,color:"#166534"}}});
check.freezePanes.freezeRows(1);
check.showGridLines = false;

console.log((await wb.inspect({kind:"table",range:`Корзина Платан!A4:F${4+data.length}`,include:"values,formulas",tableMaxRows:25,tableMaxCols:6,maxChars:12000})).ndjson);
console.log((await wb.inspect({kind:"match",searchTerm:"#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A",options:{useRegex:true,maxResults:100},summary:"formula errors"})).ndjson);
for (const [name,file] of [["Корзина Платан","/private/tmp/platan_basket.png"],["Проверка корзины","/private/tmp/platan_check.png"]]) {
  const png = await wb.render({sheetName:name,autoCrop:"all",scale:1,format:"png"});
  await fs.writeFile(file,new Uint8Array(await png.arrayBuffer()));
}
const out = await SpreadsheetFile.exportXlsx(wb);
await out.save(outputPath);
