/**
 * ABD Bank Manager — Contract Orchestration Helpers
 *
 * Shared building blocks that ModelContracts use to implement the file-level
 * SysEx orchestration (parse a whole file into PatchData[], serialize a set
 * of patches back to a file). Keeps the per-model parse/serialize methods
 * focused on family-specific details while centralising the common plumbing:
 * bank addressing, fallback naming, hardware-id expansion, heavy warnings.
 */

import type { PatchData } from '../PatchData';
import type { ModelContract } from '../ModelContract';

export interface ParsePatchInput {
  rawData: Uint8Array;
  /** Global patch index inferred from the file (used for originAddress / name fallback). */
  index: number;
  /** Optional explicit name already extracted from the raw bytes. */
  name?: string;
}

export interface ParsedFile {
  modelId: string;
  bankName: string;
  patches: PatchData[];
  warnings: string[];
}

/**
 * Build a generic addressing letter+number scheme:
 *   A1, A2, ... A16, B1 ... (bank letter, 1-based program).
 * Used as fallback originAddress when the real format has no address info.
 */
export function letterNumberAddress(index: number, programsPerBank: number): string {
  const bank = Math.floor(index / programsPerBank);
  const prog = (index % programsPerBank) + 1;
  return `${String.fromCharCode(65 + bank)}${prog}`;
}

/**
 * Assemble PatchData[] from per-message parse results, attaching the fallback
 * names, addresses and expanded hardware ids. Returns null when no patches
 * were produced (caller should treat as "file does not match this contract").
 */
export function assemblePatches(
  contract: ModelContract,
  inputs: ParsePatchInput[],
  detectedModelId: string,
  bankName: string,
): ParsedFile | null {
  if (inputs.length === 0) return null;

  const hardwareIds = expandHardwareIds(contract, detectedModelId);
  const programsPerBank = contract.programsPerBank;

  const patches: PatchData[] = inputs.map((input, i) => {
    const address = letterNumberAddress(input.index, programsPerBank);
    const fallbackName = address;
    return {
      name: input.name || fallbackName,
      category: contract.defaultCategory,
      author: 'Unknown',
      tags: [],
      notes: '',
      originAddress: address,
      rawData: new Uint8Array(input.rawData),
      hardwareIds: hardwareIds.slice(),
      isFavorite: false,
      creationDate: new Date().toISOString(),
    } as PatchData;
  });

  return {
    modelId: detectedModelId,
    bankName,
    patches,
    warnings: [],
  };
}

/** Canonical + compatible hardware ids for a model (multi-hardware association). */
export function expandHardwareIds(contract: ModelContract, modelId: string): string[] {
  const ids = new Set<string>([modelId]);
  for (const id of contract.compatibleModels || []) ids.add(id);
  return Array.from(ids);
}

/** Standard zero-fill helper for a raw patch blob of known size. */
export function toCanonicalSize(raw: Uint8Array, size: number): Uint8Array {
  if (raw.length >= size) return raw.slice(0, size);
  const out = new Uint8Array(size);
  out.set(raw);
  return out;
}

/**
 * Sanitize a 7-bit ASCII name: strip trailing nulls/whitespace, drop bytes
 * outside printable range, trim.
 */
export function decodeAsciiName(bytes: Uint8Array, maxLen: number): string {
  const end = Math.min(bytes.length, maxLen);
  let name = '';
  for (let i = 0; i < end; i++) {
    const c = bytes[i];
    if (c === 0x00) break;
    if (c >= 0x20 && c <= 0x7E) name += String.fromCharCode(c);
  }
  return name.replace(/\s+$/, '').trim();
}
