// The session API stores titles in a 257-byte C buffer, including the NUL.
export const SESSION_TITLE_UTF8_LIMIT = 256;

const encoder = new TextEncoder();

export function sessionTitleUtf8Bytes(title) {
  return encoder.encode(title).length;
}
