/**
 * Platform-neutral MIDI port shapes used by hardware discovery.
 * Browser Web MIDI, Tauri and JUCE adapters can all provide these fields.
 */
export interface MidiPortInfo {
  id?: string;
  name?: string;
  manufacturer?: string;
  type?: 'input' | 'output' | string;
}

export type MidiOutputPortInfo = MidiPortInfo;
