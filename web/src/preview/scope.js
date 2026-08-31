function accentColor() {
  return getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() || "#b6b2a1";
}

function prepareCanvas(canvas) {
  const rect = canvas.getBoundingClientRect();
  if (rect.width < 2 || rect.height < 2) {
    return null;
  }
  const cssWidth = rect.width;
  const cssHeight = rect.height;
  const dpr = window.devicePixelRatio || 1;
  const pixelWidth = Math.max(1, Math.round(cssWidth * dpr));
  const pixelHeight = Math.max(1, Math.round(cssHeight * dpr));
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, cssWidth, cssHeight };
}

export function paintTimeDomainScope(canvas, timeDomain, referenceFrequency, sampleRate) {
  const prepared = prepareCanvas(canvas);
  if (!prepared || !timeDomain?.length) {
    return;
  }
  const { ctx, cssWidth, cssHeight } = prepared;
  ctx.fillStyle = "#0d0d0d";
  ctx.fillRect(0, 0, cssWidth, cssHeight);
  ctx.lineWidth = 1;
  ctx.strokeStyle = accentColor();
  ctx.beginPath();

  const frequency = Math.max(referenceFrequency || 440, 1);
  let startIndex = 0;
  let scoreMax = 0;
  const dx = Math.floor(400 / frequency) + 1;

  for (let sampleIndex = dx; sampleIndex < timeDomain.length / 2; sampleIndex += 1) {
    if (timeDomain[sampleIndex - 1] > 0 && timeDomain[sampleIndex + 1] < 0) {
      const score = (timeDomain[sampleIndex - dx] - timeDomain[sampleIndex + dx])
        - timeDomain[sampleIndex + 5 * dx];
      if (score > scoreMax) {
        scoreMax = score;
        startIndex = sampleIndex;
      }
    }
  }

  const fftSize = timeDomain.length;
  let numHalfCycles = Math.floor(fftSize * frequency / sampleRate) - 1;
  let endIndex = timeDomain.length - 1;
  if (numHalfCycles > 0) {
    endIndex = Math.round(startIndex + numHalfCycles * sampleRate / frequency / 2);
  }

  const deltaX = cssWidth / Math.max(endIndex - startIndex, 1);
  let x = 0;
  for (let sampleIndex = startIndex; sampleIndex <= endIndex; sampleIndex += 1) {
    const value = timeDomain[sampleIndex];
    const y = (value * 0.5 + 1.0) * cssHeight / 2;
    if (sampleIndex === startIndex) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
    x += deltaX;
  }
  ctx.stroke();
}

export function paintFrequencyScope(canvas, frequencyData) {
  const prepared = prepareCanvas(canvas);
  if (!prepared || !frequencyData?.length) {
    return;
  }
  const { ctx, cssWidth, cssHeight } = prepared;
  ctx.fillStyle = "#0d0d0d";
  ctx.fillRect(0, 0, cssWidth, cssHeight);

  const barWidth = cssWidth / frequencyData.length;
  const color = accentColor();
  let posX = 0;
  for (let binIndex = 0; binIndex < frequencyData.length; binIndex += 1) {
    const barHeight = frequencyData[binIndex] + cssHeight;
    ctx.fillStyle = color;
    ctx.fillRect(posX, cssHeight - barHeight, barWidth, barHeight);
    posX += barWidth;
  }
}
