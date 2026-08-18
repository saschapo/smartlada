import { FileBlob, SpreadsheetFile } from "@oai/artifact-tool";

const source = "/Users/saschapo/Documents/Claude/Projects/VAZ smart light/custom_pcb/smartlada_revA/BOM_work/revA/SmartLada_RevA_BOM_закупка.xlsx";
const temp = "/Users/saschapo/Documents/Claude/Projects/VAZ smart light/custom_pcb/smartlada_revA/BOM_work/revA/SmartLada_RevA_BOM_закупка.compat.xlsx";
const wb = await SpreadsheetFile.importXlsx(await FileBlob.load(source));
const sheet = wb.worksheets.getItem("Закупка");
sheet.getRange("H5:H22").dataValidation = {
  rule: {type:"list", values:["Промэлектроника","Платан","ЧИП и ДИП","E-Components","Компэл"]}
};
const output = await SpreadsheetFile.exportXlsx(wb);
await output.save(temp);
