export type Pro800ParameterKind = 'u8' | 'u16le' | 'float32le' | 'name' | 'bitmask';

export interface Pro800ParameterOption {
  value: number;
  label: string;
}

export interface Pro800ParameterDefinition {
  id: string;
  name: string;
  description: string;
  offset: number;
  length: number;
  kind: Pro800ParameterKind;
  min?: number;
  max?: number;
  defaultValue?: number;
  unit?: string;
  sinceFormatVersion?: number;
  options?: Pro800ParameterOption[];
  notes?: string;
  ccMsb?: number;
  ccLsb?: number;
  firmwareRange?: { min?: string; max?: string };
}

const u8 = (id: string, name: string, offset: number, description: string, options?: Pro800ParameterOption[], sinceFormatVersion?: number): Pro800ParameterDefinition => ({
  id, name, description, offset, length: 1, kind: 'u8', min: 0, max: 255, options, sinceFormatVersion
});

const u16 = (id: string, name: string, offset: number, description: string, ccMsb?: number, ccLsb?: number): Pro800ParameterDefinition => ({
  id, name, description, offset, length: 2, kind: 'u16le', min: 0, max: 65535, ccMsb, ccLsb
});

const continuous = [
  ['osc-a-frequency', 'Osc A Frequency', 5], ['osc-a-level', 'Osc A Level', 7], ['osc-a-pulse-width', 'Osc A Pulse Width', 9],
  ['osc-b-frequency', 'Osc B Frequency', 11], ['osc-b-level', 'Osc B Level', 13], ['osc-b-pulse-width', 'Osc B Pulse Width', 15],
  ['osc-b-fine-frequency', 'Osc B Fine Frequency', 17], ['filter-cutoff', 'Filter Cutoff', 19], ['filter-resonance', 'Filter Resonance', 21],
  ['filter-envelope-amount', 'Filter Envelope Amount', 23], ['filter-release', 'Filter Release', 25], ['filter-sustain', 'Filter Sustain', 27],
  ['filter-decay', 'Filter Decay', 29], ['filter-attack', 'Filter Attack', 31], ['amp-release', 'Amp Release', 33],
  ['amp-sustain', 'Amp Sustain', 35], ['amp-decay', 'Amp Decay', 37], ['amp-attack', 'Amp Attack', 39],
  ['polymod-filter-envelope', 'PolyMod Filter Envelope', 41], ['polymod-osc-b', 'PolyMod Osc B', 43], ['lfo-frequency', 'LFO Frequency', 45],
  ['lfo-amount', 'LFO Amount', 47], ['glide-amount', 'Glide Amount', 49], ['amp-velocity', 'Amp Velocity', 51],
  ['filter-velocity', 'Filter Velocity', 53], ['modulation-delay', 'Modulation Delay', 76], ['vibrato-speed', 'Vibrato Speed', 78],
  ['vibrato-amount', 'Vibrato Amount', 80], ['unison-detune', 'Unison Detune', 82], ['noise-amount', 'Noise Amount', 142],
  ['amp-aftertouch', 'Amp Aftertouch Amount', 144], ['filter-aftertouch', 'Filter Aftertouch Amount', 146],
  ['lfo-aftertouch', 'LFO Aftertouch Amount', 166], ['pitchbend-range', 'Pitchbend Range', 171]
] as const;

const stepped: Pro800ParameterDefinition[] = [
  u8('osc-a-saw', 'Osc A Saw', 55, 'Oscillator A saw waveform'), u8('osc-a-tri', 'Osc A Tri', 56, 'Oscillator A triangle waveform'),
  u8('osc-a-rect', 'Osc A Rect', 57, 'Oscillator A rectangle waveform'), u8('osc-b-saw', 'Osc B Saw', 58, 'Oscillator B saw waveform'),
  u8('osc-b-tri', 'Osc B Tri', 59, 'Oscillator B triangle waveform'), u8('osc-b-rect', 'Osc B Rect', 60, 'Oscillator B rectangle waveform'),
  u8('osc-a-sync', 'Osc A Sync', 61, 'Oscillator sync'), u8('polymod-frequency-a', 'PolyMod Frequency A', 62, 'PolyMod frequency destination'),
  u8('polymod-filter', 'PolyMod Filter', 63, 'PolyMod filter destination'),
  u8('lfo-shape', 'LFO Shape', 64, 'LFO waveform', [{ value: 0, label: 'Pulse' }, { value: 1, label: 'Tri' }, { value: 2, label: 'Rand' }, { value: 3, label: 'Sin' }, { value: 4, label: 'Noise' }, { value: 5, label: 'Saw' }]),
  u8('lfo-speed', 'LFO Speed', 65, 'LFO speed', [{ value: 0, label: 'Slow' }, { value: 1, label: 'Fast' }]),
  { ...u8('lfo-destination', 'LFO Destination', 66, 'LFO destination bitmask'), kind: 'bitmask' },
  u8('filter-keyboard-tracking', 'Filter Keyboard Tracking', 67, 'Filter keyboard tracking', [{ value: 0, label: 'Off' }, { value: 1, label: 'Half' }, { value: 2, label: 'Full' }]),
  u8('filter-envelope-shape', 'Filter Envelope Shape', 68, 'Filter envelope shape', [{ value: 0, label: 'Linear' }, { value: 1, label: 'Exponential' }]),
  u8('filter-envelope-speed', 'Filter Envelope Speed', 69, 'Filter envelope speed', [{ value: 0, label: 'Fast' }, { value: 1, label: 'Slow' }]),
  u8('amp-envelope-shape', 'Amp Envelope Shape', 70, 'Amp envelope shape', [{ value: 0, label: 'Linear' }, { value: 1, label: 'Exponential' }]),
  u8('unison', 'Unison', 71, 'Unison enabled', [{ value: 0, label: 'Off' }, { value: 1, label: 'On' }]),
  u8('pitchbend-target', 'Pitchbend Target', 72, 'Pitchbend destination', [{ value: 0, label: 'Off' }, { value: 1, label: 'VCO' }, { value: 2, label: 'VCF' }, { value: 3, label: 'Volume' }]),
  u8('mod-wheel-amount', 'Mod Wheel Amount', 73, 'Mod wheel amount'), u8('osc-a-frequency-mode', 'Osc A Frequency Pot Mode', 74, 'Osc A tuning mode'),
  u8('osc-b-frequency-mode', 'Osc B Frequency Pot Mode', 75, 'Osc B tuning mode'), u8('mod-wheel-target', 'Mod Wheel Target', 84, 'Mod wheel destination'),
  u8('amp-envelope-speed', 'Amp Envelope Speed', 148, 'Amp envelope speed'), u8('arp-mode', 'Arpeggiator Mode', 149, 'Arpeggiator mode'),
  u8('voice-spread', 'Voice Spread Enable', 168, 'Voice spread enabled', undefined, 111),
  u8('key-tracking-reference', 'Key Tracking Reference Note', 169, 'Key tracking reference note', undefined, 111),
  u8('glide-mode', 'Glide Mode', 170, 'Glide mode', undefined, 111)
];

export const PRO800_PARAMETER_SCHEMA: Pro800ParameterDefinition[] = [
  ...continuous.map(([id, name, offset], index) => ({
    ...u16(id, name, offset, `${name} stored as unsigned 16-bit little-endian value`, index < 31 ? index + 8 : undefined, index < 31 ? index + 80 : undefined),
    sinceFormatVersion: id === 'lfo-aftertouch' ? 110 : id === 'pitchbend-range' ? 111 : undefined
  })),
  ...stepped,
  ...Array.from({ length: 8 }, (_, index) => u8(`chord-note-${index + 1}`, `Chord Note ${index + 1}`, 86 + index, 'Chord interval in semitones; 255 means no note')),
  ...Array.from({ length: 12 }, (_, index) => ({ id: `tuning-${index}`, name: `Tune Per Note ${['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][index]}`, description: 'Per-note tuning float32 little-endian', offset: 94 + index * 4, length: 4, kind: 'float32le' as const, notes: 'Scaling is device-specific; zero represents 12TET.' })),
  { id: 'patch-name', name: 'Patch Name', description: 'Patch display name', offset: 150, length: 16, kind: 'name', max: 16 }
];

export function getPro800Parameter(id: string): Pro800ParameterDefinition | undefined {
  return PRO800_PARAMETER_SCHEMA.find(parameter => parameter.id === id);
}

export function getPro800ParametersForFormat(formatVersion: number): Pro800ParameterDefinition[] {
  return PRO800_PARAMETER_SCHEMA.filter(parameter => !parameter.sinceFormatVersion || formatVersion >= parameter.sinceFormatVersion);
}

export function decodePro800Parameter(parameter: Pro800ParameterDefinition, rawData: Uint8Array): number | string | number[] | null {
  if (parameter.offset + parameter.length > rawData.length) return null;
  if (parameter.kind === 'name') {
    return new TextDecoder().decode(rawData.slice(parameter.offset, parameter.offset + parameter.length)).replace(/[\0\x80\x7F].*$/s, '').trim();
  }
  if (parameter.kind === 'u16le') return rawData[parameter.offset] | (rawData[parameter.offset + 1] << 8);
  if (parameter.kind === 'float32le') return new DataView(rawData.buffer, rawData.byteOffset + parameter.offset, 4).getFloat32(0, true);
  if (parameter.kind === 'bitmask') {
    const value = rawData[parameter.offset];
    return Array.from({ length: 7 }, (_, bit) => (value & (1 << bit)) ? bit : -1).filter(bit => bit >= 0);
  }
  return rawData[parameter.offset];
}

export function decodePro800Parameters(rawData: Uint8Array, formatVersion = rawData[4] || 111) {
  return getPro800ParametersForFormat(formatVersion).map(parameter => ({
    ...parameter,
    value: decodePro800Parameter(parameter, rawData)
  }));
}
