/**
 * ABDScope Mount Layout Helpers
 * =============================
 * Pure, DOM-state-free helpers shared by MountBase, EmbeddedMount, FloatingMount
 * and LaneController: lane-count parsing, non-duplicated picking, grid column
 * span resolution, lane frame routing. Kept under the 200-line budget.
 */

export const DEFAULT_ENABLED_MODES = ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'];
export const COMPACT_MODES = ['lissajous', 'phase'];
export const DEFAULT_AVAILABLE_TAPS = [{ id: 'master', name: 'Master Out' }];

/** Normalize a layout token ('single'|'dual'|'triple'|'1'..'N') to 1..maxLanes. */
export function parseLaneCount(layout, maxLanes) {
  if (layout === 'dual') return 2;
  if (layout === 'triple') return 3;
  if (layout === 'single') return 1;
  const n = parseInt(layout, 10);
  return isNaN(n) ? 1 : Math.max(1, Math.min(n, maxLanes));
}

/** Extract the id of a candidate ({id,name} or bare string). */
export function candidateId(candidate) {
  return typeof candidate === 'string' ? candidate : candidate?.id;
}

/**
 * Pick the first unused candidate id, falling back to the least repeated one
 * so newly added lanes never duplicate an active mode/tap when possible.
 */
export function pickNextAvailable(candidates, existingIds) {
  if (!candidates || candidates.length === 0) return null;

  for (const item of candidates) {
    const id = candidateId(item);
    if (!existingIds.includes(id)) return id;
  }

  const counts = new Map();
  candidates.forEach(c => counts.set(candidateId(c), 0));
  existingIds.forEach(id => {
    if (counts.has(id)) counts.set(id, counts.get(id) + 1);
  });

  let minCount = Infinity;
  let bestChoice = candidateId(candidates[0]);
  for (const item of candidates) {
    const id = candidateId(item);
    const count = counts.get(id) || 0;
    if (count < minCount) {
      minCount = count;
      bestChoice = id;
    }
  }
  return bestChoice;
}

/**
 * Resolve final effective column spans for the lane stack:
 * - Half-width (1) lanes pair up on shared rows.
 * - A solitary half-width lane auto-expands to full width (2) to leave no gaps.
 */
export function applyEffectiveColSpans(lanes) {
  let pendingHalfLane = null;

  for (const lane of lanes) {
    if (lane.requestedColSpan === 1) {
      if (pendingHalfLane === null) {
        pendingHalfLane = lane;
      } else {
        pendingHalfLane.setEffectiveColSpan(1);
        lane.setEffectiveColSpan(1);
        pendingHalfLane = null;
      }
    } else {
      if (pendingHalfLane !== null) {
        pendingHalfLane.setEffectiveColSpan(2);
        pendingHalfLane = null;
      }
      lane.setEffectiveColSpan(2);
    }
  }

  if (pendingHalfLane !== null) {
    pendingHalfLane.setEffectiveColSpan(2);
  }
}

/** Per-lane height honoring the minimum lane height guarantee. */
export function computeLaneHeight(totalHeight, numLanes, minLaneHeight = 130) {
  const computedH = Math.floor(totalHeight / Math.max(1, numLanes));
  return Math.max(minLaneHeight, computedH);
}

/**
 * Route an incoming frame to a specific lane by its active tap.
 * - Bundled frames ({ taps: {...} }) match by exact tap id then lenient substring.
 * - Single-tap frames carrying tapId only render on the lane using that tap.
 * - Frames without tap metadata (e.g. direct Web Audio) render on every lane.
 */
export function resolveFrameForLane(dataFrame, activeTap) {
  if (!dataFrame) return null;

  if (dataFrame.taps) {
    if (dataFrame.taps[activeTap]) return dataFrame.taps[activeTap];
    const activeLower = String(activeTap).toLowerCase();
    for (const key of Object.keys(dataFrame.taps)) {
      const keyLower = key.toLowerCase();
      if (keyLower.includes(activeLower) || activeLower.includes(keyLower)) {
        return dataFrame.taps[key];
      }
    }
    return null;
  }

  if (dataFrame.tapId) {
    const tapMatches = (dataFrame.tapId === activeTap)
      || dataFrame.tapId.toLowerCase().includes(String(activeTap).toLowerCase())
      || String(activeTap).toLowerCase().includes(dataFrame.tapId.toLowerCase());
    return tapMatches ? dataFrame : null;
  }

  return dataFrame;
}
