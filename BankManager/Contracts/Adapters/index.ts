/**
 * Adapters barrel — concrete Import/Export/HardwareLink adapters.
 */

export { RolandJunoImportAdapter, RolandJunoExportAdapter, RolandJunoHardwareLink } from './rolandJunoAdapter';
export { KorgMs2000ImportAdapter, KorgMs2000ExportAdapter, KorgMs2000HardwareLink } from './korgMs2000Adapter';
export { YamahaDx7ImportAdapter, YamahaDx7ExportAdapter, YamahaDx7HardwareLink } from './yamahaDx7Adapter';
export { CasioCzImportAdapter, CasioCzExportAdapter, CasioCzHardwareLink } from './casioCzAdapter';
export { BehringerDm12ImportAdapter, BehringerDm12ExportAdapter } from './behringerDm12Adapter';
export { BehringerPro800ImportAdapter, BehringerPro800ExportAdapter, BehringerPro800HardwareLink } from './behringerPro800Adapter';
export { BehringerDeepMind12HardwareLink } from './behringerDeepMindAdapter';
export { RolandAiraImportAdapter, RolandAiraExportAdapter, RolandAiraHardwareLink } from './rolandAiraAdapter';

import { RolandJunoImportAdapter, RolandJunoExportAdapter, RolandJunoHardwareLink } from './rolandJunoAdapter';
import { KorgMs2000ImportAdapter, KorgMs2000ExportAdapter, KorgMs2000HardwareLink } from './korgMs2000Adapter';
import { YamahaDx7ImportAdapter, YamahaDx7ExportAdapter, YamahaDx7HardwareLink } from './yamahaDx7Adapter';
import { CasioCzImportAdapter, CasioCzExportAdapter, CasioCzHardwareLink } from './casioCzAdapter';
import { BehringerDm12ImportAdapter, BehringerDm12ExportAdapter } from './behringerDm12Adapter';
import { BehringerPro800ImportAdapter, BehringerPro800ExportAdapter, BehringerPro800HardwareLink } from './behringerPro800Adapter';
import { BehringerDeepMind12HardwareLink } from './behringerDeepMindAdapter';
import { RolandAiraImportAdapter, RolandAiraExportAdapter, RolandAiraHardwareLink } from './rolandAiraAdapter';
import type { ImportAdapter } from '../ImportAdapter';
import type { ExportAdapter } from '../ExportAdapter';
import type { HardwareLinkContract } from '../HardwareLinkContract';

export const allImportAdapters: ImportAdapter[] = [
  new RolandAiraImportAdapter(),
  new RolandJunoImportAdapter(),
  new KorgMs2000ImportAdapter(),
  new YamahaDx7ImportAdapter(),
  new CasioCzImportAdapter(),
  new BehringerDm12ImportAdapter(),
  new BehringerPro800ImportAdapter()
];

export const allExportAdapters: ExportAdapter[] = [
  new RolandAiraExportAdapter(),
  new RolandJunoExportAdapter(),
  new KorgMs2000ExportAdapter(),
  new YamahaDx7ExportAdapter(),
  new CasioCzExportAdapter(),
  new BehringerDm12ExportAdapter(),
  new BehringerPro800ExportAdapter()
];

export const allHardwareLinks: HardwareLinkContract[] = [
  new RolandAiraHardwareLink(),
  new RolandJunoHardwareLink(),
  new KorgMs2000HardwareLink(),
  new YamahaDx7HardwareLink(),
  new CasioCzHardwareLink(),
  new BehringerDeepMind12HardwareLink(),
  new BehringerPro800HardwareLink()
];

export function getImportAdapter(modelId: string): ImportAdapter | undefined {
  return allImportAdapters.find(adapter => adapter.targetModelIds.includes(modelId));
}

export function getExportAdapter(modelId: string): ExportAdapter | undefined {
  return allExportAdapters.find(adapter => adapter.targetModelIds.includes(modelId));
}
