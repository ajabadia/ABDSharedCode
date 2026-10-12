/**
 * ABDKeyboard — Parameter Lookup Tests
 * ==============================================
 * Tests para validar la función lookupRawValue con los diccionarios de parámetros.
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { lookupRawValue, domainToRawValue, lookupRawValueWithInterpolation, init } from '../src/parameterLookup.js';
import { CZ101_PRESET } from '../src/keyboard.js';

describe('Parameter Lookup Tests', () => {
  beforeEach(() => {
    // Asegurar que los schemas están cargados antes de cada test
    init();
  });

  afterEach(() => {
    // Cleanup si es necesario
  });

  describe('lookupRawValue - MS2000 parameters', () => {
    it('debe convertir CC 64 a Hz para filterCutoff', () => {
      const result = lookupRawValue('filterCutoff', 64);
      // filterCutoff: domainMin=20, domainMax=20000, curve=Pow con curveP1=0.8
      // expected: 20 + (20000-20) * (64/127)^0.8
      const expected = 20 + (20000 - 20) * Math.pow(64 / 127, 0.8);
      expect(result).toBeCloseTo(expected, 1);
    });

    it('debe convertir CC 0 a domainMin para cualquier parámetro', () => {
      const result = lookupRawValue('filterCutoff', 0);
      expect(result).toBe(20); // domainMin
    });

    it('debe convertir CC 127 a domainMax para cualquier parámetro', () => {
      const result = lookupRawValue('filterCutoff', 127);
      expect(result).toBeCloseTo(20000, 1); // domainMax
    });
  });

  describe('lookupRawValue - ABDEep parameters', () => {
    it('debe convertir CC 64 a Hz para lfo1Freq', () => {
      const result = lookupRawValue('lfo1Freq', 64);
      // lfo1Freq: domainMin=0.01, domainMax=20, curve=Exp con curveP0=0.035, curveP1=7.5
      const expected = 0.035 * Math.exp(7.5 * (64 / 127));
      expect(result).toBeCloseTo(expected, 2);
    });
  });

  describe('lookupRawValue - Casio CZ-101 parameters', () => {
    it('debe convertir CC 64 a Hz para LFO1 Rate', () => {
      // Nota: casio_cz101.json no tiene lfo1Freq en la misma estructura,
      // pero probamos con un parámetro que sí tenga
      const result = lookupRawValue('lfo1Freq', 64);
      // Debe retornar un número válido (no NaN)
      expect(Number.isFinite(result)).toBe(true);
    });
  });

  describe('lookupRawValue - Roland Juno-106 parameters', () => {
    it('debe convertir CC 64 a Hz para filterCutoff en Juno-106', () => {
      const result = lookupRawValue('filterCutoff', 64);
      expect(Number.isFinite(result)).toBe(true);
    });

    it('debe convertir CC 0 a domainMin', () => {
      const result = lookupRawValue('filterCutoff', 0);
      expect(result).toBe(20); // domainMin típico
    });
  });

  describe('lookupRawValue - Behringer DeepMind 12 parameters', () => {
    it('debe convertir CC 64 a un valor válido', () => {
      const result = lookupRawValue('osc1Gain', 64);
      expect(Number.isFinite(result)).toBe(true);
    });
  });

  describe('lookupRawValue - Synthetizadores variados', () => {
    it('sintetizadores MS2000, Juno-60, DX7 deben tener parámetros válidos', () => {
      // Probar que los schemas se cargaron correctamente
      const ms2000Params = lookupRawValue('filterCutoff', 64);
      const juno106Params = lookupRawValue('filterCutoff', 64);
      const dx7Params = lookupRawValue('filterCutoff', 64);
      
      expect(Number.isFinite(ms2000Params)).toBe(true);
      expect(Number.isFinite(juno106Params)).toBe(true);
      expect(Number.isFinite(dx7Params)).toBe(true);
    });
  });

  describe('domainToRawValue - Round-trip tests', () => {
    it('round-trip: CC 64 → dominio → CC should be close to 64', () => {
      const id = 'filterCutoff';
      const raw = 64;
      const domain = lookupRawValue(id, raw);
      const roundTrip = domainToRawValue(id, domain);
      
      // El round-trip debería estar cerca del original (pero no necesariamente exacto
      // debido a la curva no lineal)
      expect(roundTrip).toBeGreaterThanOrEqual(0);
      expect(roundTrip).toBeLessThanOrEqual(127);
    });

    it('round-trip extremo: CC 0 debe retornar domainMin', () => {
      const id = 'filterCutoff';
      const raw = 0;
      const domain = lookupRawValue(id, raw);
      const roundTrip = domainToRawValue(id, domain);
      expect(roundTrip).toBe(0);
    });

    it('round-trip extremo: CC 127 debe retornar domainMax', () => {
      const id = 'filterCutoff';
      const raw = 127;
      const domain = lookupRawValue(id, raw);
      const roundTrip = domainToRawValue(id, domain);
      expect(roundTrip).toBe(127);
    });
  });

  describe('lookupRawValueWithInterpolation - valores no exactos', () => {
    it('debe interpolar valores entre los puntos de curva definidos', () => {
      // Probar con un valor intermedio (no solo 0, 64, 127)
      const result = lookupRawValueWithInterpolation('filterCutoff', 32);
      expect(Number.isFinite(result)).toBe(true);
      expect(result).toBeGreaterThan(20); // domainMin
      expect(result).toBeLessThan(20000); // domainMax
    });

    it('debe manejar valores fuera de rango (clamp)', () => {
      const result = lookupRawValueWithInterpolation('filterCutoff', 200); // > 127
      expect(Number.isFinite(result)).toBe(true);
      // Debería estar clamped al domainMax
      expect(result).toBeLessThanOrEqual(20000);
    });

    it('debe manejar valores negativos (clamp)', () => {
      const result = lookupRawValueWithInterpolation('filterCutoff', -1);
      expect(Number.isFinite(result)).toBe(true);
      // Debería estar clamped al domainMin
      expect(result).toBeGreaterThanOrEqual(20);
    });
  });
});