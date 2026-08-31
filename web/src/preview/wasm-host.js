let wasmFrame = null;

function ensureFrameDocument(frameWindow) {
  const documentRef = frameWindow.document;
  if (!documentRef.body) {
    documentRef.open();
    documentRef.write("<!DOCTYPE html><html><head></head><body></body></html>");
    documentRef.close();
  }
}

export function getWasmWindow() {
  return wasmFrame?.contentWindow ?? window;
}

export function getModule() {
  return getWasmWindow().Module;
}

export function createWasmHost() {
  destroyWasmHost();

  wasmFrame = document.createElement("iframe");
  wasmFrame.setAttribute("aria-hidden", "true");
  wasmFrame.tabIndex = -1;
  wasmFrame.style.cssText = "position:absolute;width:0;height:0;border:0;opacity:0;pointer-events:none";
  wasmFrame.src = "about:blank";
  document.body.appendChild(wasmFrame);

  const frameWindow = wasmFrame.contentWindow;
  ensureFrameDocument(frameWindow);
  return frameWindow;
}

export function destroyWasmHost() {
  if (wasmFrame) {
    wasmFrame.remove();
    wasmFrame = null;
  }
}

export function getWasmDocument() {
  return getWasmWindow().document;
}
