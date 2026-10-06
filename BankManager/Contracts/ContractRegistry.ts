/**
 * ABD Bank Manager — ContractRegistry
 *
 * Registro declarativo de ModelContracts, ImportAdapters, ExportAdapters y
 * HardwareLinks que el core/UI consultan para auto-configurarse (diseño §4.5).
 *
 * La naturaleza del despliegue (standalone vs plugin) la determina el conjunto
 * de contratos registrados, no modos ni flags:
 *   - Standalone: registrar todos los contratos → gestor universal multi-modelo.
 *   - Plugin: registrar solo su ModelContract (+ compatibles) → gestor acotado.
 *
 * Reglas de registro (diseño §4.5):
 *   - modelId duplicado            → error (throw)
 *   - adapterId duplicado          → error (throw)
 *   - HardwareLink sin ModelContract → error (throw)
 *   - ImportAdapter.targetModelIds sin modelo registrado → warning (issue)
 */

import { validateModelContract, type ModelContract } from './ModelContract.ts';
import type { ImportAdapter } from './ImportAdapter.ts';
import type { ExportAdapter } from './ExportAdapter.ts';
import type { HardwareLinkContract } from './HardwareLinkContract.ts';
import { getMidiConfig as deriveMidiConfig, type MidiConfig } from './Models/index.ts';
import { allModelContracts } from './Models/index.ts';
import { allImportAdapters, allExportAdapters, allHardwareLinks } from './Adapters/index.ts';

export type DeploymentMode = 'standalone' | 'plugin';

export interface RegistryIssue {
  kind: 'warning' | 'error';
  message: string;
}

export class ContractRegistry {
  private models = new Map<string, ModelContract>();
  private importAdapters = new Map<string, ImportAdapter>();
  private exportAdapters = new Map<string, ExportAdapter>();
  private hardwareLinks = new Map<string, HardwareLinkContract>();
  private issues: RegistryIssue[] = [];

  // ─── Registro (declarativo, validado al registrar) ───

  registerModel(contract: ModelContract): void {
    const validation = validateModelContract(contract);
    if (!validation.valid) {
      throw new Error(
        `ContractRegistry: ModelContract inválido (${contract.modelId || '(sin modelId)'}): ${validation.errors.join('; ')}`
      );
    }
    if (this.models.has(contract.modelId)) {
      throw new Error(`ContractRegistry: modelId duplicado '${contract.modelId}'`);
    }
    this.models.set(contract.modelId, contract);
  }

  registerImportAdapter(adapter: ImportAdapter): void {
    if (!adapter.adapterId || typeof adapter.canParse !== 'function' || typeof adapter.parse !== 'function') {
      throw new Error('ContractRegistry: ImportAdapter inválido (adapterId, canParse y parse requeridos)');
    }
    if (this.importAdapters.has(adapter.adapterId)) {
      throw new Error(`ContractRegistry: adapterId duplicado '${adapter.adapterId}'`);
    }
    for (const modelId of adapter.targetModelIds || []) {
      if (!this.models.has(modelId)) {
        this.issues.push({
          kind: 'warning',
          message: `ImportAdapter '${adapter.adapterId}': targetModelIds '${modelId}' no tiene ModelContract registrado (formato genérico?)`
        });
      }
    }
    this.importAdapters.set(adapter.adapterId, adapter);
  }

  registerExportAdapter(adapter: ExportAdapter): void {
    if (!adapter.adapterId || typeof adapter.serialize !== 'function') {
      throw new Error('ContractRegistry: ExportAdapter inválido (adapterId y serialize requeridos)');
    }
    if (this.exportAdapters.has(adapter.adapterId)) {
      throw new Error(`ContractRegistry: adapterId duplicado '${adapter.adapterId}'`);
    }
    this.exportAdapters.set(adapter.adapterId, adapter);
  }

  registerHardwareLink(link: HardwareLinkContract): void {
    if (!this.models.has(link.modelId)) {
      throw new Error(
        `ContractRegistry: HardwareLinkContract para '${link.modelId}' sin ModelContract registrado`
      );
    }
    this.hardwareLinks.set(link.modelId, link);
  }

  // ─── Consulta — el core/UI se auto-configuran SOLO a partir de esto ───

  /** 'standalone' si hay más de un modelo registrado; si no, 'plugin'. */
  get mode(): DeploymentMode {
    return this.models.size > 1 ? 'standalone' : 'plugin';
  }

  getModels(): ModelContract[] {
    return [...this.models.values()];
  }

  getModel(modelId: string): ModelContract | undefined {
    return this.models.get(modelId);
  }

  getCompatibleModels(modelId: string): string[] {
    return this.models.get(modelId)?.compatibleModels || [];
  }

  /** Asociación multi-hardware: [canónico, ...compatibles] (forward + reverse). */
  getHardwareIds(modelId: string): string[] {
    const contract = this.models.get(modelId);
    if (!contract) return [modelId];
    const ids = new Set<string>([modelId]);
    if (contract.compatibleModels) {
      for (const id of contract.compatibleModels) ids.add(id);
    }
    for (const [id, c] of this.models) {
      if (id !== modelId && c.compatibleModels?.includes(modelId)) ids.add(id);
    }
    return Array.from(ids);
  }

  getImportAdapters(modelId?: string): ImportAdapter[] {
    const all = [...this.importAdapters.values()];
    if (!modelId) return all;
    return all.filter(a => a.targetModelIds.includes(modelId));
  }

  getExportAdapters(modelId?: string): ExportAdapter[] {
    const all = [...this.exportAdapters.values()];
    if (!modelId) return all;
    return all.filter(a => a.targetModelIds.includes(modelId));
  }

  getHardwareLinks(modelId?: string): HardwareLinkContract[] {
    const all = [...this.hardwareLinks.values()];
    if (!modelId) return all;
    return all.filter(l => l.modelId === modelId);
  }

  /**
   * Devuelve la cobertura real del bundle: cada modelo y las capacidades
   * registradas para él. Los booleanos distinguen ausencia de implementación
   * de un fallo de consulta.
   */
  getCoverage(): Array<{
    modelId: string;
    importAdapters: string[];
    exportAdapters: string[];
    hardwareLinks: boolean;
  }> {
    return this.getModels().map(model => ({
      modelId: model.modelId,
      importAdapters: this.getImportAdapters(model.modelId).map(adapter => adapter.adapterId),
      exportAdapters: this.getExportAdapters(model.modelId).map(adapter => adapter.adapterId),
      hardwareLinks: this.getHardwareLinks(model.modelId).length > 0
    }));
  }

  /** Canal/device + timing de la cola MIDI, derivados (no editables). */
  getMidiConfig(modelId: string): MidiConfig {
    return deriveMidiConfig(modelId);
  }

  isSupported(modelId: string): boolean {
    return this.models.has(modelId);
  }

  /** Issues (warnings/errores no fatales) acumulados durante el registro. */
  getIssues(): RegistryIssue[] {
    return [...this.issues];
  }
}

/**
 * Registry para el despliegue standalone: registra todos los ModelContracts
 * del monorepo, junto con los adapters y HardwareLinks concretos disponibles.
 * Los formatos aún no implementados (por ejemplo tape/clipboard) no se
 * registran hasta disponer de una implementación verificable.
 */
export function createStandaloneRegistry(): ContractRegistry {
  const registry = new ContractRegistry();
  for (const contract of allModelContracts) {
    registry.registerModel(contract);
  }
  for (const adapter of allImportAdapters) {
    registry.registerImportAdapter(adapter);
  }
  for (const adapter of allExportAdapters) {
    registry.registerExportAdapter(adapter);
  }
  for (const link of allHardwareLinks) {
    registry.registerHardwareLink(link);
  }
  return registry;
}
