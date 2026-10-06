/**
 * ABD Bank Manager — Canonical PatchData Interface
 *
 * Single source of truth for the patch data shape used across the entire
 * codebase: ImportAdapter, ExportAdapter, HardwareLinkContract and the
 * Zod validation schemas all reference this definition.
 *
 * The interface is the **superset** of every former copy — the adapter
 * interfaces used a 12-field subset, while validationSchemas added
 * `fingerprint`, `versionNumber` and `previousVersionId`. This unified
 * definition includes all 15 fields.
 */
export interface PatchData {
  name: string;
  category: string;
  author: string;
  tags: string[];
  notes: string;
  originAddress: string;
  rawData: Uint8Array;
  /** Hardwares where the blob is valid (canonical + compatible models);
   *  if omitted it is derived from the ModelContract. */
  hardwareIds?: string[];
  /** RESERVED for plugin editors — the bank manager never uses or displays it */
  parameters?: Record<string, number>;
  isFavorite: boolean;
  creationDate: string;
  /** SHA-256 fingerprint of the patch payload */
  fingerprint?: string;
  /** Monotonically increasing version counter */
  versionNumber?: number;
  /** Pointer to the previous version's patch id (for version history) */
  previousVersionId?: string | null;
}
