import { describe, it, expect, beforeEach } from 'vitest';
import {
  rawToAftertouchPressure,
  rawToModWheelDepth,
  pressureToRaw,
  modWheelToRaw,
  rawToAftertouchDomain,
  rawToModWheelDomain,
} from '../src/parameterLookup.js';

describe('Aftertouch and ModWheel Conversion', () => {
  beforeEach(() => {
    // Setup inicial antes de cada prueba
  });

  describe('rawToAftertouchPressure', () => {
    it('debe convertir CC 64 a presión ~0.5', () => {
      const result = rawToAftertouchPressure(64);
      expect(result).toBeCloseTo(0.5, 1);
    });

    it('debe convertir CC 0 a presión 0', () => {
      const result = rawToAftertouchPressure(0);
      expect(result).toBe(0);
    });

    it('debe convertir CC 127 a presión 1', () => {
      const result = rawToAftertouchPressure(127);
      expect(result).toBeCloseTo(1, 1);
    });
  });

  describe('rawToModWheelDepth', () => {
    it('debe convertir CC 64 a profundidad ~0.5', () => {
      const result = rawToModWheelDepth(64);
      expect(result).toBeCloseTo(0.5, 1);
    });

    it('debe convertir CC 0 a profundidad 0', () => {
      const result = rawToModWheelDepth(0);
      expect(result).toBe(0);
    });

    it('debe convertir CC 127 a profundidad 1', () => {
      const result = rawToModWheelDepth(127);
      expect(result).toBeCloseTo(1, 1);
    });
  });

  describe('pressureToRaw', () => {
    it('debe convertir presión 0.5 a CC ~64', () => {
      const result = pressureToRaw(0.5);
      expect(result).toBeCloseTo(64, 0);
    });

    it('debe convertir presión 0 a CC 0', () => {
      const result = pressureToRaw(0);
      expect(result).toBe(0);
    });

    it('debe convertir presión 1 a CC 127', () => {
      const result = pressureToRaw(1);
      expect(result).toBe(127);
    });
  });

  describe('modWheelToRaw', () => {
    it('debe convertir profundidad 0.5 a CC ~64', () => {
      const result = modWheelToRaw(0.5);
      expect(result).toBeCloseTo(64, 0);
    });
  });

  describe('rawToAftertouchDomain', () => {
    it('debe retornar un número válido para CC 64', () => {
      const result = rawToAftertouchDomain(64);
      expect(Number.isFinite(result)).toBe(true);
    });
  });

  describe('rawToModWheelDomain', () => {
    it('debe retornar un número válido para CC 64', () => {
      const result = rawToModWheelDomain(64);
      expect(Number.isFinite(result)).toBe(true);
    });
  });
});