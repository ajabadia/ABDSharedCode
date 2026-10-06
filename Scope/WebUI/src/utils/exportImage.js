/**
 * ABDScope Frame Capture Utility
 * ===============================
 * Exports visual canvas snapshots to PNG download or system clipboard.
 */

/**
 * Copy canvas visual snapshot to system clipboard as PNG.
 * @param {HTMLCanvasElement} canvas - Source canvas
 * @returns {Promise<boolean>} True if successfully copied
 */
export async function copyCanvasToClipboard(canvas) {
  if (!canvas || !navigator?.clipboard?.write) return false;

  return new Promise((resolve) => {
    canvas.toBlob(async (blob) => {
      if (!blob) return resolve(false);
      try {
        const item = new ClipboardItem({ 'image/png': blob });
        await navigator.clipboard.write([item]);
        resolve(true);
      } catch (err) {
        resolve(false);
      }
    }, 'image/png');
  });
}

/**
 * Trigger immediate browser download of current canvas as PNG.
 * @param {HTMLCanvasElement} canvas - Source canvas
 * @param {string} [filename='abd-scope-snapshot.png'] - Target filename
 */
export function downloadCanvasAsPng(canvas, filename = 'abd-scope-snapshot.png') {
  if (!canvas || typeof document === 'undefined') return;

  const link = document.createElement('a');
  link.download = filename;
  link.href = canvas.toDataURL('image/png');
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}
