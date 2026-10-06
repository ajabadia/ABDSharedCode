/**
 * ABD Bank Manager — SysEx Codec
 *
 * Single source of truth (SSOT) for the low-level SysEx encodings shared across
 * ModelContracts and adapters. Removes the duplication that previously lived
 * in each model contract + Adapters/sysexUtils.ts.
 *
 * Encodings covered:
 *   - 7-to-8 bit packing  (Korg, Behringer DeepMind family, Pro-800)
 *   - Prophecy [7 data][1 ctrl] packing
 *   - Casio CZ nibble encoding
 *   - Roland / Yamaha / Casio checksums
 *   - F0...F7 message splitting
 *
 * SSOT del lado C++: `ABDSharedCode/HardwareDrivers/` (`abd::hw::SysExCodec`
 * y `abd::hw::CasioNibbleCodec`). Este fichero es el port para la WebUI; los
 * vectores de paridad viven en `cpp/tests/BankManagerCoreTests.cpp`, que
 * fija los mismos bytes en los dos lados del puente.
 */

// ─── 7-to-8 Bit Packing (Korg, Behringer) ───

/**
 * Pack 8-bit data to 7-bit encoding.
 * Every 7 input bytes produce 8 output bytes.
 * The first byte of each group is the control byte whose bits
 * indicate the MSB of each subsequent byte.
 * The last group is zero-padded to a full 7 bytes, so the output
 * length is always a multiple of 8 (required by devices, which always
 * transmit complete groups). `unpack7to8` decodes padded and non-padded
 * payloads alike; for wire-exact frames without padding use `pack8to7NoPad`.
 */
export function pack8to7(data: Uint8Array): Uint8Array {
  const packed: number[] = [];
  for (let i = 0; i < data.length; i += 7) {
    const group = data.slice(i, Math.min(i + 7, data.length));
    let control = 0;
    for (let j = 0; j < 7; j++) {
      const byte = j < group.length ? group[j] : 0;
      control |= ((byte >> 7) & 1) << (6 - j);
    }
    packed.push(control);
    for (let j = 0; j < 7; j++) {
      packed.push((j < group.length ? group[j] : 0) & 0x7F);
    }
  }
  return new Uint8Array(packed);
}

/**
 * Tolerant 7-to-8 unpack used by the DeepMind family (DM12/DM6/DM12D).
 * Two differences from pack8to7/unpack7to8 (Korg order):
 *   - Control-bit order is reversed: byte i's MSB lives in control bit i
 *     (Korg packs byte j into control bit 6-j).
 *   - Partial trailing groups are decoded instead of dropped, because the
 *     DeepMind family transmits a packed payload that is not a multiple of 8
 *     (278 bytes → 243 decoded, callers slice down to the 242-byte patch).
 */
export function pack8to7Dm(data: Uint8Array): Uint8Array {
  const packed: number[] = [];
  for (let offset = 0; offset < data.length; offset += 7) {
    const count = Math.min(7, data.length - offset);
    let control = 0;
    for (let i = 0; i < count; i++) {
      if ((data[offset + i] & 0x80) !== 0) control |= 1 << i;
    }
    packed.push(control);
    for (let i = 0; i < 7; i++) packed.push(i < count ? data[offset + i] & 0x7F : 0);
  }
  return new Uint8Array(packed);
}

/** Inverse of pack8to7Dm (DeepMind control-bit order + partial-tolerance). */
export function unpack7to8Dm(packed: Uint8Array): Uint8Array {
  const unpacked: number[] = [];
  for (let offset = 0; offset < packed.length; offset += 8) {
    const control = packed[offset];
    for (let i = 0; i < 7 && offset + i + 1 < packed.length; i++) {
      unpacked.push((packed[offset + i + 1] & 0x7F) | (((control >> i) & 1) << 7));
    }
  }
  return new Uint8Array(unpacked);
}

/**
 * Unpack 7-bit encoded data to 8-bit.
 * Every 7 input bytes produce 8 output bytes.
 * Input bytes have 7 significant bits; the MSB of each input byte
 * carries 1 bit of each of the next 7 output bytes.
 *
 * Partial trailing groups are **decoded, not dropped** (parity with the C++
 * codec `ABDSharedCode/HardwareDrivers/SysExCodec.cpp`): real devices end the
 * payload mid-group — the MS2000/microKORG transmit a 254-byte program as 291
 * payload bytes (36 full groups + 1 control + 2 data). A decoder that required
 * a multiple of 8 silently lost the last bytes of every program.
 */
export function unpack7to8(packed: Uint8Array): Uint8Array {
  const unpacked: number[] = [];
  let i = 0;
  while (i < packed.length) {
    const control = packed[i++];
    for (let j = 0; j < 7 && i < packed.length; j++) {
      const highBit = (control >> (6 - j)) & 1;
      unpacked.push(((highBit << 7) | (packed[i++] & 0x7F)) & 0xFF);
    }
  }
  return new Uint8Array(unpacked);
}

/**
 * Korg-order 7-to-8 packer **without padding the final partial group**.
 *
 * The MS2000/microKORG transmit a 254-byte program as 291 payload bytes
 * (36 full groups + 1 control + 2 data), not as 296 padded bytes: the same
 * 291-byte length the plugin's C++ packer produces (`SysExCodec::pack8to7`,
 * `MS2000HardwareProgram::packedPayloadSize()`). `pack8to7` zero-pads, which
 * would make the frame 5 bytes longer than a real device's.
 *
 * El orden Korg (bit 6-j) ya existe tambien en C++ como
 * `abd::hw::SysExCodec::pack8to7Korg` — subido a ABDSharedCode junto con las
 * dos politicas de relleno; el nombre "Korg" del header compartido, que antes
 * describia el orden canonico bit i, esta corregido alla.
 */
export function pack8to7NoPad(data: Uint8Array): Uint8Array {
  const packed: number[] = [];
  for (let i = 0; i < data.length; i += 7) {
    const count = Math.min(7, data.length - i);
    let control = 0;
    for (let j = 0; j < count; j++) {
      if ((data[i + j] & 0x80) !== 0) control |= 1 << (6 - j);
    }
    packed.push(control);
    for (let j = 0; j < count; j++) packed.push(data[i + j] & 0x7F);
  }
  return new Uint8Array(packed);
}

/**
 * Inverse of `pack8to7NoPad`: Korg control-bit order with the partial trailing
 * group decoded. Now that `unpack7to8` itself is tolerant of partial groups
 * (parity with the C++ codec), this is an alias kept for its semantic name —
 * Korg model contracts read real device payloads through it.
 */
export function unpack7to8Tolerant(packed: Uint8Array): Uint8Array {
  return unpack7to8(packed);
}

// ─── Pro-800 7-to-8 Packing (no trailing-group padding) ───
// The Pro-800 packs exact 7-byte chunks and does NOT zero-pad the final
// partial group (unlike Korg/DeepMind, which pad to full 7-byte groups).
// Preserved verbatim from the Pro-800 contract to keep frame sizes identical
// to real hardware dumps (173 raw → 198 packed → 210-byte frame).

/** Pack 8-bit data without padding the final partial 7-byte group. */
export function pack7to8NoPad(data: Uint8Array): Uint8Array {
  const packed: number[] = [];
  let srcIdx = 0;
  while (srcIdx < data.length) {
    const chunkSize = Math.min(7, data.length - srcIdx);
    let msbCollector = 0;
    for (let i = 0; i < chunkSize; i++) {
      if ((data[srcIdx + i] & 0x80) !== 0) msbCollector |= (1 << i);
    }
    packed.push(msbCollector);
    for (let i = 0; i < chunkSize; i++) packed.push(data[srcIdx + i] & 0x7F);
    srcIdx += chunkSize;
  }
  return new Uint8Array(packed);
}

/** Unpack non-padded 7-bit encoded data (tolerant of partial trailing groups). */
export function unpack7to8NoPad(packed: Uint8Array): Uint8Array {
  const unpacked: number[] = [];
  let srcIdx = 0;
  while (srcIdx < packed.length) {
    const msbCollector = packed[srcIdx++];
    for (let i = 0; i < 7 && srcIdx < packed.length; i++) {
      const bit7 = (msbCollector >> i) & 0x01;
      unpacked.push((packed[srcIdx++] & 0x7F) | (bit7 << 7));
    }
  }
  return new Uint8Array(unpacked);
}

// ─── Prophecy [7 data][1 ctrl] Packing ───
// Prophecy uses control byte AFTER each 7 data bytes with a 3-byte tail./** Pack 8-bit data to Prophecy 7-to-8 encoding: [7 data][1 ctrl] groups + trailing bytes (no ctrl). */
export function packProphecy7to8(data: Uint8Array): Uint8Array {
  const packed: number[] = [];
  const fullGroups = Math.floor(data.length / 7);
  const rem = data.length % 7;

  for (let g = 0; g < fullGroups; g++) {
    const i = g * 7;
    let control = 0;
    for (let j = 0; j < 7; j++) {
      const byte = data[i + j];
      control |= ((byte >> 7) & 1) << (6 - j);
      packed.push(byte & 0x7F);
    }
    packed.push(control);
  }

  if (rem > 0) {
    const i = fullGroups * 7;
    for (let j = 0; j < rem; j++) {
      packed.push(data[i + j] & 0x7F);
    }
  }
  return new Uint8Array(packed);
}

/** Unpack Prophecy 8-to-7 encoding back to 8-bit data. */
export function unpackProphecy8to7(packed: Uint8Array): Uint8Array {
  const out: number[] = [];
  for (let i = 0; i + 8 <= packed.length; i += 8) {
    const control = packed[i + 7];
    for (let j = 0; j < 7; j++) {
      const highBit = (control >> (6 - j)) & 1;
      out.push(((highBit << 7) | (packed[i + j] & 0x7F)) & 0xFF);
    }
  }
  const rem = packed.length % 8;
  for (let i = packed.length - rem; i < packed.length; i++) out.push(packed[i] & 0x7F);
  return new Uint8Array(out);
}

// ─── Nibble Encoding (Casio CZ) ───

/**
 * Decode Casio CZ nibble format.
 * Each pair of input bytes encodes one output byte:
 *   high nibble = (byte0 & 0x0F) << 4
 *   low nibble  = byte1 & 0x0F
 *
 * Parity con `abd::hw::CasioNibbleCodec::decode` (ABDSharedCode), HighFirst.
 */
export function decodeNibble(nibbles: Uint8Array): Uint8Array {
  const decoded: number[] = [];
  for (let i = 0; i + 1 < nibbles.length; i += 2) {
    const high = (nibbles[i] & 0x0F) << 4;
    const low = nibbles[i + 1] & 0x0F;
    decoded.push((high | low) & 0xFF);
  }
  return new Uint8Array(decoded);
}

/**
 * Encode data to Casio CZ nibble format.
 * Each input byte produces two output bytes (high nibble, low nibble).
 */
export function encodeNibble(data: Uint8Array): Uint8Array {
  const nibbles: number[] = [];
  for (const byte of data) {
    nibbles.push((byte >> 4) & 0x0F);
    nibbles.push(byte & 0x0F);
  }
  return new Uint8Array(nibbles);
}

// ─── Checksums ───

/** Roland bulk: (-sum) & 0x7F over payload (used by Juno bulk dump). */
export function bulkChecksum(bytes: Uint8Array): number {
  let sum = 0;
  for (const b of bytes) sum += b;
  return (-sum) & 0x7F;
}

/** Roland: XOR all bytes, invert, AND 0x7F (used by AIRA DT1). */
export function rolandChecksum(bytes: Uint8Array): number {
  let xor = 0;
  for (const b of bytes) xor ^= b;
  return (~xor) & 0x7F;
}

/** Yamaha: sum all bytes, AND 0x7F, two's complement (negate mod128). */
export function yamahaChecksum(bytes: Uint8Array): number {
  let sum = 0;
  for (const b of bytes) sum += b;
  return (128 - (sum % 128)) & 0x7F;
}

/** Casio: sum all bytes, AND 0x7F. */
export function casioChecksum(bytes: Uint8Array): number {
  let sum = 0;
  for (const b of bytes) sum += b;
  return sum & 0x7F;
}

// ─── SysEx Message Splitting ───

/** Find all F0...F7 messages in raw bytes. */
export function splitSysexMessages(raw: Uint8Array): Uint8Array[] {
  const messages: Uint8Array[] = [];
  let inSysex = false;
  let start = -1;
  for (let i = 0; i < raw.length; i++) {
    if (raw[i] === 0xF0 && !inSysex) {
      inSysex = true;
      start = i;
    } else if (raw[i] === 0xF7 && inSysex) {
      messages.push(raw.slice(start, i + 1));
      inSysex = false;
    }
  }
  return messages;
}

/** Extract data between F0...F7, stripping checksum byte before F7. */
export function extractSysexData(sysex: Uint8Array, headerLen: number): Uint8Array {
  if (sysex.length < headerLen + 2) return new Uint8Array(0);
  return sysex.slice(headerLen, sysex.length - 2);
}

/** Re-export canonical names for any existing imports. */
export const pack8to7Alias = pack8to7;
