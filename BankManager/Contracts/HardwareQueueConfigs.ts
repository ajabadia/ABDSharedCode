/**
 * ABD Bank Manager — Hardware queue configs (canonical)
 *
 * Extraido de Source/Core/MidiSysExQueue.ts (ABDBankManager) durante el corte
 * F4 (DOCS/bank-manager-module-cut.md): los ModelContracts los usan en
 * getMidiConfig() y la cola del consumidor reutiliza los mismos valores, asi
 * que viven en el modulo compartido como SSOT y ambos lados importan de aqui.
 */
/**
 * Hardware-specific queue configurations
 */
export const HARDWARE_QUEUE_CONFIGS = {
  'casio-cz': { interMessageDelayMs: 100, dumpTimeoutMs: 5000 },
  'roland-juno': { interMessageDelayMs: 50, dumpTimeoutMs: 3000 },
  'korg-ms2000': { interMessageDelayMs: 20, dumpTimeoutMs: 2000 },
  'behringer-dm12': { interMessageDelayMs: 10, dumpTimeoutMs: 1000 },
  'yamaha-dx7': { interMessageDelayMs: 20, dumpTimeoutMs: 2000 }
} as const;
