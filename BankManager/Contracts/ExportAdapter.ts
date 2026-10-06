/**
 * ABD Bank Manager — ExportAdapter Interface
 * Serializes internal patches to external formats (SysEx, JSON, CSV, .abdbank)
 */

import type { PatchData } from './PatchData.ts';

// Re-export so existing `import { PatchData } from '../ExportAdapter'` keeps working
export type { PatchData } from './PatchData.ts';

export interface ExportOptions {
  includeRawData: boolean;
  includeParameters: boolean;
  midiChannel: number;
  deviceId: number;
  format: 'single' | 'bank';
}

export interface ExportAdapter {
  adapterId: string;
  displayName: string;
  fileExtension: string;
  targetModelIds: string[];

  serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array;
}

export abstract class BaseExportAdapter implements ExportAdapter {
  abstract adapterId: string;
  abstract displayName: string;
  abstract fileExtension: string;
  abstract targetModelIds: string[];

  abstract serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array;

  protected getDefaultOptions(): ExportOptions {
    return {
      includeRawData: true,
      includeParameters: true,
      midiChannel: 0,
      deviceId: 0,
      format: 'bank'
    };
  }
}