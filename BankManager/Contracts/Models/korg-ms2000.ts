/**
 * Korg MS2000 / microKORG / Prophecy ModelContract
 * Covers: MS2000, microKORG, Prophecy
 *
 * SysEx format (ABDMS2000 reference):
 *   Single dump:      F0 42 3n 58 40 [291B packed = 254B programa] F7
 *   All data dump:    F0 42 3n 58 4C [N × 254B packed] F7
 *   Param change:     F0 42 3n 58 41 [offsetL] [offsetH] [value] F7
 *   Dump request:     F0 42 3n 58 10 F7
 *   All dump request: F0 42 3n 58 0E F7
 *
 * Packing: 7-to-8 bit (every 7 data bytes → 1 control + 7 encoded bytes)
 * No separate checksum — integrity via packing structure.
 *
 * Layout REAL del programa del equipo (verificado contra un dump real y el panel
 * ReMS2000 — ver `ABDMS2000/DOCS/MS2000_SysEx_Spec.md` y `ABDMS2000/Source/MIDI/
 * MS2000HardwareProgram.h`):
 *   [0x00..0x0B] nombre · [0x0C..0x0F] sin uso · [0x10..0x25] globales (voice/scale/
 *   split/delay/mod FX/EQ/arp) · [0x26..0x91] Timbre 1 (108 B) · [0x92..0xFD] Timbre 2
 *   (108 B) → **254 B**, y el payload empaquetado 7→8 son **291 B** (el último grupo
 *   parcial NO se rellena), no 288.
 *
 * CORREGIDO (2026-09-12): `patchDataSize` es 254 y el nombre vive en `0x00`; el codec usa
 * `pack8to7NoPad`/`unpack7to8Tolerant`, así que un `.syx` real del equipo se abre con su
 * nombre real y sin perder los últimos bytes (el payload real son 291 B, con el último
 * grupo parcial; antes se rellenaba al escribir y se tiraba al leer).
 *
 * NOTA ABIERTA (orden de los bits del empaquetado): este contrato empaqueta con el bit
 * `6 - j` del byte de control. El empaquetador del plugin (`ABDSharedCode/
 * HardwareDrivers/SysExCodec.h`) y el panel ReMS2000 (su `Bin2Dec` invierte la lista, con
 * lo que el byte *j* cae en el bit *j*) usan el bit `j`. Los dos extremos del mismo
 * formato no coinciden en ese detalle: ver `ABDMS2000/DOCS/MS2000_SysEx_Spec.md` §5.1.
 */

import {
  ModelContract, validateModelContract, type ContractFileParse
} from '../ModelContract';
import {
  pack8to7NoPad, unpack7to8Tolerant, unpack7to8, splitSysexMessages,
  pack7to8NoPad, unpack7to8NoPad
} from '../SysEx/codec';

const BANK_CAPACITY = 128;
const BANKS_COUNT = 8;
const PROGRAMS_PER_BANK = 16;
const PATCH_DATA_SIZE = 254; // Programa real del MS2000/microKORG (2 bloques de timbre de 108 B)
const PATCH_NAME_MAX_LENGTH = 12;

const CATEGORIES = ['Bass', 'Lead', 'Pad', 'Motion', 'Arp', 'Split', 'Bell', 'Other'];
const DEFAULT_CATEGORY = 'Other';
const SYSEX_MANUFACTURER_ID = [0x42];
const FORMAT_VERSION = 1;

const BANK_LETTERS = 'ABCDEFGH';

// ─── Command codes (ABDMS2000 SysExManager.h / MS2000_SysEx_Spec.md) ───
const CMD_DUMP       = 0x40; // Program Data Dump (single patch)
const CMD_ALL_DUMP   = 0x4C; // All Data Dump (128 patches)
const CMD_REQUEST    = 0x10; // Program Dump Request
const CMD_ALL_REQUEST = 0x0E; // All Data Dump Request
const CMD_PARAM      = 0x41; // Parameter Change
const CMD_ACK        = 0x23; // Write Completed
const CMD_NACK       = 0x24; // Write Error

// ─── Model IDs (per hardware) ───
// MS2000 uses model byte 0x58. microKORG uses 0x71 (and 0x58 legacy). Prophecy uses 0x41 (different format).
const MODEL_IDS: Record<string, number> = {
  'korg-ms2000':    0x58,
  'korg-microkorg': 0x71,
  'korg-prophecy':  0x41
};

function getBankLetter(index: number): string {
  return BANK_LETTERS[Math.floor(index / 16)];
}

function getProgramNumber(index: number): number {
  return (index % 16) + 1;
}

// ─── Canal (`3n`) ───

/** Base del byte de canal de Korg: la cabecera es `F0 42 3n …`, con `n` en `0x0..0xF`. */
export const KORG_CHANNEL_BYTE_BASE = 0x30;

/**
 * ¿El byte `3n` lleva el canal desplazado (`0x30 | (canal - 1)`) o tal cual (`0x30 | canal`)?
 *
 * **CORREGIDO (2026-09-15): `true`.** Hasta esa fecha este contrato emitía `0x30 | channel`
 * (canal 1 → `F0 42 31 …`), **un desplazado** frente al plugin
 * (`MS2000HardwareProgram::korgChannelByte`) y frente a los volcados reales de fábrica del
 * repo: 23 de los 24 `.syx` de Korg de `fixtures/` (MS2000, microKORG y Prophecy) empiezan
 * por `F0 42 30`. Es además la única lectura que puede direccionar los 16 canales: con
 * `0x30 | channel`, el canal 16 se colapsa en `0x30` (el bit 4 ya está puesto en la base).
 *
 * ⚠️ *Pendiente de confirmar con el hardware* (item 4 del roadmap de ABDMS2000,
 * `ABDMS2000/ROADMAP.md`): con el MS2000 en canal 1, mandarle `F0 42 30 58 10 F7` y
 * `F0 42 31 58 10 F7` y ver cuál contesta. Si contestara al `0x31`, esta constante vuelve a
 * `false` y `WebUI/tests/unit/korgChannelConvention.test.js` (que hoy fija el `0x30`) pasa a
 * esperar `0x31`. Nada más cambia: el parseo no filtra por canal
 * (`(msg[2] & 0xF0) !== 0x30`), así que la ida y vuelta con el plugin cuadra en cualquiera de
 * las dos lecturas.
 */
export const KORG_CHANNEL_ZERO_BASED = true;

/**
 * Byte `3n` de la cabecera, según la convención declarada arriba. **Punto único** de salida
 * de este byte en el repo: lo comparten el MS2000/microKORG y el Prophecy (misma cabecera de
 * Korg). El canal se acota a `1..16` igual que en el C++, así que un `0` o un `undefined` no
 * desbordan el nibble ni direccionan a otro canal.
 */
export function korgChannelByte(channel: number): number {
  const clamped = Math.max(1, Math.min(16, Number(channel) || 1));
  const value = KORG_CHANNEL_ZERO_BASED ? clamped - 1 : clamped;
  return KORG_CHANNEL_BYTE_BASE | (value & 0x0F);
}

// ─── SysEx Helpers ───

function isKorgSysEx(msg: Uint8Array, modelIdByte: number | number[]): boolean {
  if (msg.length < 5 || msg[0] !== 0xF0 || msg[1] !== 0x42) return false;
  const isMatch = Array.isArray(modelIdByte)
    ? modelIdByte.includes(msg[3])
    : msg[3] === modelIdByte;
  return isMatch && (msg[4] === CMD_DUMP || msg[4] === CMD_ALL_DUMP);
}

const korgMs2000Contract: ModelContract = {
  modelId: 'korg-ms2000',
  displayName: 'Korg MS2000',
  manufacturer: 'Korg',
  icon: 'korg-logo.svg',
  thumbnail: 'korg-ms2000.webp',

  bankCapacity: BANK_CAPACITY,
  banksCount: BANKS_COUNT,
  programsPerBank: PROGRAMS_PER_BANK,

  getProgramAddress(globalIndex: number): string {
    return `${getBankLetter(globalIndex)}.${String(getProgramNumber(globalIndex)).padStart(2, '0')}`;
  },

  parseProgramAddress(address: string): number | null {
    const match = address.match(/^([A-H])\.(\d{2})$/i);
    if (!match) return null;
    const bank = match[1].toUpperCase();
    const prog = parseInt(match[2], 10);
    const bankIdx = BANK_LETTERS.indexOf(bank);
    if (bankIdx === -1 || prog < 1 || prog > 16) return null;
    return bankIdx * 16 + (prog - 1);
  },

  patchDataSize: PATCH_DATA_SIZE,
  patchNameMaxLength: PATCH_NAME_MAX_LENGTH,
  extractPatchName(data: Uint8Array): string {
    const nameOffset = 0x00; // el nombre va al principio del programa real, no en 0x1C
    if (data.length < nameOffset + PATCH_NAME_MAX_LENGTH) return '';
    const nameBytes = data.slice(nameOffset, nameOffset + PATCH_NAME_MAX_LENGTH);
    return new TextDecoder().decode(nameBytes).replace(/\0/g, '').trim();
  },

  categories: CATEGORIES,
  defaultCategory: DEFAULT_CATEGORY,

  compatibleModels: ['korg-microkorg'],

  sysexManufacturerId: SYSEX_MANUFACTURER_ID,
  formatVersion: FORMAT_VERSION,
  sysexModelId: { offset: 3, values: [0x58] },
  midiDetection: { portPattern: /ms.?2000/i, displayName: 'Korg MS2000' },

  midi: {
    defaultChannel: 1,
    defaultDeviceId: 0x58
  },

  parameterSchemaKey: "korg_ms2000_parameter_spec",

  supportsEditBuffer: false,
  interMessageDelayMs: 50,
  dumpTimeoutMs: 3000,

  computeChecksum(): number { return 0; }, // Korg uses 7-to-8 packing, no separate checksum

  verifyChecksum(sysex: Uint8Array): boolean {
    const validModelIds = this.modelId === 'korg-microkorg' ? [0x71, 0x58] : [MODEL_IDS[this.modelId] || 0x58];
    if (sysex.length < 6) return false;
    if (sysex[0] !== 0xF0 || sysex[1] !== 0x42 || !validModelIds.includes(sysex[3])) return false;
    if (sysex[sysex.length - 1] !== 0xF7) return false;
    // Check that the payload decodes to at least a whole program. It is NOT a multiple
    // of 8: 254 B pack to 291 B, with a partial trailing group (1 control + 2 data).
    const packed = sysex.slice(5, sysex.length - 1);
    if (packed.length === 0) return false;
    const rem = packed.length % 8;
    const decoded = Math.floor(packed.length / 8) * 7 + (rem > 0 ? rem - 1 : 0);
    if (decoded < this.patchDataSize) return false;
    return true;
  },

  buildPatchSysEx(rawData: Uint8Array, slot: number, channel: number): Uint8Array {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    const size = this.patchDataSize;
    const data = rawData.slice(0, size);
    const padded = new Uint8Array(size);
    padded.set(data);

    const packed = pack8to7NoPad(padded);
    return new Uint8Array([0xF0, 0x42, korgChannelByte(channel), modelId, CMD_DUMP, ...packed, 0xF7]);
  },

  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    const validModelIds = this.modelId === 'korg-microkorg' ? [0x71, 0x58] : [MODEL_IDS[this.modelId] || 0x58];
    if (!isKorgSysEx(sysex, validModelIds)) return null;
    if (sysex[sysex.length - 1] !== 0xF7) return null;

    const packed = sysex.slice(5, sysex.length - 1);
    const unpacked = unpack7to8Tolerant(packed);

    const rawData = unpacked.slice(0, this.patchDataSize);
    const slot = 0; // Single patch dump, no slot info
    return { rawData: new Uint8Array(rawData), slot };
  },

  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    const cmd = slot === 'all' ? CMD_ALL_REQUEST : CMD_REQUEST;
    return new Uint8Array([0xF0, 0x42, korgChannelByte(channel), modelId, cmd, 0xF7]);
  },

  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    const validModelIds = this.modelId === 'korg-microkorg' ? [0x71, 0x58] : [MODEL_IDS[this.modelId] || 0x58];
    const msgs = splitSysexMessages(sysex).filter(m => isKorgSysEx(m, validModelIds));
    const results: { rawData: Uint8Array; slot: number }[] = [];

    for (const msg of msgs) {
      // All Data Dump (`0x4C`): la memoria completa del equipo. El 7→8 de Korg se aplica al
      // **flujo** de N programas concatenados, no a cada programa por separado, así que se
      // deshace entero y se parte después en bloques de 254 B. Leerlo como un `0x40` —que es
      // lo que hacía esta función, porque `parsePatchSysEx` corta los primeros 254 B— devolvía
      // **solo el primer programa** y tiraba los otros 127 en silencio.
      if (msg[4] === CMD_ALL_DUMP) {
        const unpacked = unpack7to8Tolerant(msg.slice(5, msg.length - 1));
        const size = this.patchDataSize;
        for (let off = 0; off + size <= unpacked.length; off += size)
          results.push({ rawData: new Uint8Array(unpacked.slice(off, off + size)), slot: results.length });
        continue;
      }

      const parsed = korgMs2000Contract.parsePatchSysEx?.call(this, msg);
      if (parsed) results.push(parsed);
    }
    return results;
  },

  /**
   * Acuse del equipo: `F0 42 3n [modelId] 23 F7` (guardado) / `... 24 F7` (error). 6 B y
   * sin payload, así que no se confunde con un dump (`0x40`, 297 B).
   *
   * Ojo: no se reutiliza `isKorgSysEx`, que exige un comando de dump (`0x40`/`0x4C`)
   * en el byte 4 y por eso descarta precisamente estos acuses.
   */
  parseAcknowledgement(sysex: Uint8Array): { ok: boolean } | null {
    const validModelIds = this.modelId === 'korg-microkorg' ? [0x71, 0x58] : [MODEL_IDS[this.modelId] || 0x58];
    if (sysex.length !== 6) return null;
    if (sysex[0] !== 0xF0 || sysex[1] !== 0x42) return null;
    if ((sysex[2] & 0xF0) !== 0x30) return null;   // canal global 30..3F
    if (!validModelIds.includes(sysex[3]) || sysex[5] !== 0xF7) return null;
    if (sysex[4] === CMD_ACK) return { ok: true };
    if (sysex[4] === CMD_NACK) return { ok: false };
    return null;
  },

  parseFile(data: Uint8Array, _filename: string): ContractFileParse | null {
    const validModelIds = this.modelId === 'korg-microkorg' ? [0x71, 0x58] : [MODEL_IDS[this.modelId] || 0x58];
    const parsed = splitSysexMessages(data)
      .filter(m => isKorgSysEx(m, validModelIds))
      .map(m => this.parsePatchSysEx?.(m))
      .filter((p): p is { rawData: Uint8Array; slot: number } => p !== null);
    if (parsed.length === 0) return null;
    const patches = parsed.map(p => ({
      name: this.extractPatchName?.(p.rawData) || this.getProgramAddress(p.slot),
      category: this.defaultCategory,
      author: 'Unknown',
      tags: [] as string[],
      notes: '',
      originAddress: this.getProgramAddress(p.slot),
      rawData: new Uint8Array(p.rawData),
      isFavorite: false,
      creationDate: new Date().toISOString(),
    }));
    return {
      modelId: this.modelId,
      bankName: `Korg ${this.displayName}`,
      patches,
      warnings: [],
    };
  },

  serializeFile(patches: { rawData: Uint8Array; slot: number; name?: string }[], options: { midiChannel: number; deviceId: number; format: 'single' | 'bank' }): Uint8Array {
    const msgs = patches.map(p => this.buildPatchSysEx?.(p.rawData, p.slot, options.midiChannel) ?? new Uint8Array());
    if (msgs.length === 1) return msgs[0];
    const total = msgs.reduce((n, m) => n + m.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const m of msgs) { out.set(m, off); off += m.length; }
    return out;
  },

  detectHardware(ports: Array<{ name?: string; id?: string }>): { name: string; inputId: string; outputId: string; manufacturer: string; modelId: string } | null {
    const port = ports.find(p => /ms.?2000|microkorg/i.test(p.name || ''));
    return port
      ? { name: port.name || 'Korg MS2000', inputId: port.id || '', outputId: port.id || '', manufacturer: 'Korg', modelId: this.modelId }
      : null;
  },

  // Transporte declarado (ver DOCS/BANK_MANAGER_TRANSPORT_MODEL.md). El MS2000
  // habla el protocolo de arriba; esto es lo que lee el Bank Manager para saber
  // qué verbos existen sin mirar si hay función.
  //
  // Sin `audition`: el contrato declara `supportsEditBuffer: false`, es decir, el
  // MS2000 no tiene volcado de buffer de edición. Inventarlo aquí haría que la UI
  // ofreciera algo que el equipo no sabe hacer.
  transport: {
    hardware: { read: true, write: true, bulk: true, dumpRequest: true },
  },

  legacySysEx: {
    modelIdByte: 0x58,
    buildDumpRequest: (ch) => new Uint8Array([0xF0, 0x42, korgChannelByte(ch), 0x58, CMD_REQUEST, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 5 && bytes[0] === 0xF0 && bytes[1] === 0x42 && bytes[3] === 0x58
  }
};

// microKORG (bifurcado: Model ID 0x71, compatible con volcados 0x58 de fábrica)
export const korgMicrokorgContract: ModelContract = {
  ...korgMs2000Contract,
  modelId: 'korg-microkorg',
  displayName: 'Korg microKORG',
  thumbnail: 'korg-microkorg.webp',
  compatibleModels: ['korg-ms2000', 'abd-sm002'],
  sysexModelId: { offset: 3, values: [0x71, 0x58] },
  midiDetection: { portPattern: /microkorg/i, displayName: 'Korg microKORG' },
  midi: {
    defaultChannel: 1,
    defaultDeviceId: 0x71
  },
  legacySysEx: {
    modelIdByte: 0x71,
    buildDumpRequest: (ch) => new Uint8Array([0xF0, 0x42, korgChannelByte(ch), 0x71, CMD_REQUEST, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 5 && bytes[0] === 0xF0 && bytes[1] === 0x42 && (bytes[3] === 0x71 || bytes[3] === 0x58)
  }
};

// ─── ABDSynths native SysEx (ABDMS2000 plugin) ──────────────────────────────
//
// El bloque nativo del plugin (MS2000ProgramData v2 = 384 B, v1 = 128 B) NO es
// el programa del MS2000 físico (254 B). Por eso viaja con su propia cabecera:
//   F0 7D 0A [cmd] [payload 7→8] F7
// donde 0x7D es el ID reservado MIDI 1.0 para uso no comercial/educativo y
// 0x0A es el modelo ABD MS2000. Ver `ABDMS2000/DOCS/ABDSynths_SysEx_Spec.md`.
//
// Este contrato reclama ÚNICAMENTE el formato propio (0x7D/0x0A). Los dumps del
// hardware real (F0 42 3n 58 ...) los reclama `korgMs2000Contract` / `korgMicrokorgContract`
// y se abren como bancos `korg-ms2000` compatibles (presetFormat 'sysex'). Un
// banco `abd-sm002` es siempre presetFormat 'native' y sus bytes son opacos
// (base64 en el transporte software).
const ABD_MANUFACTURER_ID: number[] = [0x7D];
const ABD_MODEL_ID = 0x0A;
const ABD_CMD_DUMP = 0x40;
const ABD_CMD_ALL_DUMP = 0x4C;
// Acuse de escritura de la casa (`F0 7D 0A 23/24 F7`): mismo par que usa el
// MS2000 real, para que el software pueda hacer de hardware ante el Banco.
const ABD_CMD_ACK = 0x23;
const ABD_CMD_NACK = 0x24;
const ABD_PATCH_DATA_SIZE = 384; // v2
const ABD_PATCH_DATA_SIZE_V1 = 128;

function isAbdSysEx(msg: Uint8Array): boolean {
  return msg.length >= 5
    && msg[0] === 0xF0
    && msg[1] === 0x7D
    && msg[2] === ABD_MODEL_ID
    && msg[msg.length - 1] === 0xF7
    && (msg[3] === ABD_CMD_DUMP || msg[3] === ABD_CMD_ALL_DUMP);
}

function isAbdProgramDump(msg: Uint8Array): boolean {
  return msg.length >= 5
    && msg[0] === 0xF0
    && msg[1] === 0x7D
    && msg[2] === ABD_MODEL_ID
    && msg[3] === ABD_CMD_DUMP
    && msg[msg.length - 1] === 0xF7;
}

// ABD MS2000 (SM002) — software synth of the ABDMS2000 host.
// Formato propio F0 7D 0A 40 ... (384 B por patch, v2). No hereda el SysEx
// Korg: lo declara explícitamente para que `getContractForSysex` lo reclame.
export const korgAbdSm002Contract: ModelContract = {
  modelId: 'abd-sm002',
  displayName: 'ABD MS2000 (SM002)',
  manufacturer: 'ABDSynths',
  icon: 'korg-logo.svg',
  thumbnail: 'korg-ms2000.webp',
  bankCapacity: BANK_CAPACITY,
  banksCount: BANKS_COUNT,
  programsPerBank: PROGRAMS_PER_BANK,
  getProgramAddress: korgMs2000Contract.getProgramAddress,
  parseProgramAddress: korgMs2000Contract.parseProgramAddress,
  patchDataSize: ABD_PATCH_DATA_SIZE,
  patchNameMaxLength: PATCH_NAME_MAX_LENGTH,
  extractPatchName(data: Uint8Array): string {
    const nameOffset = 0x00;
    if (data.length < nameOffset + PATCH_NAME_MAX_LENGTH) return '';
    const nameBytes = data.slice(nameOffset, nameOffset + PATCH_NAME_MAX_LENGTH);
    return new TextDecoder().decode(nameBytes).replace(/\0/g, '').trim();
  },
  categories: CATEGORIES,
  defaultCategory: DEFAULT_CATEGORY,
  compatibleModels: ['korg-ms2000', 'korg-microkorg'],
  sysexManufacturerId: ABD_MANUFACTURER_ID,
  sysexModelId: { offset: 2, values: [ABD_MODEL_ID] },
  formatVersion: FORMAT_VERSION,
  isSoftsynth: true,
  midi: {
    defaultChannel: 1,
    defaultDeviceId: ABD_MODEL_ID
  },
  supportsEditBuffer: false,
  interMessageDelayMs: 50,
  dumpTimeoutMs: 3000,
  computeChecksum(): number { return 0; },
  verifyChecksum(sysex: Uint8Array): boolean {
    if (!isAbdSysEx(sysex)) return false;
    const packed = sysex.slice(4, sysex.length - 1);
    if (packed.length === 0) return false;
    const rem = packed.length % 8;
    const decoded = Math.floor(packed.length / 8) * 7 + (rem > 0 ? rem - 1 : 0);
    if (decoded < ABD_PATCH_DATA_SIZE_V1) return false;
    // Un payload de 439 B decodifica a 384; uno de 147 a 128 (v1)
    return decoded === ABD_PATCH_DATA_SIZE || decoded === ABD_PATCH_DATA_SIZE_V1 || decoded % ABD_PATCH_DATA_SIZE === 0 || decoded % ABD_PATCH_DATA_SIZE_V1 === 0;
  },
  buildPatchSysEx(rawData: Uint8Array, _slot: number, _channel: number): Uint8Array {
    const size = ABD_PATCH_DATA_SIZE;
    const data = rawData.slice(0, size);
    const padded = new Uint8Array(size);
    padded.set(data);
    // Codec C++ (ABDSharedCode/HardwareDrivers/SysExCodec) usa bit i, no 6-j.
    // En TS ese es pack7to8NoPad / unpack7to8NoPad (misma geometría sin relleno).
    const packed = pack7to8NoPad(padded);
    return new Uint8Array([0xF0, 0x7D, ABD_MODEL_ID, ABD_CMD_DUMP, ...packed, 0xF7]);
  },
  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    if (!isAbdProgramDump(sysex)) return null;
    const packed = sysex.slice(4, sysex.length - 1);
    const unpacked = unpack7to8NoPad(packed);
    if (unpacked.length === ABD_PATCH_DATA_SIZE_V1) {
      const padded = new Uint8Array(ABD_PATCH_DATA_SIZE);
      padded.set(unpacked);
      return { rawData: padded, slot: 0 };
    }
    if (unpacked.length < ABD_PATCH_DATA_SIZE) return null;
    const rawData = unpacked.slice(0, ABD_PATCH_DATA_SIZE);
    return { rawData: new Uint8Array(rawData), slot: 0 };
  },
  buildDumpRequest(_slot: number | 'all', _channel: number): Uint8Array {
    const cmd = _slot === 'all' ? ABD_CMD_ALL_DUMP : ABD_CMD_DUMP;
    // Petición sin payload: F0 7D 0A [cmd] F7 — el plugin responde con Program/All dump.
    return new Uint8Array([0xF0, 0x7D, ABD_MODEL_ID, cmd, 0xF7]);
  },
  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    const msgs = splitSysexMessages(sysex).filter(m => isAbdSysEx(m));
    const results: { rawData: Uint8Array; slot: number }[] = [];
    for (const msg of msgs) {
      const packed = msg.slice(4, msg.length - 1);
      const unpacked = unpack7to8NoPad(packed);
      if (unpacked.length === 0) continue;
      // Determinar tamaño de bloque: v2 (384) o v1 (128)
      let blockSize = ABD_PATCH_DATA_SIZE;
      if (unpacked.length % ABD_PATCH_DATA_SIZE !== 0 && unpacked.length % ABD_PATCH_DATA_SIZE_V1 === 0) {
        blockSize = ABD_PATCH_DATA_SIZE_V1;
      } else if (unpacked.length < ABD_PATCH_DATA_SIZE && unpacked.length === ABD_PATCH_DATA_SIZE_V1) {
        blockSize = ABD_PATCH_DATA_SIZE_V1;
      } else if (unpacked.length % ABD_PATCH_DATA_SIZE !== 0) {
        continue;
      }
      const count = Math.floor(unpacked.length / blockSize);
      for (let i = 0; i < count; i++) {
        const chunk = unpacked.slice(i * blockSize, (i + 1) * blockSize);
        let raw: Uint8Array;
        if (blockSize === ABD_PATCH_DATA_SIZE_V1) {
          raw = new Uint8Array(ABD_PATCH_DATA_SIZE);
          raw.set(chunk);
        } else {
          raw = new Uint8Array(chunk);
        }
        results.push({ rawData: raw, slot: i });
      }
    }
    return results;
  },

  /**
   * Acuse del **software** (ABDMSynths): `F0 7D 0A 23 F7` guardado / `... 24 F7`
   * error. Es el mismo par de códigos que el MS2000 real, con la cabecera de la
   * casa: así el Banco puede tratar al plugin como si fuera un equipo.
   */
  parseAcknowledgement(sysex: Uint8Array): { ok: boolean } | null {
    if (sysex.length !== 5) return null;
    if (sysex[0] !== 0xF0 || sysex[1] !== 0x7D || sysex[2] !== ABD_MODEL_ID || sysex[4] !== 0xF7) return null;
    if (sysex[3] === ABD_CMD_ACK) return { ok: true };
    if (sysex[3] === ABD_CMD_NACK) return { ok: false };
    return null;
  },

  parseFile(data: Uint8Array, _filename: string): ContractFileParse | null {
    const msgs = splitSysexMessages(data).filter(m => isAbdSysEx(m));
    if (msgs.length === 0) return null;
    const all: { rawData: Uint8Array; slot: number }[] = [];
    for (const msg of msgs) {
      const bulk = (this.parseDumpResponse as any)?.call(this, msg) as { rawData: Uint8Array; slot: number }[] | null;
      if (bulk && bulk.length > 0) {
        // parseDumpResponse ya divide en bloques; slot relativo dentro del mensaje
        const base = all.length;
        for (let i = 0; i < bulk.length; i++) all.push({ rawData: bulk[i].rawData, slot: base + i });
      } else {
        const single = this.parsePatchSysEx?.(msg);
        if (single) all.push({ rawData: single.rawData, slot: all.length });
      }
    }
    if (all.length === 0) return null;
    const patches = all.map((p, idx) => ({
      name: this.extractPatchName?.(p.rawData) || this.getProgramAddress(idx),
      category: this.defaultCategory,
      author: 'Unknown',
      tags: [] as string[],
      notes: '',
      originAddress: this.getProgramAddress(idx),
      rawData: new Uint8Array(p.rawData),
      isFavorite: false,
      creationDate: new Date().toISOString(),
    }));
    return {
      modelId: this.modelId,
      bankName: `ABDSynths ${this.displayName}`,
      patches,
      warnings: [],
    };
  },
  serializeFile(patches: { rawData: Uint8Array; slot: number; name?: string }[], options: { midiChannel: number; deviceId: number; format: 'single' | 'bank' }): Uint8Array {
    const msgs = patches.map(p => this.buildPatchSysEx?.(p.rawData, p.slot, options.midiChannel) ?? new Uint8Array());
    if (msgs.length === 1) return msgs[0];
    const total = msgs.reduce((n, m) => n + m.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const m of msgs) { out.set(m, off); off += m.length; }
    return out;
  },
  detectHardware(): { name: string; inputId: string; outputId: string; manufacturer: string; modelId: string } | null {
    return null;
  },
  // Transporte: híbrido — el hardware real por SysEx Korg (vía korg-ms2000) y
  // los presets propios del plugin por el bridge (sistema native, 384 B opacos).
  // `hardware` aquí no se usa para importar 0x7D (ya lo hace este contrato),
  // sino para documentar que el host físico sigue siendo accesible.
  transport: {
    hardware: { read: true, write: true, bulk: true, dumpRequest: true },
    software: {
      systems: ['native'],
      read: true,
      write: true,
      audition: true,
      capture: true,
    },
  },
  legacySysEx: {
    modelIdByte: ABD_MODEL_ID,
    buildDumpRequest: () => new Uint8Array([0xF0, 0x7D, ABD_MODEL_ID, ABD_CMD_DUMP, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 5 && bytes[0] === 0xF0 && bytes[1] === 0x7D && bytes[2] === ABD_MODEL_ID
  }
};

export const allKorgContracts = [
  korgMs2000Contract,
  korgMicrokorgContract,
  korgAbdSm002Contract
];

allKorgContracts.forEach(c => {
  const result = validateModelContract(c);
  if (!result.valid) {
    console.error(`❌ ${c.modelId} validation failed:`, result.errors);
  }
});

export default korgMs2000Contract;
