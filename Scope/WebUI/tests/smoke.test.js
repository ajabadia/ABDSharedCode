import { describe, it, expect } from 'vitest';

describe('ABDScope Environment Smoke Test', () => {
  it('should execute tests cleanly in node environment', () => {
    expect(true).toBe(true);
  });

  it('should support Float32Array allocations', () => {
    const buffer = new Float32Array(512);
    expect(buffer.length).toBe(512);
    expect(buffer[0]).toBe(0);
  });
});
