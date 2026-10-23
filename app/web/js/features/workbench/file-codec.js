export function fileEncoding(bytes) {
  if (bytes[0] === 255 && bytes[1] === 254) return { encoding: "utf-16le", bom: true };
  if (bytes[0] === 254 && bytes[1] === 255) return { encoding: "utf-16be", bom: true };
  return { encoding: "utf-8", bom: bytes[0] === 239 && bytes[1] === 187 && bytes[2] === 191 };
}
export function encodeFileText(text, format) {
  const content = text.replace(/\r\n|\r|\n/g, format.lineEnding || "\n");
  if (format.encoding === "utf-8") return new TextEncoder().encode((format.bom ? "\ufeff" : "") + content);
  const output = new Uint8Array((content.length + (format.bom ? 1 : 0)) * 2), view = new DataView(output.buffer), little = format.encoding === "utf-16le";
  let offset = 0; if (format.bom) { view.setUint16(offset, 0xfeff, little); offset += 2; }
  for (let at = 0; at < content.length; at++, offset += 2) view.setUint16(offset, content.charCodeAt(at), little);
  return output;
}
export function fileBase64(bytes) {
  const chunks = [];
  for (let at = 0; at < bytes.length; at += 16384) chunks.push(String.fromCharCode(...bytes.subarray(at, at + 16384)));
  return btoa(chunks.join(""));
}
