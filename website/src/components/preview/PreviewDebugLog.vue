<script setup>
import { ref } from "vue";
import { previewDebugLog, usePreviewDebugLog } from "../../composables/usePreviewDebugLog.js";

const { lines, visible, copyLog } = usePreviewDebugLog();
const copied = ref(false);

async function onCopy() {
  try {
    await copyLog();
    copied.value = true;
    window.setTimeout(() => {
      copied.value = false;
    }, 2000);
  } catch (error) {
    previewDebugLog("error", "Copy failed", error);
  }
}
</script>

<template>
  <div
    v-if="visible"
    class="preview-debug"
  >
    <header class="preview-debug__head">
      <h3>Preview debug log</h3>
      <v-btn
        variant="outlined"
        prepend-icon="mdi-content-copy"
        @click="onCopy"
      >
        {{ copied ? "Copied" : "Copy log" }}
      </v-btn>
    </header>
    <pre class="preview-debug__body"><code
      v-for="(line, lineIndex) in lines"
      :key="lineIndex"
      class="preview-debug__line"
      :class="`preview-debug__line--${line.kind}`"
    >{{ line.at }} [{{ line.kind }}] {{ line.message }}
</code></pre>
  </div>
</template>
