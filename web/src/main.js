import { createApp } from "vue";
import App from "./App.vue";
import { initPreviewDebugLog } from "./composables/usePreviewDebugLog.js";
import "../styles.css";

initPreviewDebugLog();

createApp(App).mount("#app");
