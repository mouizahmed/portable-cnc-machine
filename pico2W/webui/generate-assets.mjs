#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";

function getArg(name) {
  const index = process.argv.indexOf(name);
  if (index === -1 || index + 1 >= process.argv.length) {
    throw new Error(`Missing required argument: ${name}`);
  }
  return process.argv[index + 1];
}

function escapeCString(text) {
  let output = "";
  for (const char of text) {
    switch (char) {
      case "\\":
        output += "\\\\";
        break;
      case "\"":
        output += "\\\"";
        break;
      case "\n":
        output += "\\n\"\n    \"";
        break;
      case "\r":
        break;
      case "\t":
        output += "\\t";
        break;
      default:
        output += char;
        break;
    }
  }
  return output;
}

const srcDir = path.resolve(getArg("--src"));
const outFile = path.resolve(getArg("--out"));

const assets = [
  {
    symbol: "kIndexHtml",
    path: "/",
    contentType: "text/html; charset=utf-8",
    sourcePath: path.join(srcDir, "index.html"),
  },
  {
    symbol: "kIndexHtml",
    path: "/index.html",
    contentType: "text/html; charset=utf-8",
    sourcePath: path.join(srcDir, "index.html"),
  },
  {
    symbol: "kStylesCss",
    path: "/styles.css",
    contentType: "text/css; charset=utf-8",
    sourcePath: path.join(srcDir, "styles.css"),
  },
  {
    symbol: "kAppJs",
    path: "/app.js",
    contentType: "application/javascript; charset=utf-8",
    sourcePath: path.join(srcDir, "main.ts"),
  },
];

const uniqueSymbols = [];
for (const asset of assets) {
  if (!uniqueSymbols.find((item) => item.symbol === asset.symbol)) {
    uniqueSymbols.push({
      symbol: asset.symbol,
      sourcePath: asset.sourcePath,
    });
  }
}

const lines = [];
lines.push("// Generated from pico2W/webui/src. Do not edit by hand.");
lines.push("");
lines.push("#include <cstring>");
lines.push("");
lines.push("namespace {");
lines.push("");

for (const asset of uniqueSymbols) {
  const content = fs.readFileSync(asset.sourcePath, "utf8");
  lines.push(`constexpr char ${asset.symbol}[] =`);
  lines.push(`    "${escapeCString(content)}";`);
  lines.push("");
}

lines.push("constexpr web_assets::Asset kAssets[] = {");
for (const asset of assets) {
  lines.push(
    `    {"${asset.path}", "${asset.contentType}", ${asset.symbol}, sizeof(${asset.symbol}) - 1},`,
  );
}
lines.push("};");
lines.push("");
lines.push("}  // namespace");
lines.push("");
lines.push("namespace web_assets {");
lines.push("");
lines.push("const Asset* find(const char* path) {");
lines.push("    if (path == nullptr) {");
lines.push("        return nullptr;");
lines.push("    }");
lines.push("");
lines.push("    for (const Asset& asset : kAssets) {");
lines.push("        if (std::strcmp(asset.path, path) == 0) {");
lines.push("            return &asset;");
lines.push("        }");
lines.push("    }");
lines.push("");
lines.push("    return nullptr;");
lines.push("}");
lines.push("");
lines.push("}  // namespace web_assets");
lines.push("");

fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, lines.join("\n"), "utf8");
