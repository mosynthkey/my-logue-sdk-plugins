import { createApp } from "vue";
import App from "./App.vue";
import vuetify from "./plugins/vuetify.js";
import { initPreviewDebugLog } from "./composables/usePreviewDebugLog.js";
import "../styles.css";

initPreviewDebugLog();

createApp(App).use(vuetify).mount("#app");
