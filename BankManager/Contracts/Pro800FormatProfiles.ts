import type { SysexFormatProfile } from './SysexFormatProfile.ts';

/** Profiles supported by the local Pro-800 evidence set. */
export const pro800FormatProfiles: SysexFormatProfile[] = [
  {
    profileId: 'behringer-pro800-fw-legacy-v109',
    modelId: 'behringer-pro800',
    displayName: 'Behringer Pro-800 patch format v109',
    firmwareRange: { max: '1.2.7' },
    manufacturerId: [0x00, 0x20, 0x32],
    modelIdBytes: [0x00, 0x01, 0x24],
    commands: { request: 0x77, response: 0x78 },
    rawDataSize: 166,
    packing: '8to7',
    checksum: 'none',
    addressing: 'A001..D100',
    notes: 'Legacy variable-length records; parse through the name terminator and treat trailing bytes as padding.'
  },
  {
    profileId: 'behringer-pro800-fw-legacy-v110',
    modelId: 'behringer-pro800',
    displayName: 'Behringer Pro-800 patch format v110',
    firmwareRange: { max: '1.2.7' },
    manufacturerId: [0x00, 0x20, 0x32],
    modelIdBytes: [0x00, 0x01, 0x24],
    commands: { request: 0x77, response: 0x78 },
    rawDataSize: 168,
    packing: '8to7',
    checksum: 'none',
    addressing: 'A001..D100',
    notes: 'Includes LFO aftertouch amount after the name.'
  },
  {
    profileId: 'behringer-pro800-fw-v111',
    modelId: 'behringer-pro800',
    displayName: 'Behringer Pro-800 patch format v111',
    firmwareRange: { min: '1.3.6' },
    manufacturerId: [0x00, 0x20, 0x32],
    modelIdBytes: [0x00, 0x01, 0x24],
    commands: { request: 0x77, response: 0x78 },
    rawDataSize: 173,
    packing: '8to7',
    checksum: 'none',
    addressing: 'A001..D100',
    notes: 'Fixed 16-byte name field and additional voice spread, key reference, glide mode and pitchbend fields.'
  }
];
