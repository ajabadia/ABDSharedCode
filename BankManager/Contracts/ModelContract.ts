/**
 * ABD Bank Manager — ModelContract Interface
 * Defines the capabilities and structure of a synthesizer model
 */

/**
 * Result of parsing a full SysEx dump file. Mirrors the shape of a PatchData[]
 * so adapters can hand the result back verbatim as an ImportResult.
 */
export interface ContractFileParse {
  modelId: string;
  bankName: string;
  patches: {
    name: string;
    category: string;
    author: string;
    tags: string[];
    notes: string;
    originAddress: string;
    rawData: Uint8Array;
    hardwareIds?: string[];
    isFavorite: boolean;
    creationDate: string;
  }[];
  warnings: string[];
}

export interface ModelContract {
  // ─── Identity ───
  modelId: string;              // 'casio-cz101', 'roland-juno106', 'korg-ms2000', 'behringer-deepmind12', 'yamaha-dx7'
  displayName: string;          // 'Casio CZ-101'
  manufacturer: string;         // 'Casio', 'Roland', 'Korg', 'Behringer', 'Yamaha'
  icon?: string;                // URL or SVG inline logo
  thumbnail?: string;           // Path to hardware image (~200x120px, WebP)

  // ─── Bank Structure ───
  bankCapacity: number;         // Total patches (16 CZ101, 128 Juno106, 128 MS2000, 1024 DM12)
  banksCount: number;           // Logical banks (1 CZ, 2 Juno, 8 MS2000, 8 DM12)
  programsPerBank: number;      // Patches per bank (16, 64, 16, 128)

  // ─── Addressing ───
  getProgramAddress(globalIndex: number): string;
  parseProgramAddress(address: string): number | null;

  // ─── Patch Data ───
  patchDataSize: number;        // Bytes per patch (128 CZ, 18 Juno, 288 MS2000, 242 DM12)
  patchNameMaxLength: number;   // Name chars (0 CZ, 0 Juno, 12 MS2000, 16 DM12)
  extractPatchName?(data: Uint8Array): string;

  // ─── Categories ───
  categories: string[];         // ['Bass','Lead','Pad','FX','Keys','Perc','Synth','Other']
  defaultCategory: string;      // 'Other'

  // ─── Compatibility ───
  compatibleModels?: string[];  // Models with identical patch format

  // ─── Nature ───
  /** True for a software-synth that shares a hardware family's patch format.
   *  Such a model is a local edit target (no physical MIDI port); dumps apply
   *  to the emulated hardware but auto-detection/hardware routing is disabled. */
  isSoftsynth?: boolean;

  // ─── SysEx Metadata ───
  sysexManufacturerId: number[]; // [0x44,0x00,0x00] Casio, [0x41] Roland, [0x42] Korg
  formatVersion: number;        // Contract version for migrations

  // ─── SysEx Disambiguation (when multiple models share a manufacturer byte) ───
  /** Byte position + expected value(s) to disambiguate models within one manufacturer.
   *  `values` describes the byte at `offset`; `multiByte`, when present, describes
   *  the following bytes as one contiguous identity sequence.
   *  Example: DX7 uses { offset: 3, values: [0x00] }, DX7II uses { offset: 3, values: [0x01] }.
   */
  sysexModelId?: {
    offset: number;       // byte offset in SysEx message (after F0)
    values: number[];     // expected byte value(s) at this offset
    multiByte?: number[]; // optional expected bytes immediately after offset
  };

  // ─── MIDI Detection (auto-detect from port name) ───
  /** Regex pattern to match against MIDI port name for auto-detection */
  midiDetection?: {
    portPattern: RegExp;    // e.g. /dx.?7|fm.?1/i
    displayName: string;    // e.g. 'DX7'
  };

  // ─── Parameter Schema (reference to model-specific parameter decoder) ───
  /** Key in the PARAMETER_SCHEMAS registry. If set, enables parameter interpretation UI. */
  parameterSchemaKey?: string;

  // ─── MIDI Defaults (derived, not user-editable) ───
  midi?: {
    defaultChannel: number;   // MIDI channel 1-16 for SysEx dumps
    defaultDeviceId: number;  // Device ID for SysEx headers (e.g. Roland Juno: 0x18)
  };

  // ─── SysEx Operations (contract-driven import/export) ───
  /** Build a complete SysEx message for a single patch dump */
  buildPatchSysEx?(rawData: Uint8Array, slot: number, channel: number): Uint8Array;
  /** Build a bulk SysEx message for multiple patches (bank dump) */
  buildBulkSysEx?(patches: { rawData: Uint8Array; slot: number }[], channel: number): Uint8Array;
  /** Parse a SysEx message into { rawData, slot } or null if not matching */
  parsePatchSysEx?(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null;
  /** Compute manufacturer-specific checksum byte for the given payload */
  computeChecksum?(data: Uint8Array): number;
  /** Verify checksum on a complete SysEx message (returns true if valid) */
  verifyChecksum?(sysex: Uint8Array): boolean;
  /** Build a dump request message (single slot or 'all') */
  buildDumpRequest?(slot: number | 'all', channel: number): Uint8Array;
  /** Parse a dump response (bulk) into an array of { rawData, slot } */
  parseDumpResponse?(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[];
  /** Whether this model supports edit buffer (audition without overwrite) */
  supportsEditBuffer?: boolean;
  /** Minimum delay between SysEx messages (ms) */
  interMessageDelayMs?: number;
  /** Timeout waiting for dump response (ms) */
  dumpTimeoutMs?: number;
  /** Maximum SysEx message size (bytes) before splitting. If set, bulk dumps are split into chunks. */
  maxSysExMessageSize?: number;

  // ─── File-Level Orchestration (SSOT for Import/Export/HardwareLink) ───
  // These make the contract the single source of truth for parsing a whole
  // SysEx file into PatchData[] and serializing patches back to a file, so the
  // concrete adapters can be thin delegating wrappers.

  /**
   * Parse a complete SysEx dump file (single patch, bank or bulk) into
   * PatchData[]. Returns null when the file does not match this contract's
   * format (used by the import adapter's canParse + parse).
   */
  parseFile?(data: Uint8Array, filename: string): ContractFileParse | null;

  /** Serialize a set of patches back to a SysEx file (single or bank). */
  serializeFile?(
    patches: { rawData: Uint8Array; slot: number; name?: string }[],
    options: { midiChannel: number; deviceId: number; format: 'single' | 'bank' },
  ): Uint8Array;

  /** Auto-detect hardware from MIDI port name (used by the HardwareLink adapter). */
  detectHardware?(ports: Array<{ name?: string; id?: string }>): { name: string; inputId: string; outputId: string; manufacturer: string; modelId: string } | null;

  // ─── Parameter Validation (NeuralDX7 ranges) ───
  /** Verify that a decoded parameter set is within valid hardware ranges. */
  verifyVoice?(params: Record<string, number>): boolean;

  // ─── Legacy Support (Guide §9.1 compat) ───
  legacySysEx?: {
    modelIdByte: number;
    buildDumpRequest: (channel: number) => Uint8Array;
    validateSysEx: (bytes: Uint8Array) => boolean;
  };
}

/**
 * Validates a ModelContract implementation
 */
export function validateModelContract(contract: ModelContract): { valid: boolean; errors: string[] } {
  const errors = [];

  if (!contract.modelId) errors.push('modelId is required');
  if (!contract.displayName) errors.push('displayName is required');
  if (!contract.manufacturer) errors.push('manufacturer is required');

  if (!Number.isInteger(contract.bankCapacity) || contract.bankCapacity <= 0) {
    errors.push('bankCapacity must be positive integer');
  }
  if (!Number.isInteger(contract.banksCount) || contract.banksCount <= 0) {
    errors.push('banksCount must be positive integer');
  }
  if (!Number.isInteger(contract.programsPerBank) || contract.programsPerBank <= 0) {
    errors.push('programsPerBank must be positive integer');
  }
  if (contract.banksCount * contract.programsPerBank !== contract.bankCapacity) {
    errors.push('banksCount * programsPerBank must equal bankCapacity');
  }

  if (typeof contract.getProgramAddress !== 'function') {
    errors.push('getProgramAddress function required');
  }
  if (typeof contract.parseProgramAddress !== 'function') {
    errors.push('parseProgramAddress function required');
  }

  if (!Number.isInteger(contract.patchDataSize) || contract.patchDataSize <= 0) {
    errors.push('patchDataSize must be positive integer');
  }
  if (!Number.isInteger(contract.patchNameMaxLength) || contract.patchNameMaxLength < 0) {
    errors.push('patchNameMaxLength must be non-negative integer');
  }

  if (!Array.isArray(contract.categories) || contract.categories.length === 0) {
    errors.push('categories must be non-empty array');
  }
  if (!contract.defaultCategory || !contract.categories.includes(contract.defaultCategory)) {
    errors.push('defaultCategory must be one of categories');
  }

  if (!Array.isArray(contract.sysexManufacturerId) || contract.sysexManufacturerId.length === 0) {
    errors.push('sysexManufacturerId must be non-empty array');
  }
  if (!Number.isInteger(contract.formatVersion) || contract.formatVersion < 1) {
    errors.push('formatVersion must be positive integer');
  }

  // Test round-trip addressing
  for (let i = 0; i < Math.min(10, contract.bankCapacity); i++) {
    const addr = contract.getProgramAddress(i);
    const parsed = contract.parseProgramAddress(addr);
    if (parsed !== i) {
      errors.push(`Address round-trip failed for index ${i}: ${addr} -> ${parsed}`);
    }
  }

  return { valid: errors.length === 0, errors };
}