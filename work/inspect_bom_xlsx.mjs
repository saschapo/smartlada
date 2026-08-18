import fs from "node:fs/promises";
import { FileBlob, SpreadsheetFile } from "@oai/artifact-tool";

const source = process.argv[2] || "/Users/saschapo/Documents/Claude/Projects/VAZ smart light/custom_pcb/smartlada_revA/BOM_work/revA/SmartLada_RevA_BOM_закупка.xlsx";
const prefix = process.argv[3] || "source";
const wb = await SpreadsheetFile.importXlsx(await FileBlob.load(source));
console.log((await wb.inspect({kind:"sheet",include:"id,name",maxChars:3000})).ndjson);
console.log((await wb.inspect({kind:"table",range:"Закупка!A4:M22",include:"values,formulas",tableMaxRows:22,tableMaxCols:13,maxChars:12000})).ndjson);
console.log((await wb.inspect({kind:"formula",sheetId:"Закупка",range:"G5:G22",maxChars:5000,options:{maxResults:30}})).ndjson);
console.log((await wb.inspect({kind:"match",searchTerm:"#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A",options:{useRegex:true,maxResults:100},summary:"formula errors"})).ndjson);
for (const name of ["Закупка","Проверка BOM","Магазины"]) {
  const png = await wb.render({sheetName:name,autoCrop:"all",scale:1,format:"png"});
  await fs.writeFile(`/private/tmp/${prefix}_${name.replaceAll(" ","_")}.png`,new Uint8Array(await png.arrayBuffer()));
}
